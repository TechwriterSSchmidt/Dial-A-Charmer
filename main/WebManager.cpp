#include "WebManager.h"
#include "TimeManager.h" // Added TimeManager
#include <string.h>
#include <sys/param.h>
#include <dirent.h>
#include <sys/types.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
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

        // Simple DNS Hijack: Respond with 192.168.4.1 to everything
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

static esp_err_t api_phonebook_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    std::string json = phonebook.getJson();
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

static esp_err_t api_phonebook_post_handler(httpd_req_t *req) {
    char *buf = (char *)malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        free(buf);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    ESP_LOGI(TAG, "Saving Phonebook JSON: %s", buf);
    phonebook.saveFromJson(std::string(buf));
    free(buf);
    
    httpd_resp_send(req, "{\"status\":\"saved\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_ringtones_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateArray();
    
    DIR *dir = opendir("/sdcard/ringtones");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
	    // Filter for common audio files
            if (strstr(ent->d_name, ".wav") || strstr(ent->d_name, ".mp3")) {
                cJSON_AddItemToArray(root, cJSON_CreateString(ent->d_name));
            }
        }
        closedir(dir);
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
            if (strstr(param, "..") == NULL && (strstr(param, ".wav") || strstr(param, ".mp3"))) {
                char filepath[128];
                snprintf(filepath, sizeof(filepath), "/sdcard/ringtones/%s", param);
                
                ESP_LOGI(TAG, "Preview request: %s", filepath);
                play_file(filepath); 
                
                httpd_resp_send(req, "OK", 2);
                return ESP_OK;
            }
        }
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
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
    uint8_t vol_h = 60;
    if (err == ESP_OK) nvs_get_u8(my_handle, "volume_handset", &vol_h);
    cJSON_AddNumberToObject(root, "volume_handset", vol_h);


    // Snooze Time
    int32_t snooze = 5;
    if (err == ESP_OK) nvs_get_i32(my_handle, "snooze_min", &snooze);
    cJSON_AddNumberToObject(root, "snooze_min", snooze);
    
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
    nvs_handle_t my_handle;
    if (nvs_open("dialcharm", NVS_READWRITE, &my_handle) == ESP_OK) {
        
        cJSON *item = cJSON_GetObjectItem(root, "lang");
        if (cJSON_IsString(item)) {
            nvs_set_str(my_handle, "src_lang", item->valuestring);
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

        item = cJSON_GetObjectItem(root, "snooze_min");
        if (cJSON_IsNumber(item)) {
            nvs_set_i32(my_handle, "snooze_min", item->valueint);
        }

        // Timezone
        item = cJSON_GetObjectItem(root, "timezone");
        if (cJSON_IsString(item) && strlen(item->valuestring) > 0) {
            // Save inside TimeManager (it handles NVS internally too, but distinct key/handle)
            // But we can just call it here. 
            // Note: Since TimeManager opens NVS "dialcharm" too, we must ensure handles don't conflict 
            // or just rely on TimeManager's logic.
            // Since we are holding 'my_handle' open here, checking concurrency... 
            // NVS single partition open is fine usually, but cleaner to close first?
            // Actually TimeManager uses its own nvs_open/close.
            // To avoid "NVS_ERR_NVS_PART_ALREADY_OPEN" or similar if logic restricted, 
            // we should probably just save it via TimeManager AFTER closing here, or just save key here manually if we know it.
            // TimeManager uses key "time_zone". Let's save it directly to 'my_handle' to avoid overhead!
            
            nvs_set_str(my_handle, "time_zone", item->valuestring);
            
            // Also update runtime - warning: this updates env var so we need to do it.
            // But we can't do it easily inside this NVS block for TimeManager state. 
            // Better: Let's extract the string and call TimeManager::setTimezone() *after* this block.
        }

        // --- Alarms ---
        cJSON *alarms_arr = cJSON_GetObjectItem(root, "alarms");
        if (cJSON_IsArray(alarms_arr)) {
            cJSON *elem;
            cJSON_ArrayForEach(elem, alarms_arr) {
                cJSON *d = cJSON_GetObjectItem(elem, "d");
                cJSON *h = cJSON_GetObjectItem(elem, "h");
                cJSON *m = cJSON_GetObjectItem(elem, "m");
                cJSON *en = cJSON_GetObjectItem(elem, "en");
                cJSON *rmp = cJSON_GetObjectItem(elem, "rmp");
                cJSON *snd = cJSON_GetObjectItem(elem, "snd");
                
                if (cJSON_IsNumber(d) && cJSON_IsNumber(h) && cJSON_IsNumber(m)) {
                    bool active = false;
                    bool ramp = false;
                    if (cJSON_IsBool(en)) active = cJSON_IsTrue(en);
                    if (cJSON_IsNumber(en)) active = (en->valueint == 1);
                    
                    if (cJSON_IsBool(rmp)) ramp = cJSON_IsTrue(rmp);
                    if (cJSON_IsNumber(rmp)) ramp = (rmp->valueint == 1);

                    const char* ringtone = (snd && cJSON_IsString(snd)) ? snd->valuestring : "";
                    TimeManager::setAlarm(d->valueint, h->valueint, m->valueint, active, ramp, ringtone);
                }
            }
        }
        // --------------


        nvs_commit(my_handle);
        nvs_close(my_handle);
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
        }
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

static esp_err_t static_file_handler(httpd_req_t *req) {
    const char* file_path = req->uri;
    const char* file_start = NULL;
    const char* file_end = NULL;
    const char* mime_type = "text/plain";

    ESP_LOGI(TAG, "Handling URI: %s", file_path);

    // Map URIs to embedded files
    // SPA Routing: Serve index.html for known client-side routes
    if (strcmp(file_path, "/") == 0 || 
        strcmp(file_path, "/index.html") == 0 ||
        strcmp(file_path, "/alarm") == 0 ||
        strcmp(file_path, "/settings") == 0 ||
        strcmp(file_path, "/phonebook") == 0 ||
        strcmp(file_path, "/advanced") == 0 ||
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
        mime_type = "text/html";    }

    if (file_start) {
        size_t file_size = file_end - file_start;
        // If EMBED_TXTFILES adds a null terminator, exclude it from serving
        if (file_size > 0 && file_start[file_size - 1] == '\0') {
            file_size--;
        }

        ESP_LOGI(TAG, "Serving embedded file: %s (Size: %d)", file_path, (int)file_size);
        httpd_resp_set_type(req, mime_type);
        httpd_resp_send(req, file_start, file_size);
        return ESP_OK;
    } else {
        // Fallback to SD Card
        char filepath[HTTPD_MAX_URI_LEN + 64];
        
        // SPECIAL ROUTING FOR FONTS
        // If URI starts with /fonts/, map to /sdcard/fonts/
        if (strncmp(file_path, "/fonts/", 7) == 0) {
            snprintf(filepath, sizeof(filepath), "/sdcard%s", file_path);
        } else if (strncmp(file_path, "/ringtones/", 11) == 0) {
            snprintf(filepath, sizeof(filepath), "/sdcard%s", file_path);
        } else {
            // Default: map to /sdcard/data/
            snprintf(filepath, sizeof(filepath), "/sdcard/data%s", file_path);
        }
        
        struct stat st;
        if (stat(filepath, &st) == -1) {
            ESP_LOGW(TAG, "File not found: %s", filepath);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        
        FILE* f = fopen(filepath, "r");
        if (!f) {
            ESP_LOGE(TAG, "Failed to open: %s", filepath);
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

void WebManager::setupWifi() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create Default Netifs
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

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
        ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);
        wifi_config_t wifi_config = {};
        strcpy((char*)wifi_config.sta.ssid, ssid);
        strcpy((char*)wifi_config.sta.password, pass);
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());
        
        // Wait for connection (Blocking for simplicity in this phase, or use event group)
        // ideally we don't block main loop long. 
        // For now, let's just start it. If it fails, we need event logic to fallback to AP.
    } else {
        ESP_LOGI(TAG, "No WiFi credentials. Starting AP Mode (APSTA for scanning).");
        _apMode = true;
        
        wifi_config_t wifi_config = {};
        strcpy((char*)wifi_config.ap.ssid, "Dial-A-Charmer");
        wifi_config.ap.ssid_len = strlen("Dial-A-Charmer");
        wifi_config.ap.max_connection = 4;
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        
        ESP_LOGI(TAG, "AP Started. SSID: Dial-A-Charmer, IP: 192.168.4.1");
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

        httpd_uri_t pb_get_uri = {
            .uri       = "/api/phonebook",
            .method    = HTTP_GET,
            .handler   = api_phonebook_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &pb_get_uri);

        httpd_uri_t pb_post_uri = {
            .uri       = "/api/phonebook",
            .method    = HTTP_POST,
            .handler   = api_phonebook_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &pb_post_uri);

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
    setupWifi();
    setupMdns();
    setupWebServer();
    if (_apMode) {
        setupDnsServer();
    }
}

void WebManager::loop() {
    // mDNS handles itself
}
