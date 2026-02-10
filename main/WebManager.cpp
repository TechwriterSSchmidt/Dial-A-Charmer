#include "WebManager.h"
#include "TimeManager.h" // Added TimeManager
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/param.h>
#include <dirent.h>
#include <sys/types.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app_config.h"
#include <sys/stat.h>
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mdns.h"
#include "lwip/apps/netbiosns.h"
#include "app_config.h"
#include "PhonebookManager.h"
#include "cJSON.h"
#include "lwip/sockets.h"

// External reference to play_file from main.cpp
extern void play_file(const char* path);

static const char *TAG = "WEB_MANAGER";
WebManager webManager;

static const size_t LOG_LINE_COUNT = 20;
static const size_t LOG_LINE_MAX = 256;

static char s_log_lines[LOG_LINE_COUNT][LOG_LINE_MAX];
static size_t s_log_head = 0;
static size_t s_log_count = 0;
static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_prev_vprintf = nullptr;

#if APP_ENABLE_SD_LOG
static FILE *s_sd_log_file = nullptr;
static TaskHandle_t s_sd_log_task = nullptr;
static portMUX_TYPE s_sd_log_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_sd_log_buf[APP_SD_LOG_BUFFER_LINES][LOG_LINE_MAX];
static char s_sd_log_flush_buf[APP_SD_LOG_BUFFER_LINES][LOG_LINE_MAX];
static size_t s_sd_log_buf_count = 0;

// WiFi / AP Logic
static esp_event_handler_instance_t s_wifi_event_instance = NULL;
static esp_event_handler_instance_t s_ip_event_instance = NULL;
static int s_wifi_retry_count = 0;
static bool s_ap_mode_active = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_ap_mode_active) return; // Don't retry if we switched to AP

        if (s_wifi_retry_count < 3) {
            esp_wifi_connect();
            s_wifi_retry_count++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        } else {
            ESP_LOGI(TAG, "Connection failed. Switching to AP Mode.");
            webManager.startAPMode();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retry_count = 0;
    }
}

static void sd_log_write_line(const char *line) {
    if (!line || !line[0]) {
        return;
    }
    portENTER_CRITICAL(&s_sd_log_mux);
    if (s_sd_log_buf_count < APP_SD_LOG_BUFFER_LINES) {
        strncpy(s_sd_log_buf[s_sd_log_buf_count], line, LOG_LINE_MAX - 1);
        s_sd_log_buf[s_sd_log_buf_count][LOG_LINE_MAX - 1] = '\0';
        s_sd_log_buf_count++;
    }
    portEXIT_CRITICAL(&s_sd_log_mux);
}

static void sd_log_flush_task(void *pvParameters) {
    (void)pvParameters;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(APP_SD_LOG_FLUSH_INTERVAL_MS));

        size_t count = 0;
        portENTER_CRITICAL(&s_sd_log_mux);
        count = s_sd_log_buf_count;
        for (size_t i = 0; i < count; ++i) {
            strncpy(s_sd_log_flush_buf[i], s_sd_log_buf[i], LOG_LINE_MAX);
            s_sd_log_flush_buf[i][LOG_LINE_MAX - 1] = '\0';
        }
        portEXIT_CRITICAL(&s_sd_log_mux);

        if (count == 0) {
            continue;
        }

        if (!s_sd_log_file) {
            const char *log_path = APP_SD_LOG_PATH;
            char dir_path[128] = {0};
            const char *last = strrchr(log_path, '/');
            if (last && last != log_path) {
                size_t len = (size_t)(last - log_path);
                if (len < sizeof(dir_path)) {
                    memcpy(dir_path, log_path, len);
                    dir_path[len] = '\0';
                    mkdir(dir_path, 0775);
                }
            }
            s_sd_log_file = fopen(log_path, "a");
        }

        if (!s_sd_log_file) {
            continue;
        }

        fseek(s_sd_log_file, 0, SEEK_END);
        long size = ftell(s_sd_log_file);
        if (size >= (long)APP_SD_LOG_MAX_BYTES) {
            freopen(APP_SD_LOG_PATH, "w", s_sd_log_file);
        }
        for (size_t i = 0; i < count; ++i) {
            fputs(s_sd_log_flush_buf[i], s_sd_log_file);
            fputc('\n', s_sd_log_file);
        }
        fflush(s_sd_log_file);
        fsync(fileno(s_sd_log_file));

        portENTER_CRITICAL(&s_sd_log_mux);
        if (s_sd_log_buf_count >= count) {
            size_t remaining = s_sd_log_buf_count - count;
            for (size_t i = 0; i < remaining; ++i) {
                strncpy(s_sd_log_buf[i], s_sd_log_buf[count + i], LOG_LINE_MAX);
                s_sd_log_buf[i][LOG_LINE_MAX - 1] = '\0';
            }
            s_sd_log_buf_count = remaining;
        } else {
            s_sd_log_buf_count = 0;
        }
        portEXIT_CRITICAL(&s_sd_log_mux);
    }
}
#endif

static std::string get_first_ringtone_name() {
    const char *dir_path = "/sdcard/ringtones";
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return "";
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        if (!name || name[0] == '.') {
            continue;
        }
        const char *dot = strrchr(name, '.');
        if (dot && strcasecmp(dot, ".wav") == 0) {
            std::string result = name;
            closedir(dir);
            return result;
        }
    }

    closedir(dir);
    return "";
}

static void compact_log_line(const char *src, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) {
        return;
    }

    // Skip log level character (E, W, I, D, V) if present
    const char *start = src;
    if (src[0] && (src[0] == 'E' || src[0] == 'W' || src[0] == 'I' || src[0] == 'D' || src[0] == 'V') && src[1] == ' ') {
        start = src + 1;  // Skip level letter, keep the space
    }

    const char *open = strchr(start, '(');
    const char *close = open ? strchr(open, ')') : NULL;

    // No timestamp found, just prefix with '>'
    if (!open || !close || close <= open + 1) {
        snprintf(dst, dst_size, ">%s", start);
        return;
    }

    // Check if content in parentheses is all digits (timestamp)
    bool all_digits = true;
    for (const char *p = open + 1; p < close; ++p) {
        if (*p < '0' || *p > '9') {
            all_digits = false;
            break;
        }
    }

    if (!all_digits) {
        snprintf(dst, dst_size, ">%s", start);
        return;
    }

    // Remove timestamp: copy head + tail
    size_t head_len = (size_t)(open - start);
    const char *tail = close + 1;
    size_t tail_len = strlen(tail);
    size_t needed = 1 + head_len + tail_len + 1;  // '>' + head + tail + '\0'

    if (needed > dst_size) {
        snprintf(dst, dst_size, ">%s", start);
        return;
    }

    dst[0] = '>';
    memcpy(dst + 1, start, head_len);
    memcpy(dst + 1 + head_len, tail, tail_len);
    dst[1 + head_len + tail_len] = '\0';

    // Remove tag prefix (everything before and including first ':')
    char *colon = strchr(dst + 1, ':');
    if (colon) {
        // Skip colon and any following spaces
        const char *msg = colon + 1;
        while (*msg == ' ') {
            msg++;
        }
        // Move message to start (after '>')
        memmove(dst + 1, msg, strlen(msg) + 1);
    }
}

static void strip_ansi(const char *src, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) {
        return;
    }
    size_t di = 0;
    for (size_t i = 0; src[i] != '\0' && di + 1 < dst_size; ++i) {
        // ANSI CSI sequence detection: \x1b [ ... [0x40-0x7E]
        if (src[i] == '\x1b' && src[i + 1] == '[') {
            size_t j = i + 2;
            while (src[j] != '\0') {
                if (src[j] >= 0x40 && src[j] <= 0x7E) {
                    i = j; // Found terminator, update i to skip sequence
                    break;
                }
                j++;
            }
            continue;
        }
        dst[di++] = src[i];
    }
    dst[di] = '\0';
}

static void log_buffer_add(const char *line) {
    if (!line || !line[0]) {
        return;
    }
    portENTER_CRITICAL(&s_log_mux);
    size_t idx = s_log_head % LOG_LINE_COUNT;
    strncpy(s_log_lines[idx], line, LOG_LINE_MAX - 1);
    s_log_lines[idx][LOG_LINE_MAX - 1] = '\0';
    s_log_head = (s_log_head + 1) % LOG_LINE_COUNT;
    if (s_log_count < LOG_LINE_COUNT) {
        s_log_count++;
    }
    portEXIT_CRITICAL(&s_log_mux);
}

static int log_vprintf(const char *fmt, va_list args) {
    char buf[LOG_LINE_MAX];
    va_list args_copy;
    va_copy(args_copy, args);
    vsnprintf(buf, sizeof(buf), fmt, args_copy);
    va_end(args_copy);

    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[len - 1] = '\0';
        len--;
    }
    log_buffer_add(buf);

#if APP_ENABLE_SD_LOG
    char clean[LOG_LINE_MAX];
    strip_ansi(buf, clean, sizeof(clean));
    sd_log_write_line(clean);
#endif

    if (s_prev_vprintf) {
        return s_prev_vprintf(fmt, args);
    }
    return vprintf(fmt, args);
}

static void init_log_capture() {
    if (!s_prev_vprintf) {
        s_prev_vprintf = esp_log_set_vprintf(log_vprintf);
    }
#if APP_ENABLE_SD_LOG
    if (!s_sd_log_task) {
        xTaskCreate(sd_log_flush_task, "sd_log_flush", 4096, NULL, 2, &s_sd_log_task);
    }
#endif
}

// DNS Server Task
static void dns_server_task(void *pvParameters) {
    uint8_t data[512];
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(53);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS: Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "DNS: Socket unable to bind: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS Server listening on port 53");

    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    while (1) {
        int len = recvfrom(sock, data, sizeof(data), 0, (struct sockaddr *)&source_addr, &socklen);
        if (len < 0) {
            ESP_LOGE(TAG, "DNS: recvfrom failed: errno %d", errno);
            break;
        }

        // DNS Hijack: Resolve all queries to 192.168.4.1
        if (len > 12) {
            // Keep ID
            // Flags -> Standard Query Response, No Error
            data[2] = 0x81;
            data[3] = 0x80;
            
            // Questions: From Request
            // Answers: 1
            data[6] = 0x00;
            data[7] = 0x01;
            // Auth/Add logic: 0
            data[8] = 0x00; data[9] = 0x00;
            data[10] = 0x00; data[11] = 0x00;

            // Skip QNAME/QTYPE/QCLASS to append Answer
            int idx = 12;
            while (idx < len && data[idx] != 0) {
                idx += data[idx] + 1;
            }
            idx++; // Null byte
            idx += 4; // QTYPE + QCLASS

            // Only append if buffer allows
            if (idx + 16 <= sizeof(data)) {
                // Name: Pointer to header (0xC00C)
                data[idx++] = 0xC0; data[idx++] = 0x0C;
                // Type: A (1)
                data[idx++] = 0x00; data[idx++] = 0x01;
                // Class: IN (1)
                data[idx++] = 0x00; data[idx++] = 0x01;
                // TTL: 60
                data[idx++] = 0x00; data[idx++] = 0x00; data[idx++] = 0x00; data[idx++] = 0x3C;
                // Length: 4
                data[idx++] = 0x00; data[idx++] = 0x04;
                // IP: 192.168.4.1
                data[idx++] = 192; data[idx++] = 168; data[idx++] = 4; data[idx++] = 1;
                
                sendto(sock, data, idx, 0, (struct sockaddr *)&source_addr, socklen);
            }
        }
    }
    close(sock);
    vTaskDelete(NULL);
}

// Handlers Need Forward Declaration or Static implementation
#include <sys/stat.h>

// Forward decl
static esp_err_t static_file_handler(httpd_req_t *req);

/*
static esp_err_t index_handler(httpd_req_t *req) {
   // ...
}
*/

// Helper to determine mime type
static const char* get_mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    if (strcmp(ext, ".json") == 0) return "application/json";
    return "text/plain";
}

// --- API HANDLERS ---

static esp_err_t api_ringtones_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateArray();
    
    DIR *dir = opendir("/sdcard/ringtones");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type != DT_REG) {
                continue;
            }

            const char *name = ent->d_name;
            size_t len = strlen(name);
            if (len < 4) {
                continue;
            }

            const char *ext = name + len - 4;
            if (strcasecmp(ext, ".wav") == 0) {
                cJSON_AddItemToArray(root, cJSON_CreateString(name));
            }
        }
        closedir(dir);
    } else {
        ESP_LOGW(TAG, "Ringtones folder not accessible: /sdcard/ringtones");
    }
    
    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_send(req, json_str, strlen(json_str));
    free((void*)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_preview_handler(httpd_req_t *req) {
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[64];
        if (httpd_query_key_value(buf, "file", param, sizeof(param)) == ESP_OK) {
            // Basic security check: no parent directory traversal
            const char *dot = strrchr(param, '.');
            bool is_wav = dot && strcasecmp(dot, ".wav") == 0;
            if (strstr(param, "..") == NULL && is_wav) {
                // If the user requests a new preview, we allow it immediately.
                // The previous cooldown logic blocked rapid-fire preview switching.
                // We trust the frontend (or debounce there) but allow backend to restart playback.
                
                char filepath[128];
                snprintf(filepath, sizeof(filepath), "/sdcard/ringtones/%s", param);
                
                ESP_LOGI(TAG, "Preview request: %s", filepath);
                
                // play_file handles "stop current -> start new" logic safely
                play_file(filepath); 
                
                httpd_resp_send(req, "OK", 2);
                return ESP_OK;
            }
        }
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

static esp_err_t api_logs_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");

    char lines[LOG_LINE_COUNT][LOG_LINE_MAX];
    size_t count = 0;
    size_t start = 0;

    portENTER_CRITICAL(&s_log_mux);
    count = s_log_count;
    start = (s_log_head + LOG_LINE_COUNT - count) % LOG_LINE_COUNT;
    for (size_t i = 0; i < count; ++i) {
        size_t idx = (start + i) % LOG_LINE_COUNT;
        strncpy(lines[i], s_log_lines[idx], LOG_LINE_MAX - 1);
        lines[i][LOG_LINE_MAX - 1] = '\0';
    }
    portEXIT_CRITICAL(&s_log_mux);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < count; ++i) {
        char compacted[LOG_LINE_MAX];
        compact_log_line(lines[i], compacted, sizeof(compacted));
        cJSON_AddItemToArray(arr, cJSON_CreateString(compacted));
    }
    cJSON_AddItemToObject(root, "lines", arr);

    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_send(req, json_str, strlen(json_str));
    free((void *)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_time_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    
    struct tm now = TimeManager::getCurrentTime();
    char timeStr[16];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &now);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "time", timeStr);
    
    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_send(req, json_str, strlen(json_str));
    free((void *)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_settings_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("dialcharm", NVS_READONLY, &my_handle);
    
    // Default Values
    const char* lang_def = "de";
    // const char* ssid_def = ""; // Unused
    
    char val[65]; size_t len;

    // Language
    len = sizeof(val);
    if (err == ESP_OK && nvs_get_str(my_handle, "src_lang", val, &len) == ESP_OK) {
        cJSON_AddStringToObject(root, "lang", val);
    } else {
        cJSON_AddStringToObject(root, "lang", lang_def);
    }
    
    // WiFi SSID (Redacted for security, or show?)
    len = sizeof(val);
    if (err == ESP_OK && nvs_get_str(my_handle, "wifi_ssid", val, &len) == ESP_OK) {
        cJSON_AddStringToObject(root, "wifi_ssid", val);
    } else {
        cJSON_AddStringToObject(root, "wifi_ssid", "");
    }

    // WiFi Pass (Always return empty or placeholder)
    cJSON_AddStringToObject(root, "wifi_pass", "");

    // System Volume
    // Base Speaker
    uint8_t vol = 60;
    if (err == ESP_OK) nvs_get_u8(my_handle, "volume", &vol);
    cJSON_AddNumberToObject(root, "volume", vol);

    // Handset Volume (New)
    uint8_t vol_h = APP_DEFAULT_HANDSET_VOLUME;
    if (err == ESP_OK) nvs_get_u8(my_handle, "volume_handset", &vol_h);
    cJSON_AddNumberToObject(root, "volume_handset", vol_h);

    // Alarm Volume
    uint8_t vol_a = APP_ALARM_DEFAULT_VOLUME;
    if (err == ESP_OK) nvs_get_u8(my_handle, "vol_alarm", &vol_a);
    cJSON_AddNumberToObject(root, "vol_alarm", vol_a);
    // Send configured minimum to frontend
    cJSON_AddNumberToObject(root, "vol_alarm_min", APP_ALARM_MIN_VOLUME);

    // Snooze Time
    int32_t snooze = APP_SNOOZE_DEFAULT_MINUTES;
    if (err == ESP_OK) nvs_get_i32(my_handle, "snooze_min", &snooze);
    cJSON_AddNumberToObject(root, "snooze_min", snooze);

    // Timer Ringtone
    len = sizeof(val);
    std::string ringtone_name;
    if (err == ESP_OK && nvs_get_str(my_handle, "timer_ringtone", val, &len) == ESP_OK && val[0] != '\0') {
        ringtone_name = val;
    } else {
        ringtone_name = APP_DEFAULT_TIMER_RINGTONE;
    }

    if (!ringtone_name.empty()) {
        char path[128];
        snprintf(path, sizeof(path), "/sdcard/ringtones/%s", ringtone_name.c_str());
        struct stat st;
        if (stat(path, &st) != 0) {
            std::string fallback = get_first_ringtone_name();
            if (!fallback.empty()) {
                ringtone_name = fallback;
            }
        }
    }

    if (ringtone_name.empty()) {
        ringtone_name = APP_DEFAULT_TIMER_RINGTONE;
    }

    cJSON_AddStringToObject(root, "timer_ringtone", ringtone_name.c_str());
    
    // --- Time & Timezone ---
    struct tm now = TimeManager::getCurrentTime();
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &now);
    cJSON_AddStringToObject(root, "current_time", timeStr);
    
    std::string tz = TimeManager::getTimezone();
    if(tz.empty()) tz = "CET-1CEST,M3.5.0,M10.5.0/3"; // Fallback default in UI
    cJSON_AddStringToObject(root, "timezone", tz.c_str());
    // -----------------------

    // --- Alarms ---
    cJSON *alarms = cJSON_CreateArray();
    for (int i=0; i<7; i++) {
        DayAlarm a = TimeManager::getAlarm(i);
        cJSON *itm = cJSON_CreateObject();
        cJSON_AddNumberToObject(itm, "d", i);
        cJSON_AddNumberToObject(itm, "h", a.hour);
        cJSON_AddNumberToObject(itm, "m", a.minute);
        cJSON_AddBoolToObject(itm, "en", a.active);
        cJSON_AddBoolToObject(itm, "rmp", a.volumeRamp);
        cJSON_AddBoolToObject(itm, "msg", a.useRandomMsg);
        cJSON_AddStringToObject(itm, "snd", a.ringtone.c_str());
        cJSON_AddItemToArray(alarms, itm);
    }
    cJSON_AddItemToObject(root, "alarms", alarms);
    // --------------
    
    if (err == ESP_OK) nvs_close(my_handle);

    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_send(req, json_str, strlen(json_str));
    free((void*)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_settings_post_handler(httpd_req_t *req) {
    char *buf = (char *)malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        free(buf);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        free(buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    bool wifi_updated = false;
    bool lang_updated = false;
    nvs_handle_t my_handle;
    if (nvs_open("dialcharm", NVS_READWRITE, &my_handle) == ESP_OK) {
        
        cJSON *item = cJSON_GetObjectItem(root, "lang");
        if (cJSON_IsString(item)) {
            nvs_set_str(my_handle, "src_lang", item->valuestring);
            lang_updated = true;
        }
        
        item = cJSON_GetObjectItem(root, "wifi_ssid");
        if (cJSON_IsString(item) && strlen(item->valuestring) > 0) {
            nvs_set_str(my_handle, "wifi_ssid", item->valuestring);
            wifi_updated = true;
        }
        
        item = cJSON_GetObjectItem(root, "wifi_pass");
        if (cJSON_IsString(item)) {
            nvs_set_str(my_handle, "wifi_pass", item->valuestring);
        }

        item = cJSON_GetObjectItem(root, "volume");
        if (cJSON_IsNumber(item)) {
            nvs_set_u8(my_handle, "volume", (uint8_t)item->valueint);
        }

        item = cJSON_GetObjectItem(root, "volume_handset");
        if (cJSON_IsNumber(item)) {
            nvs_set_u8(my_handle, "volume_handset", (uint8_t)item->valueint);
        }

        item = cJSON_GetObjectItem(root, "vol_alarm");
        if (cJSON_IsNumber(item)) {
            nvs_set_u8(my_handle, "vol_alarm", (uint8_t)item->valueint);
        }

        item = cJSON_GetObjectItem(root, "snooze_min");
        if (cJSON_IsNumber(item)) {
            nvs_set_i32(my_handle, "snooze_min", item->valueint);
        }

        // Timer Ringtone
        item = cJSON_GetObjectItem(root, "timer_ringtone");
        if (cJSON_IsString(item) && strlen(item->valuestring) > 0) {
            nvs_set_str(my_handle, "timer_ringtone", item->valuestring);
            ESP_LOGI(TAG, "Timer ringtone set to: %s", item->valuestring);
        }

        // Timezone configuration
        item = cJSON_GetObjectItem(root, "timezone");
        if (cJSON_IsString(item) && strlen(item->valuestring) > 0) {
            nvs_set_str(my_handle, "time_zone", item->valuestring);
        }

        // --- Alarms ---
        cJSON *alarms_arr = cJSON_GetObjectItem(root, "alarms");
        if (cJSON_IsArray(alarms_arr)) {
            int alarm_updates = 0;
            bool has_day0 = false;
            int day0_h = 0;
            int day0_m = 0;
            bool day0_active = false;
            bool day0_ramp = false;
            bool day0_msg = false;
            std::string day0_ringtone;
            cJSON *elem;
            cJSON_ArrayForEach(elem, alarms_arr) {
                cJSON *d = cJSON_GetObjectItem(elem, "d");
                cJSON *h = cJSON_GetObjectItem(elem, "h");
                cJSON *m = cJSON_GetObjectItem(elem, "m");
                cJSON *en = cJSON_GetObjectItem(elem, "en");
                cJSON *rmp = cJSON_GetObjectItem(elem, "rmp");
                cJSON *msg = cJSON_GetObjectItem(elem, "msg");
                cJSON *snd = cJSON_GetObjectItem(elem, "snd");
                
                if (cJSON_IsNumber(d) && cJSON_IsNumber(h) && cJSON_IsNumber(m)) {
                    bool active = false;
                    bool ramp = false;
                    bool useMsg = false;
                    if (cJSON_IsBool(en)) active = cJSON_IsTrue(en);
                    if (cJSON_IsNumber(en)) active = (en->valueint == 1);
                    
                    if (cJSON_IsBool(rmp)) ramp = cJSON_IsTrue(rmp);
                    if (cJSON_IsNumber(rmp)) ramp = (rmp->valueint == 1);

                    if (cJSON_IsBool(msg)) useMsg = cJSON_IsTrue(msg);
                    if (cJSON_IsNumber(msg)) useMsg = (msg->valueint == 1);

                    const char* ringtone = (snd && cJSON_IsString(snd)) ? snd->valuestring : "";
                    if (d->valueint == 0) {
                        has_day0 = true;
                        day0_h = h->valueint;
                        day0_m = m->valueint;
                        day0_active = active;
                        day0_ramp = ramp;
                        day0_msg = useMsg;
                        day0_ringtone = ringtone ? ringtone : "";
                        continue;
                    }
                    TimeManager::setAlarm(d->valueint, h->valueint, m->valueint, active, ramp, useMsg, ringtone);
                    alarm_updates++;
                }
            }
            if (has_day0) {
                TimeManager::setAlarm(0, day0_h, day0_m, day0_active, day0_ramp, day0_msg, day0_ringtone.c_str());
                alarm_updates++;
            }
            ESP_LOGI(TAG, "Alarm settings saved: %d entries", alarm_updates);
        }
        // --------------


        nvs_commit(my_handle);
        nvs_close(my_handle);
    }

    if (lang_updated) {
        phonebook.reloadDefaults();
    }
    
    // Apply Timezone (read from JSON again to be safe/easy, or variable)
    cJSON *tzItem = cJSON_GetObjectItem(root, "timezone");
    if (cJSON_IsString(tzItem) && strlen(tzItem->valuestring) > 0) {
        TimeManager::setTimezone(tzItem->valuestring);
    }
    
    cJSON_Delete(root);
    free(buf);
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    
    if (wifi_updated) {
        ESP_LOGI(TAG, "WiFi Settings changed. Restarting...");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    
    return ESP_OK;
}

static esp_err_t api_phonebook_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    std::string json = phonebook.getJson();
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

static esp_err_t api_wifi_scan_handler(httpd_req_t *req) {
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = { .min = 100, .max = 300 },
            .passive = 100
        },
        .home_chan_dwell_time = 0,
        .channel_bitmap = { .ghz_2_channels = 0, .ghz_5_channels = 0 },
        .coex_background_scan = false
    };

    // Trigger scan (blocking)
    // Note: In strict AP mode, scan might not work on all ESP32 revisions without APSTA.
    // However, esp_wifi_scan_start usually works.
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi Scan failed: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    // Limit to 20 for memory safety
    if (ap_count > 20) ap_count = 20;

    wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_list) {
         httpd_resp_send_500(req);
         return ESP_FAIL;
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_list);

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < ap_count; i++) {
        // Filter out empty SSIDs
        if (strlen((char*)ap_list[i].ssid) > 0) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "ssid", (char *)ap_list[i].ssid);
            cJSON_AddNumberToObject(item, "rssi", ap_list[i].rssi);
            cJSON_AddNumberToObject(item, "auth", ap_list[i].authmode);
            cJSON_AddItemToArray(root, item);
        }
    }
    free(ap_list);

    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free((void*)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// Embedded Files
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");
extern const char style_css_start[]  asm("_binary_style_css_start");
extern const char style_css_end[]    asm("_binary_style_css_end");
extern const char app_js_start[]     asm("_binary_app_js_start");
extern const char app_js_end[]       asm("_binary_app_js_end");
extern const char font_plaisir_start[] asm("_binary_3620_plaisir_app_otf_start");
extern const char font_plaisir_end[]   asm("_binary_3620_plaisir_app_otf_end");
extern const char font_aatriple_start[] asm("_binary_AATriple_otf_start");
extern const char font_aatriple_end[]   asm("_binary_AATriple_otf_end");

static esp_err_t static_file_handler(httpd_req_t *req) {
    const char* file_path = req->uri;
    const char* file_start = NULL;
    const char* file_end = NULL;
    const char* mime_type = "text/plain";
    bool is_font = false;

    ESP_LOGI(TAG, "Handling URI: %s", file_path);

    // 1. Try Embedded Files (Priority: Flash)
    // SPA Routing: Serve index.html for known client-side routes
    if (strcmp(file_path, "/") == 0 || 
        strcmp(file_path, "/index.html") == 0 ||
        strcmp(file_path, "/alarm") == 0 ||
        strcmp(file_path, "/settings") == 0 ||
        strcmp(file_path, "/phonebook") == 0 ||
        strcmp(file_path, "/advanced") == 0 ||
        strcmp(file_path, "/configuration") == 0 ||
        strcmp(file_path, "/setup") == 0) {
            
        file_start = index_html_start;
        file_end = index_html_end;
        mime_type = "text/html";
    } else if (strcmp(file_path, "/style.css") == 0) {
        file_start = style_css_start;
        file_end = style_css_end;
        mime_type = "text/css";
    } else if (strcmp(file_path, "/app.js") == 0) {
        file_start = app_js_start;
        file_end = app_js_end;
        mime_type = "application/javascript";
    } else if (strncmp(file_path, "/fonts/3620-plaisir-app.otf", 27) == 0) {
        file_start = font_plaisir_start;
        file_end = font_plaisir_end;
        mime_type = "application/font-otf";
        is_font = true;
    } else if (strncmp(file_path, "/fonts/AATriple.otf", 19) == 0) {
        file_start = font_aatriple_start;
        file_end = font_aatriple_end;
        mime_type = "application/font-otf";
        is_font = true;
    } else if (strcmp(file_path, "/favicon.ico") == 0) {

        httpd_resp_send_404(req);
        return ESP_OK;
    } else if (strcmp(file_path, "/hotspot-detect.html") == 0 || 
               strcmp(file_path, "/generate_204") == 0 || 
               strcmp(file_path, "/canonical.html") == 0 ||
               strcmp(file_path, "/ncsi.txt") == 0) {
        // Captive Portal Hijack: Serve index.html
        file_start = index_html_start;
        file_end = index_html_end;
        mime_type = "text/html";
    }

    if (file_start) {
        size_t file_size = file_end - file_start;
        // If EMBED_TXTFILES adds a null terminator, exclude it from serving
        if (!is_font && file_size > 0 && file_start[file_size - 1] == '\0') {
            file_size--;
        }

        ESP_LOGI(TAG, "Serving embedded file: %s (Size: %d)", file_path, (int)file_size);
        if (is_font) {
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        }
        httpd_resp_set_type(req, mime_type);
        httpd_resp_send(req, file_start, file_size);
        return ESP_OK;
    } else {
        // 2. Selective SD Card Serving (Ringtones ONLY)
        // We do NOT serve arbitrary files from /sdcard/data to keep the system clean.
        // Web Interface is fully embedded in Flash.
        
        char filepath[600]; // Increased buffer size to avoid truncation warning
        bool allow_sd = false;

        if (strncmp(file_path, "/ringtones/", 11) == 0) {
            // Check length to prevent overflow (buffer 600, "/sdcard" is 7)
            if (strlen(file_path) < (sizeof(filepath) - 8)) {
                snprintf(filepath, sizeof(filepath), "/sdcard%s", file_path);
                allow_sd = true;
            }
        }

        if (!allow_sd) {
             // Not found in Flash, and not allowed from SD -> 404
             ESP_LOGW(TAG, "404 Not Found (and not in SD whitelist): %s", file_path);
             httpd_resp_send_404(req);
             return ESP_FAIL;
        }
        
        // Serve allowed SD file
        struct stat st;
        if (stat(filepath, &st) == -1) {
            ESP_LOGW(TAG, "SD File allowed but not found: %s", filepath);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        
        FILE* f = fopen(filepath, "r");
        if (!f) {
            ESP_LOGE(TAG, "Failed to open allowed SD file: %s", filepath);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }

        httpd_resp_set_type(req, get_mime_type(filepath));
        
        char sendbuf[1024];
        size_t chunksize;
        while ((chunksize = fread(sendbuf, 1, sizeof(sendbuf), f)) > 0) {
            httpd_resp_send_chunk(req, sendbuf, chunksize);
        }
        fclose(f);
        httpd_resp_send_chunk(req, NULL, 0); 
        return ESP_OK;
    }
}

static esp_err_t status_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    // Simple check: if 192.168.4.1 is our IP, we are likely in AP mode context or just have it enabled
    // Better: use the WebManager instance tracking
    
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    
    bool is_ap = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
    
    char resp[100];
    snprintf(resp, sizeof(resp), "{\"status\":\"ok\", \"platform\":\"esp-idf\", \"mode\":\"%s\"}", is_ap ? "ap" : "sta");
    
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void WebManager::startAPMode() {
    if (s_ap_mode_active) {
        return;
    }
    s_ap_mode_active = true;
    s_wifi_retry_count = 0;
    _apMode = true;
    ESP_LOGI(TAG, "Starting Access Point Mode...");

    // Stop previous WiFi driver actions (if any) to ensure clean AP start
    esp_wifi_disconnect();
    esp_wifi_stop(); 
    esp_wifi_set_mode(WIFI_MODE_APSTA);

    if (s_wifi_event_instance) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_event_instance);
        s_wifi_event_instance = NULL;
    }
    if (s_ip_event_instance) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_event_instance);
        s_ip_event_instance = NULL;
    }

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.ap.ssid, "Dial-A-Charmer");
    wifi_config.ap.ssid_len = strlen("Dial-A-Charmer");
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_config.ap.channel = 1;
    wifi_config.ap.ssid_hidden = 0;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "AP Started. SSID: Dial-A-Charmer, IP: 192.168.4.1");

    // Setup DNS Server for Captive Portal immediately
    setupDnsServer(); 
}

void WebManager::setupWifi() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create Default Netifs
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register Event Handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &s_wifi_event_instance));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &s_ip_event_instance));

    // Load Creds from NVS
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("dialcharm", NVS_READWRITE, &my_handle);
    
    char ssid[33] = {0};
    char pass[65] = {0};
    size_t len = sizeof(ssid);
    
    if (err == ESP_OK) {
        nvs_get_str(my_handle, "wifi_ssid", ssid, &len);
        len = sizeof(pass);
        nvs_get_str(my_handle, "wifi_pass", pass, &len);
        nvs_close(my_handle);
    }

    if (strlen(ssid) > 0) {
        ESP_LOGI(TAG, "Configuring WiFi STA for: %s", ssid);
        wifi_config_t wifi_config = {};
        strcpy((char*)wifi_config.sta.ssid, ssid);
        strcpy((char*)wifi_config.sta.password, pass);
        
        // Basic security threshold (optional)
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        
        // Enable Power Save Mode (DTIM sleep)
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

        // Start WiFi - WIFI_EVENT_STA_START will trigger connect
        ESP_ERROR_CHECK(esp_wifi_start());
    } else {
        ESP_LOGI(TAG, "No WiFi credentials found.");
        startAPMode();
    }
}

void WebManager::setupMdns() {
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("dial-a-charmer"));
    ESP_ERROR_CHECK(mdns_instance_name_set("Dial-A-Charmer Web Interface"));
    
    // Add HTTP service
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS started: http://dial-a-charmer.local");
}

void WebManager::setupWebServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 8192; // More stack for file handling
    config.uri_match_fn = httpd_uri_match_wildcard; // Enable wildcard matching

    ESP_LOGI(TAG, "Starting Web Server...");
    if (httpd_start(&server, &config) == ESP_OK) {
        // API Handlers (Specific)
        httpd_uri_t status_uri = {
            .uri       = "/api/status",
            .method    = HTTP_GET,
            .handler   = status_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &status_uri);

        httpd_uri_t wifi_scan_uri = {
            .uri       = "/api/wifi/scan",
            .method    = HTTP_GET,
            .handler   = api_wifi_scan_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &wifi_scan_uri);

        httpd_uri_t set_get_uri = {
            .uri       = "/api/settings",
            .method    = HTTP_GET,
            .handler   = api_settings_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &set_get_uri);

        httpd_uri_t set_post_uri = {
            .uri       = "/api/settings",
            .method    = HTTP_POST,
            .handler   = api_settings_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &set_post_uri);

        httpd_uri_t ringtones_uri = {
            .uri       = "/api/ringtones",
            .method    = HTTP_GET,
            .handler   = api_ringtones_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &ringtones_uri);

        httpd_uri_t preview_uri = {
            .uri       = "/api/preview",
            .method    = HTTP_GET,
            .handler   = api_preview_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &preview_uri);

        httpd_uri_t pb_get_uri = {
            .uri       = "/api/phonebook",
            .method    = HTTP_GET,
            .handler   = api_phonebook_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &pb_get_uri);

        httpd_uri_t logs_uri = {
            .uri       = "/api/logs",
            .method    = HTTP_GET,
            .handler   = api_logs_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &logs_uri);

        httpd_uri_t time_uri = {
            .uri       = "/api/time",
            .method    = HTTP_GET,
            .handler   = api_time_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &time_uri);

        // Files (Catch-all)
        // We register this last or use a greedy match
        httpd_uri_t file_uri = {
            .uri       = "/*", // Matches anything not handled above
            .method    = HTTP_GET,
            .handler   = static_file_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &file_uri);
    }
}

void WebManager::setupDnsServer() {
    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, NULL);
}

void WebManager::begin() {
    init_log_capture();
    setupWifi();
    setupMdns();
    setupWebServer();
}

void WebManager::loop() {
    // mDNS handles itself
}
