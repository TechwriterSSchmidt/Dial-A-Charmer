#include <string.h>
#include <string>
#include <vector>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "board.h"
#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
// #include "driver/i2s.h" // Simplified Includes
#include "wav_decoder.h"

// App Includes
#include "app_config.h"
#include "led_strip.h" 
#include "RotaryDial.h"
#include "PhonebookManager.h"
#include "WebManager.h"
#include "TimeManager.h" 

static const char *TAG = "DIAL_A_CHARMER_ESP";

// Global Objects
RotaryDial dial(APP_PIN_DIAL_PULSE, APP_PIN_HOOK, APP_PIN_EXTRA_BTN, APP_PIN_DIAL_MODE);
audio_pipeline_handle_t pipeline;
audio_element_handle_t fatfs_stream, wav_decoder, i2s_writer, gain_element;
audio_board_handle_t board_handle = NULL; 
led_strip_handle_t g_led_strip = NULL;
SemaphoreHandle_t g_audio_mutex = NULL;
SemaphoreHandle_t g_led_mutex = NULL;

// Logic State
std::string dial_buffer = "";
int64_t last_digit_time = 0;
const int DIAL_TIMEOUT_MS = APP_DIAL_TIMEOUT_MS;
const int PERSONA_PAUSE_MS = APP_PERSONA_PAUSE_MS;
const int DIALTONE_SILENCE_MS = APP_DIALTONE_SILENCE_MS;
bool is_playing = false;
bool g_output_mode_handset = false; 
bool g_off_hook = false;
bool g_line_busy = false;
bool g_persona_playback_active = false;
bool g_any_digit_dialed = false;
int64_t g_off_hook_start_ms = 0;
int64_t g_last_playback_finished_ms = 0;
bool g_last_playback_was_dialtone = false;
bool g_timer_active = false;
int g_timer_minutes = 0;
int64_t g_timer_end_ms = 0;
bool g_timer_announce_pending = false;
bool g_timer_intro_playing = false;
int g_timer_announce_minutes = 0;
std::string g_current_alarm_file = "";
bool g_startup_silence_playing = false;
bool g_voice_menu_active = false;
int64_t g_voice_menu_started_ms = 0;
std::vector<std::string> g_voice_queue;
bool g_voice_queue_active = false;
bool g_night_mode_active = false;
int64_t g_night_mode_end_ms = 0;
uint8_t g_led_r = 50;
uint8_t g_led_g = 20;
uint8_t g_led_b = 0;
uint8_t g_night_prev_r = 50;
uint8_t g_night_prev_g = 20;
uint8_t g_night_prev_b = 0;
bool g_led_booting = true;
bool g_snooze_active = false;
bool g_sd_error = false;
bool g_force_base_output = false;
// Alarm State
int g_saved_volume = -1;

enum AlarmSource {
    ALARM_NONE = 0,
    ALARM_TIMER,
    ALARM_DAILY,
};

static bool g_alarm_active = false;
static AlarmSource g_alarm_source = ALARM_NONE;
static int64_t g_alarm_end_ms = 0;
static int64_t g_alarm_retry_last_ms = 0;
// Alarm Fade Logic
static bool g_alarm_fade_active = false;
static float g_alarm_fade_factor = 1.0f;
static int64_t g_alarm_fade_start_time = 0;

#if APP_ENABLE_TASK_WDT
static bool g_wdt_main_registered = false;
static bool g_wdt_input_registered = false;
#endif

// Helper Forward Declarations
static void start_voice_queue(const std::vector<std::string> &files);
void play_file(const char* path);
void update_audio_output();

static bool is_lang_en() {
    nvs_handle_t my_handle;
    char val[8] = {0};
    size_t len = sizeof(val);
    bool is_en = false;

    if (nvs_open("dialcharm", NVS_READONLY, &my_handle) == ESP_OK) {
        if (nvs_get_str(my_handle, "src_lang", val, &len) == ESP_OK) {
            is_en = (strncmp(val, "en", 2) == 0);
        }
        nvs_close(my_handle);
    }
    return is_en;
}

static const char *lang_code() {
    return is_lang_en() ? "en" : "de";
}

static std::string system_path(const char *base) {
    return std::string("/sdcard/system/") + base + "_" + lang_code() + ".wav";
}

static std::string time_path(const char *name) {
    return std::string("/sdcard/time/") + lang_code() + "/" + name;
}

static void announce_time_now() {
    struct tm now = TimeManager::getCurrentTimeRtc();
    if (now.tm_year < 120) {
        start_voice_queue({system_path("time_unavailable")});
        return;
    }

    std::vector<std::string> files;
    files.push_back(time_path("intro.wav"));

    char buf[32];
    snprintf(buf, sizeof(buf), "h_%d.wav", now.tm_hour);
    files.push_back(time_path(buf));

    snprintf(buf, sizeof(buf), "m_%02d.wav", now.tm_min);
    files.push_back(time_path(buf));

    if (!is_lang_en()) {
        files.push_back(time_path("uhr.wav"));
    }

    start_voice_queue(files);
}

static void set_led_color(uint8_t r, uint8_t g, uint8_t b) {
    g_led_r = r;
    g_led_g = g;
    g_led_b = b;
    if (g_led_strip) {
        if (g_led_mutex) {
            xSemaphoreTake(g_led_mutex, portMAX_DELAY);
        }
        led_strip_set_pixel(g_led_strip, 0, r, g, b);
        led_strip_refresh(g_led_strip);
        if (g_led_mutex) {
            xSemaphoreGive(g_led_mutex);
        }
    }
}

enum LedState {
    LED_BOOTING,
    LED_IDLE,
    LED_ALARM,
    LED_TIMER,
    LED_SNOOZE,
    LED_ERROR,
};

static void apply_led_color(uint8_t r, uint8_t g, uint8_t b) {
    if (g_night_mode_active) {
        r = (uint8_t)((r * APP_NIGHTMODE_LED_PERCENT) / 100);
        g = (uint8_t)((g * APP_NIGHTMODE_LED_PERCENT) / 100);
        b = (uint8_t)((b * APP_NIGHTMODE_LED_PERCENT) / 100);
    }
    set_led_color(r, g, b);
}

static LedState get_led_state() {
    if (g_led_booting) return LED_BOOTING;
    if (g_sd_error) return LED_ERROR;
    if (g_alarm_active) return (g_alarm_source == ALARM_DAILY) ? LED_ALARM : LED_TIMER;
    if (g_snooze_active) return LED_SNOOZE;
    return LED_IDLE;
}

static void led_task(void *pvParameters) {
    (void)pvParameters;
    int step = 0;
    int error_phase = 0;
    int error_ticks = 0;
    const int tick_ms = 50;

    while (true) {
        LedState state = get_led_state();

        if (state == LED_BOOTING) {
            // Pulse between blue and gold
            int phase = step % 200;
            int t = (phase <= 100) ? phase : (200 - phase);
            uint8_t r = (uint8_t)((80 * t) / 100);
            uint8_t g = (uint8_t)((40 * t) / 100);
            uint8_t b = (uint8_t)(120 - ((100 * t) / 100));
            apply_led_color(r, g, b);
            step += 2;
        } else if (state == LED_IDLE) {
            // Vintage orange glow with subtle flicker
            uint8_t r = 50;
            uint8_t g = 20;
            uint8_t b = 0;
            if ((step % 5) == 0) {
                int8_t flicker = (int8_t)((esp_random() % 5) - 2);
                int rr = r + flicker;
                int gg = g + (flicker / 2);
                if (rr < 0) rr = 0;
                if (gg < 0) gg = 0;
                r = (uint8_t)rr;
                g = (uint8_t)gg;
            }
            apply_led_color(r, g, b);
            step++;
        } else if (state == LED_ALARM) {
            // Warm white pulsing
            int phase = step % 200;
            int t = (phase <= 100) ? phase : (200 - phase);
            uint8_t intensity = (uint8_t)(30 + ((70 * t) / 100));
            uint8_t r = (uint8_t)((255 * intensity) / 100);
            uint8_t g = (uint8_t)((220 * intensity) / 100);
            uint8_t b = (uint8_t)((180 * intensity) / 100);
            apply_led_color(r, g, b);
            step += 3;
        } else if (state == LED_TIMER) {
            // Fast red pulsing
            int phase = step % 100;
            int t = (phase <= 50) ? phase : (100 - phase);
            uint8_t intensity = (uint8_t)(15 + ((85 * t) / 50));
            uint8_t r = (uint8_t)((255 * intensity) / 100);
            apply_led_color(r, 0, 0);
            step += 5;
        } else if (state == LED_SNOOZE) {
            // Warm white solid
            apply_led_color(200, 170, 130);
            step++;
        } else {
            // SOS pattern in red
            static const int pattern_ms[] = {200, 200, 200, 200, 200, 400, 600, 200, 600, 200, 600, 400, 200, 200, 200, 200, 200, 1000};
            static const bool pattern_on[] = {true, false, true, false, true, false, true, false, true, false, true, false, true, false, true, false, true, false};
            const int pattern_len = (int)(sizeof(pattern_ms) / sizeof(pattern_ms[0]));

            if (error_ticks <= 0) {
                error_phase = (error_phase + 1) % pattern_len;
                error_ticks = pattern_ms[error_phase] / tick_ms;
                if (error_ticks <= 0) error_ticks = 1;
            }

            if (pattern_on[error_phase]) {
                apply_led_color(255, 0, 0);
            } else {
                apply_led_color(0, 0, 0);
            }
            error_ticks--;
        }

        vTaskDelay(pdMS_TO_TICKS(tick_ms));
    }
}

static void set_night_mode(bool enable) {
    if (enable) {
        g_night_mode_active = true;
        g_night_mode_end_ms = (esp_timer_get_time() / 1000) +
            (int64_t)APP_NIGHTMODE_DURATION_HOURS * 60 * 60 * 1000;
        g_night_prev_r = g_led_r;
        g_night_prev_g = g_led_g;
        g_night_prev_b = g_led_b;
        uint8_t r = (uint8_t)((g_night_prev_r * APP_NIGHTMODE_LED_PERCENT) / 100);
        uint8_t g = (uint8_t)((g_night_prev_g * APP_NIGHTMODE_LED_PERCENT) / 100);
        uint8_t b = (uint8_t)((g_night_prev_b * APP_NIGHTMODE_LED_PERCENT) / 100);
        set_led_color(r, g, b);
    } else {
        g_night_mode_active = false;
        set_led_color(g_night_prev_r, g_night_prev_g, g_night_prev_b);
    }
    update_audio_output();
}

static void start_voice_queue(const std::vector<std::string> &files) {
    if (files.empty()) return;
    g_voice_queue = files;
    g_voice_queue_active = true;
    play_file(g_voice_queue.front().c_str());
    g_voice_queue.erase(g_voice_queue.begin());
}

static bool play_next_in_queue() {
    if (g_voice_queue_active && !g_voice_queue.empty()) {
        play_file(g_voice_queue.front().c_str());
        g_voice_queue.erase(g_voice_queue.begin());
        return true;
    }
    if (g_voice_queue_active) {
        g_voice_queue_active = false;
    }
    return false;
}

static bool get_next_alarm(struct tm *out_tm, DayAlarm *out_alarm) {
    if (!out_tm || !out_alarm) return false;
    time_t now_t;
    time(&now_t);
    struct tm now;
    localtime_r(&now_t, &now);

    for (int i = 0; i < 7; ++i) {
        int day = (now.tm_wday + i) % 7;
        DayAlarm a = TimeManager::getAlarm(day);
        if (!a.active) continue;

        time_t day_t = now_t + (int64_t)i * 24 * 60 * 60;
        struct tm t;
        localtime_r(&day_t, &t);
        t.tm_hour = a.hour;
        t.tm_min = a.minute;
        t.tm_sec = 0;

        time_t candidate = mktime(&t);
        if (candidate <= now_t) continue;

        *out_tm = t;
        *out_alarm = a;
        return true;
    }
    return false;
}

static void add_number_audio(std::vector<std::string> &files, int value) {
    if (value < 0) value = -value;
    if (value > 300) value = 300;
    char buf[32];
    snprintf(buf, sizeof(buf), "m_%02d.wav", value);
    files.push_back(time_path(buf));
}

static void handle_voice_menu_digit(int digit) {
    if (digit == 1) {
        struct tm next_tm;
        DayAlarm next_alarm;
        if (!get_next_alarm(&next_tm, &next_alarm)) {
            start_voice_queue({system_path("next_alarm_none")});
            return;
        }

        std::vector<std::string> files;
        files.push_back(system_path("next_alarm"));

        char buf[32];
        snprintf(buf, sizeof(buf), "wday_%d.wav", next_tm.tm_wday);
        files.push_back(time_path(buf));

        snprintf(buf, sizeof(buf), "h_%d.wav", next_tm.tm_hour);
        files.push_back(time_path(buf));

        snprintf(buf, sizeof(buf), "m_%02d.wav", next_tm.tm_min);
        files.push_back(time_path(buf));

        if (!is_lang_en()) {
            files.push_back(time_path("uhr.wav"));
        }

        start_voice_queue(files);
    } else if (digit == 2) {
        if (g_night_mode_active) {
            set_night_mode(false);
            start_voice_queue({system_path("night_off")});
        } else {
            set_night_mode(true);
            start_voice_queue({system_path("night_on")});
        }
    } else if (digit == 3) {
        std::vector<std::string> files;
        // files.push_back(system_path("phonebook_export")); // Removed as we are now just announcing
        files.push_back(system_path("pb_persona1"));
        files.push_back(system_path("pb_persona2"));
        files.push_back(system_path("pb_persona3"));
        files.push_back(system_path("pb_persona4"));
        files.push_back(system_path("pb_persona5"));
        files.push_back(system_path("pb_random_mix"));
        files.push_back(system_path("pb_time"));
        files.push_back(system_path("pb_menu"));
        files.push_back(system_path("pb_reboot"));
        start_voice_queue(files);
    } else if (digit == 4) {
        std::vector<std::string> files;
        files.push_back(system_path("system_check"));

        esp_netif_ip_info_t ip_info = {};
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            uint32_t ip = ip_info.ip.addr;
            files.push_back(system_path("ip"));
            add_number_audio(files, (ip) & 0xFF);
            files.push_back(system_path("dot"));
            add_number_audio(files, (ip >> 8) & 0xFF);
            files.push_back(system_path("dot"));
            add_number_audio(files, (ip >> 16) & 0xFF);
            files.push_back(system_path("dot"));
            add_number_audio(files, (ip >> 24) & 0xFF);
        }

        wifi_ap_record_t ap_info = {};
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            files.push_back(system_path("wifi"));
            add_number_audio(files, ap_info.rssi);
            files.push_back(system_path("dbm"));
        }

        time_t last_sync = TimeManager::getLastNtpSync();
        if (last_sync > 0) {
            time_t now;
            time(&now);
            int mins = (int)((now - last_sync) / 60);
            files.push_back(system_path("ntp"));
            add_number_audio(files, mins);
            files.push_back(system_path("minutes"));
        } else {
            files.push_back(system_path("time_unavailable"));
        }

        uint64_t total_bytes = 0;
        uint64_t free_bytes = 0;
        if (esp_vfs_fat_info("/sdcard", &total_bytes, &free_bytes) == ESP_OK) {
            uint32_t free_mb = (uint32_t)(free_bytes / (1024 * 1024));
            files.push_back(system_path("sd_free"));
            add_number_audio(files, (int)free_mb);
            files.push_back(system_path("mb"));
            files.push_back(system_path("sd_ok"));
        } else {
            files.push_back(system_path("sd_missing"));
        }

        start_voice_queue(files);
    } else {
        start_voice_queue({system_path("error_msg")});
    }
}

// Helpers
void stop_playback(); // forward decl
void play_file(const char* path); // forward decl
static void audio_lock() {
    if (g_audio_mutex) {
        xSemaphoreTake(g_audio_mutex, portMAX_DELAY);
    }
}

static void audio_unlock() {
    if (g_audio_mutex) {
        xSemaphoreGive(g_audio_mutex);
    }
}

static void restore_volume_after_alarm() {
    if (g_saved_volume != -1 && board_handle && board_handle->audio_hal) {
         ESP_LOGI(TAG, "Restoring volume to %d", g_saved_volume);
         audio_hal_set_volume(board_handle->audio_hal, g_saved_volume);
         g_saved_volume = -1;
    }
}

static void reset_alarm_state(bool restore_volume) {
    g_alarm_active = false;
    g_alarm_source = ALARM_NONE;
    g_alarm_fade_active = false;
    g_alarm_fade_factor = 1.0f;
    if (restore_volume) {
        restore_volume_after_alarm();
    }
}

static void force_alarm_volume() {
    if (board_handle && board_handle->audio_hal) {
        int vol = 0;
        audio_hal_get_volume(board_handle->audio_hal, &vol);
        if (g_saved_volume == -1) {
             g_saved_volume = vol;
        }

        // Load Alarm Volume from NVS
        uint8_t target_vol = APP_ALARM_DEFAULT_VOLUME;
        nvs_handle_t my_handle;
        if (nvs_open("dialcharm", NVS_READONLY, &my_handle) == ESP_OK) {
            nvs_get_u8(my_handle, "vol_alarm", &target_vol);
            nvs_close(my_handle);
        }

        // Enforce Minimum
        if (target_vol < APP_ALARM_MIN_VOLUME) {
            target_vol = APP_ALARM_MIN_VOLUME;
        }

        ESP_LOGI(TAG, "Forcing Alarm Volume: %d (Saved: %d)", target_vol, g_saved_volume);
        audio_hal_set_volume(board_handle->audio_hal, target_vol);
    }
}

static void start_timer_minutes(int minutes) {
    // Override any existing timer or pending timer announcements
    g_timer_active = true;
    g_timer_minutes = minutes;
    g_timer_end_ms = (esp_timer_get_time() / 1000) + (int64_t)minutes * 60 * 1000;
    g_timer_announce_pending = false;
    g_timer_intro_playing = false;
}

static std::string get_first_ringtone_path() {
    const char *dir_path = "/sdcard/ringtones";
    DIR *dir = opendir(dir_path);
    if (!dir) return "";

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len > 4 && strcasecmp(name + len - 4, ".wav") == 0) {
            std::string path = std::string(dir_path) + "/" + name;
            closedir(dir);
            return path;
        }
    }
    closedir(dir);
    return "";
}

static std::string get_timer_ringtone_path() {
    nvs_handle_t my_handle;
    char val[64] = {0};
    size_t len = sizeof(val);

    if (nvs_open("dialcharm", NVS_READONLY, &my_handle) == ESP_OK) {
        if (nvs_get_str(my_handle, "timer_ringtone", val, &len) != ESP_OK) {
            val[0] = '\0';
        }
        nvs_close(my_handle);
    }

    if (val[0] != '\0') {
        std::string path = std::string("/sdcard/ringtones/") + val;
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            return path;
        }
        ESP_LOGW(TAG, "Timer ringtone missing, falling back: %s", path.c_str());
    }

    std::string fallback = get_first_ringtone_path();
    if (!fallback.empty()) {
        ESP_LOGI(TAG, "Using first ringtone fallback: %s", fallback.c_str());
        return fallback;
    }

    return std::string("/sdcard/ringtones/") + (val[0] != '\0' ? val : APP_DEFAULT_TIMER_RINGTONE);
}

static void announce_timer_minutes(int minutes) {
    char path[128];
    if (minutes < 10) {
        snprintf(path, sizeof(path), "m_0%d.wav", minutes);
    } else {
        snprintf(path, sizeof(path), "m_%d.wav", minutes);
    }
    std::vector<std::string> files;
    files.push_back(time_path(path));
    files.push_back(system_path("minutes"));
    start_voice_queue(files);
}

// Software gain (no register writes) with soft ramp to reduce clicks
static float g_gain_left_target = APP_GAIN_DEFAULT_LEFT;
static float g_gain_right_target = APP_GAIN_DEFAULT_RIGHT;
static float g_gain_left_cur = 0.0f;
static float g_gain_right_cur = 0.0f;
static int g_gain_ramp_ms = APP_GAIN_RAMP_MS;
static int g_gain_sample_rate = 44100;
static uint8_t *g_stereo_buf = NULL;
static size_t g_stereo_buf_size = 0;
static volatile bool g_key3_pressed = false;

static int gain_open(audio_element_handle_t self)
{
    return ESP_OK;
}

static int gain_close(audio_element_handle_t self)
{
    return ESP_OK;
}

static audio_element_err_t gain_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
    int r = audio_element_input(self, in_buffer, in_len);
    if (r <= 0) {
        return (audio_element_err_t)r;
    }

    audio_element_info_t info = {};
    audio_element_getinfo(self, &info);
    int channels = info.channels > 0 ? info.channels : 2;
    if (info.sample_rates > 0) {
        g_gain_sample_rate = info.sample_rates;
    }

    int16_t *samples = (int16_t *)in_buffer;
    int sample_count = r / sizeof(int16_t);

    int ramp_samples = (g_gain_sample_rate * g_gain_ramp_ms) / 1000;
    if (ramp_samples < 1) {
        ramp_samples = 1;
    }
    float step_l = (g_gain_left_target - g_gain_left_cur) / (float)ramp_samples;
    float step_r = (g_gain_right_target - g_gain_right_cur) / (float)ramp_samples;

    if (channels == 2) {
        for (int i = 0; i + 1 < sample_count; i += 2) {
            g_gain_left_cur += step_l;
            g_gain_right_cur += step_r;
            if ((step_l > 0.0f && g_gain_left_cur > g_gain_left_target) || (step_l < 0.0f && g_gain_left_cur < g_gain_left_target)) {
                g_gain_left_cur = g_gain_left_target;
            }
            if ((step_r > 0.0f && g_gain_right_cur > g_gain_right_target) || (step_r < 0.0f && g_gain_right_cur < g_gain_right_target)) {
                g_gain_right_cur = g_gain_right_target;
            }
            float l = (float)samples[i] * g_gain_left_cur;
            float rch = (float)samples[i + 1] * g_gain_right_cur;
            if (l > 32767.0f) l = 32767.0f;
            if (l < -32768.0f) l = -32768.0f;
            if (rch > 32767.0f) rch = 32767.0f;
            if (rch < -32768.0f) rch = -32768.0f;
            samples[i] = (int16_t)l;
            samples[i + 1] = (int16_t)rch;
        }
    } else {
        // Mono -> duplicate to stereo with per-channel gain
        size_t needed = r * 2;
        if (g_stereo_buf_size < needed) {
            free(g_stereo_buf);
            g_stereo_buf = (uint8_t *)malloc(needed);
            g_stereo_buf_size = g_stereo_buf ? needed : 0;
        }
        if (!g_stereo_buf) {
            return (audio_element_err_t)-1;
        }

        int16_t *out = (int16_t *)g_stereo_buf;
        for (int i = 0, o = 0; i < sample_count; ++i, o += 2) {
            g_gain_left_cur += step_l;
            g_gain_right_cur += step_r;
            if ((step_l > 0.0f && g_gain_left_cur > g_gain_left_target) || (step_l < 0.0f && g_gain_left_cur < g_gain_left_target)) {
                g_gain_left_cur = g_gain_left_target;
            }
            if ((step_r > 0.0f && g_gain_right_cur > g_gain_right_target) || (step_r < 0.0f && g_gain_right_cur < g_gain_right_target)) {
                g_gain_right_cur = g_gain_right_target;
            }
            float v = (float)samples[i];
            float l = v * g_gain_left_cur;
            float rch = v * g_gain_right_cur;
            if (l > 32767.0f) l = 32767.0f;
            if (l < -32768.0f) l = -32768.0f;
            if (rch > 32767.0f) rch = 32767.0f;
            if (rch < -32768.0f) rch = -32768.0f;
            out[o] = (int16_t)l;
            out[o + 1] = (int16_t)rch;
        }

        return (audio_element_err_t)audio_element_output(self, (char *)g_stereo_buf, (int)needed);
    }

    return (audio_element_err_t)audio_element_output(self, in_buffer, r);
}

void update_audio_output() {
    if (!board_handle) return;

    // Determine Effective Output Mode
    // Force Base Speaker (false) if Alarm or Timer Announcement active
    bool effective_handset = g_output_mode_handset;
    if (g_alarm_active || g_timer_announce_pending || g_force_base_output) {
        effective_handset = false; 
    }

    // Re-apply the current output selection
    audio_board_select_output(effective_handset);

    // Update Volume based on EFFECTIVE mode
    nvs_handle_t my_handle;
    uint8_t vol = APP_DEFAULT_BASE_VOLUME; // Default Base Speaker
    
    if (nvs_open("dialcharm", NVS_READONLY, &my_handle) == ESP_OK) {
        if (effective_handset) {
            if (nvs_get_u8(my_handle, "volume_handset", &vol) != ESP_OK) vol = APP_DEFAULT_HANDSET_VOLUME;
        } else {
            if (nvs_get_u8(my_handle, "volume", &vol) != ESP_OK) vol = APP_DEFAULT_BASE_VOLUME;
        }
        nvs_close(my_handle);
    }
    
    if (g_night_mode_active) {
        vol = APP_NIGHTMODE_VOLUME_PERCENT;
    }

    audio_hal_set_volume(board_handle->audio_hal, vol);
}
std::string get_random_file(std::string folderPath) {
    std::vector<std::string> files;
    DIR *dir;
    struct dirent *ent;
    
    ESP_LOGI(TAG, "Scanning folder: %s", folderPath.c_str());
    if ((dir = opendir(folderPath.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_REG) { // Regular file
                std::string fname = ent->d_name;
                if (fname.find(".wav") != std::string::npos) {
                    files.push_back(folderPath + "/" + fname);
                }
            }
        }
        closedir(dir);
    } else {
        ESP_LOGE(TAG, "Could not open directory: %s", folderPath.c_str());
        return "";
    }

    if (files.empty()) {
        ESP_LOGW(TAG, "No WAV files found in %s", folderPath.c_str());
        return "";
    }

    // Pick random
    int idx = esp_random() % files.size();
    ESP_LOGI(TAG, "Selected %d of %d: %s", idx, files.size(), files[idx].c_str());
    return files[idx];
}

void play_file(const char* path) {
    if (path == NULL || strlen(path) == 0) {
        ESP_LOGE(TAG, "Invalid file path to play");
        return;
    }

    if (pipeline == NULL) {
        ESP_LOGE(TAG, "Audio Pipeline not initialized yet, cannot play: %s", path);
        return;
    }

    
    audio_lock();

    // Soft fade-in to reduce clicks
    g_gain_left_cur = 0.0f;
    g_gain_right_cur = 0.0f;

    // Track last playback type
    g_last_playback_was_dialtone = (strcmp(path, "/sdcard/system/dialtone_1.wav") == 0);

    // Force Output Selection immediately after run logic
    update_audio_output();
    ESP_LOGI(TAG, "Requesting playback: %s", path);
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_reset_ringbuffer(pipeline);
    audio_pipeline_reset_items_state(pipeline);
    audio_element_set_uri(fatfs_stream, path);
    audio_pipeline_run(pipeline);
    is_playing = true;
    audio_unlock();
}

void wait_for_dialtone_silence_if_needed() {
    if (!g_last_playback_was_dialtone) {
        return;
    }

    int64_t now = esp_timer_get_time() / 1000;
    if (is_playing) {
        stop_playback();
        g_last_playback_finished_ms = now;
    }

    int64_t elapsed = now - g_last_playback_finished_ms;
    if (elapsed < DIALTONE_SILENCE_MS) {
        vTaskDelay(pdMS_TO_TICKS(DIALTONE_SILENCE_MS - elapsed));
    }

    g_last_playback_was_dialtone = false;
}

void play_busy_tone() {
    g_line_busy = true;
    stop_playback();
    play_file("/sdcard/system/busy_tone.wav");
}

void play_timer_alarm() {
    g_alarm_active = true;
    g_alarm_source = ALARM_TIMER;
    g_snooze_active = false;
    g_alarm_end_ms = (esp_timer_get_time() / 1000) + (int64_t)APP_TIMER_ALARM_LOOP_MINUTES * 60 * 1000;
    g_alarm_fade_active = false;
    g_alarm_fade_factor = 1.0f;
    g_alarm_retry_last_ms = 0;
    stop_playback();
    update_audio_output(); // Force Base Speaker
    // Play the currently selected alarm (Daily or Default)
    if (g_current_alarm_file.empty()) {
        g_current_alarm_file = get_timer_ringtone_path();
    }
    play_file(g_current_alarm_file.c_str());
}

static int get_snooze_minutes() {
    nvs_handle_t my_handle;
    int32_t val = APP_SNOOZE_DEFAULT_MINUTES;
    if (nvs_open("dialcharm", NVS_READONLY, &my_handle) == ESP_OK) {
        nvs_get_i32(my_handle, "snooze_min", &val);
        nvs_close(my_handle);
    }
    if (val < APP_SNOOZE_MIN_MINUTES) val = APP_SNOOZE_MIN_MINUTES;
    if (val > APP_SNOOZE_MAX_MINUTES) val = APP_SNOOZE_MAX_MINUTES;
    return (int)val;
}


void process_phonebook_function(PhonebookEntry entry) {
    if (entry.type == "FUNCTION") {
        if (entry.value == "COMPLIMENT_CAT") {
            // Parameter is "1", "2", etc. Map to persona_0X
            std::string folder = "/sdcard/persona_0" + entry.parameter + "/" + lang_code();
            std::string file = get_random_file(folder);
            if (!file.empty()) {
                wait_for_dialtone_silence_if_needed();
                g_persona_playback_active = true;
                play_file(file.c_str());
            }
            else play_file(system_path("error_msg").c_str());
        }
        else if (entry.value == "COMPLIMENT_MIX") {
            // Pick random persona 1-5
            int persona = (esp_random() % 5) + 1;
            std::string folder = "/sdcard/persona_0" + std::to_string(persona) + "/" + lang_code();
            std::string file = get_random_file(folder);
            if (!file.empty()) {
                wait_for_dialtone_silence_if_needed();
                g_persona_playback_active = true;
                play_file(file.c_str());
            }
        }
        else if (entry.value == "ANNOUNCE_TIME") {
            announce_time_now();
        }
        else if (entry.value == "REBOOT") {
             ESP_LOGW(TAG, "Rebooting system...");
               play_file(system_path("system_ready").c_str()); // Ack before reboot
             vTaskDelay(pdMS_TO_TICKS(2000));
             esp_restart();
        }
        else if (entry.value == "VOICE_MENU") {
            g_voice_menu_active = true;
            g_voice_menu_started_ms = esp_timer_get_time() / 1000;
            play_file(system_path("menu").c_str());
        }
        else {
            ESP_LOGW(TAG, "Unknown Function: %s", entry.value.c_str());
            play_file(system_path("error_msg").c_str());
        }
    } 
    else if (entry.type == "TTS" || entry.type == "AUDIO") {
        // Direct file mapping
        if (entry.value.rfind("/", 0) == 0) {
            play_file(entry.value.c_str());
        } else {
             ESP_LOGW(TAG, "Invalid path: %s", entry.value.c_str());
        }
    }
}

void stop_playback() {
    if (is_playing) {
        audio_lock();
        audio_pipeline_stop(pipeline);
        audio_pipeline_wait_for_stop(pipeline);
        is_playing = false;
        g_last_playback_finished_ms = esp_timer_get_time() / 1000;
        audio_unlock();
    }
}

// Callbacks
void on_dial_complete(int number) {
    if (g_line_busy) {
        return;
    }
    g_any_digit_dialed = true;
    ESP_LOGI(TAG, "--- DIALED DIGIT: %d ---", number);
    dial_buffer += std::to_string(number);
    last_digit_time = esp_timer_get_time() / 1000; // Update timestamp (ms)
}

void on_button_press() {
    ESP_LOGI(TAG, "--- EXTRA BUTTON (Key 5) PRESSED ---");
    // Handle Timer/Alarm Mute
    if (g_alarm_active) {
        TimeManager::stopAlarm();
        stop_playback();

        if (g_alarm_source == ALARM_TIMER) {
            ESP_LOGI(TAG, "Timer expired -> deleted via Key 5");
            reset_alarm_state(false);
            g_snooze_active = false;
            g_force_base_output = true;
            update_audio_output();
            play_file(system_path("timer_deleted").c_str());
            return;
        }

        ESP_LOGI(TAG, "Snoozing Alarm via Key 5");
        reset_alarm_state(true);
        g_snooze_active = true;
        update_audio_output(); // Restore audio routing
        play_file(system_path("snooze_active").c_str());
        int snooze = get_snooze_minutes();
        ESP_LOGI(TAG, "Snoozing for %d minutes", snooze);

        // Start Timer (Silent, no announcement)
        start_timer_minutes(snooze);
        // Important: start_timer_minutes sets g_timer_intro_playing=false by default (line 69 impl)
        // But main loop might override if it was dialed. Here we just call the helper strictly.
        // We need to ensure we don't accidentally trigger the "Timer Set" voice.
        // The helper "start_timer_minutes" sets:
        // g_timer_announce_pending = false;
        // g_timer_intro_playing = false;
        // So calling it here is SAFE and SILENT.
    }
}

void on_hook_change(bool off_hook) {
    ESP_LOGI(TAG, "--- HOOK STATE: %s ---", off_hook ? "OFF HOOK (Active/Pickup)" : "ON HOOK (Idle/Hangup)");
    
    // Switch Audio Output
    g_output_mode_handset = off_hook;
    update_audio_output();

    g_off_hook = off_hook;

    if (off_hook) {
        // Receiver Picked Up
        bool skip_dialtone = false;
        dial_buffer = "";
        g_line_busy = false;
        g_persona_playback_active = false;
        g_any_digit_dialed = false;
        g_off_hook_start_ms = esp_timer_get_time() / 1000;
        if (g_alarm_active) {
            TimeManager::stopAlarm();
            if (g_alarm_source == ALARM_TIMER) {
                ESP_LOGI(TAG, "Timer expired -> deleted via pickup");
                reset_alarm_state(false);
                g_snooze_active = false;
                g_force_base_output = true;
                update_audio_output();
                play_file(system_path("timer_deleted").c_str());
                skip_dialtone = true;
            } else {
                reset_alarm_state(true);
                g_snooze_active = false;
                update_audio_output(); // Restore audio routing (e.g. back to handset if off-hook)
            }
        }
        // Play dial tone
        if (!skip_dialtone) {
            play_file("/sdcard/system/dialtone_1.wav");
        }
    } else {
        // Receiver Hung Up
        stop_playback();
        dial_buffer = "";
        g_line_busy = false;
        g_persona_playback_active = false;
        g_any_digit_dialed = false;
    }
}

void input_task(void *pvParameters) {
    ESP_LOGI(TAG, "Input Task Started. Priority: %d", uxTaskPriorityGet(NULL));
#if APP_ENABLE_TASK_WDT
    if (esp_task_wdt_add(NULL) == ESP_OK) {
        g_wdt_input_registered = true;
    } else {
        ESP_LOGW(TAG, "Task WDT add failed for input_task");
    }
#endif
    while(1) {
        dial.loop(); // Normal Logic restored
        // dial.debugLoop(); // DEBUG LOGIC DISABLED

        // Calculate Target Gain Base
        float target_left = APP_GAIN_DEFAULT_LEFT;
        float target_right = APP_GAIN_DEFAULT_RIGHT;

        if (APP_PIN_KEY3 >= 0) {
            int level = gpio_get_level((gpio_num_t)APP_PIN_KEY3);
            bool pressed = APP_KEY3_ACTIVE_LOW ? (level == 0) : (level == 1);
            if (pressed != g_key3_pressed) {
                g_key3_pressed = pressed;
                ESP_LOGI(TAG, "Key3 %s", pressed ? "pressed" : "released");
            }
            if (g_key3_pressed) {
                target_right = 0.0f; // Mute Right on Key3
            }
        }
        
        // Apply Alarm Fade Factor
        if (g_alarm_active && g_alarm_source == ALARM_DAILY && g_alarm_fade_active) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now < g_alarm_fade_start_time) g_alarm_fade_start_time = now;
            
            int64_t elapsed = now - g_alarm_fade_start_time;
            if (elapsed < 0) elapsed = 0;
            int64_t ramp_ms = APP_ALARM_RAMP_DURATION_MS;
            if (ramp_ms < 1) ramp_ms = 1;
            if (elapsed > ramp_ms) {
                g_alarm_fade_factor = 1.0f;
            } else {
                g_alarm_fade_factor = (float)elapsed / (float)ramp_ms;
            }
            // Ensure strictly minimum volume so it's audible
            if (g_alarm_fade_factor < 0.05f) g_alarm_fade_factor = 0.05f;
            
            target_left *= g_alarm_fade_factor;
            target_right *= g_alarm_fade_factor;
        }

        g_gain_left_target = target_left;
        g_gain_right_target = target_right;
        
    #if APP_ENABLE_TASK_WDT
        if (g_wdt_input_registered) {
            esp_task_wdt_reset();
        }
    #endif
        // Yield to IDLE task to reset WDT using vTaskDelay
        // Ensure strictly positive delay to allow IDLE task to run
        vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}

// System Monitor Task
#if defined(ENABLE_SYSTEM_MONITOR) && (ENABLE_SYSTEM_MONITOR == 1)
void monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "System Monitor Task Started");
    while(1) {
        ESP_LOGI(TAG, "MONITOR: Heap: %6d bytes (Free) / %6d bytes (Min Free)", 
                 (int)esp_get_free_heap_size(), (int)esp_get_minimum_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(SYSTEM_MONITOR_INTERVAL_MS));
    }
}
#endif

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_LOGI(TAG, "Initializing Dial-A-Charmer (ESP-ADF version)...");

    g_audio_mutex = xSemaphoreCreateMutex();
    if (!g_audio_mutex) {
        ESP_LOGE(TAG, "Audio mutex init failed");
    }
    g_led_mutex = xSemaphoreCreateMutex();
    if (!g_led_mutex) {
        ESP_LOGE(TAG, "LED mutex init failed");
    }

#if APP_ENABLE_TASK_WDT
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = APP_TASK_WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic = APP_TASK_WDT_PANIC,
    };
    esp_err_t wdt_err = esp_task_wdt_init(&wdt_cfg);
    if (wdt_err != ESP_OK && wdt_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Task WDT init failed: %s", esp_err_to_name(wdt_err));
    }
    if (esp_task_wdt_add(NULL) == ESP_OK) {
        g_wdt_main_registered = true;
    } else {
        ESP_LOGW(TAG, "Task WDT add failed for app_main");
    }
#endif

    #if defined(ENABLE_SYSTEM_MONITOR) && (ENABLE_SYSTEM_MONITOR == 1)
    xTaskCreate(monitor_task, "monitor_task", 2048, NULL, 1, NULL);
    #endif

    // --- WS2812 Init ---
    #ifdef APP_PIN_LED
    ESP_LOGI(TAG, "Initializing WS2812 LED on GPIO %d", APP_PIN_LED);
    led_strip_config_t strip_config = {
        .strip_gpio_num = APP_PIN_LED,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = {
            .invert_out = 0,
        },
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .mem_block_symbols = 0,
        .flags = {
            .with_dma = 0,
        },
    };
    // Note: ensure espressif/led_strip component is added
    if (led_strip_new_rmt_device(&strip_config, &rmt_config, &g_led_strip) == ESP_OK) {
        set_led_color(50, 20, 0); // Orange startup
    } else {
        ESP_LOGE(TAG, "Failed to init WS2812!");
    }
    if (g_led_strip) {
        xTaskCreate(led_task, "led_task", 2048, NULL, 1, NULL);
    }
    #endif

    board_handle = audio_board_init();
    if (!board_handle) {
        ESP_LOGE(TAG, "Audio board init failed");
        return;
    }

    // --- 2. Audio Board Init ---
    ESP_LOGI(TAG, "Starting Audio Board...");
    
    // Enable Power Amplifier (GPIO21)
    // Anti-Pop: Wait for Codec/I2S to stabilize before enabling PA
    vTaskDelay(pdMS_TO_TICKS(APP_PA_ENABLE_DELAY_MS));
#if APP_PIN_PA_ENABLE >= 0
    // Ensure it is output
    gpio_set_direction((gpio_num_t)APP_PIN_PA_ENABLE, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)APP_PIN_PA_ENABLE, 1);
    ESP_LOGI(TAG, "Power Amplifier enabled on GPIO %d (Anti-Pop Delay: %d ms)", APP_PIN_PA_ENABLE, APP_PA_ENABLE_DELAY_MS);
#else
    ESP_LOGW(TAG, "Power Amplifier control DISABLED via config");
#endif
    
    audio_hal_set_volume(board_handle->audio_hal, APP_DEFAULT_BASE_VOLUME); // Set volume

    // --- 3. SD Card Peripheral ---
    ESP_LOGI(TAG, "Starting SD Card...");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    periph_sdcard_cfg_t sdcard_cfg = {
        .card_detect_pin = SD_CARD_INTR_GPIO,
        .root = "/sdcard",
        .mode = SD_MODE_SPI, 
    };
    esp_periph_handle_t sdcard_handle = periph_sdcard_init(&sdcard_cfg);
    esp_periph_start(set, sdcard_handle);
    
    int64_t sd_start_ms = esp_timer_get_time() / 1000;
    const int64_t sd_timeout_ms = 5000;
    while (!periph_sdcard_is_mounted(sdcard_handle)) {
        if ((esp_timer_get_time() / 1000) - sd_start_ms > sd_timeout_ms) {
            ESP_LOGE(TAG, "SD Card mount timeout");
            g_sd_error = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (periph_sdcard_is_mounted(sdcard_handle)) {
        ESP_LOGI(TAG, "SD Card mounted successfully");
        g_sd_error = false;
    }

    if (g_sd_error) {
        ESP_LOGE(TAG, "SD Card not available. Starting in degraded mode.");
        // Bring up web UI for diagnostics/configuration.
        webManager.begin();
        g_led_booting = false;
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // --- 1. Input Initialization (Moved to ensure ISR service order) ---
    ESP_LOGI(TAG, "Initializing Rotary Dial (Mode Pin: %d)...", APP_PIN_DIAL_MODE);
    dial.begin();
    dial.setModeActiveLow(APP_DIAL_MODE_ACTIVE_LOW); 
    dial.setPulseActiveLow(APP_DIAL_PULSE_ACTIVE_LOW); 
    
    // Debug Mode Pin State
    if (APP_PIN_DIAL_MODE >= 0) {
        gpio_set_direction((gpio_num_t)APP_PIN_DIAL_MODE, GPIO_MODE_INPUT);
        int level = gpio_get_level((gpio_num_t)APP_PIN_DIAL_MODE);
        ESP_LOGI(TAG, "Startup Mode Pin Level: %d (Expected Active Low: %s)", level, APP_DIAL_MODE_ACTIVE_LOW ? "Yes" : "No");
    }

    dial.onDialComplete(on_dial_complete);
    dial.onHookChange(on_hook_change);
    dial.onButtonPress(on_button_press);

    // Initialize Phonebook (Now that SD is ready)
    phonebook.begin();

    // --- 3b. Web / Network ---
    webManager.begin();

    // --- 4. Audio Pipeline Setup ---
    ESP_LOGI(TAG, "Creating audio pipeline...");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_cfg.rb_size = 16 * 1024;
    pipeline = audio_pipeline_init(&pipeline_cfg);

    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_cfg.buf_sz = 8 * 1024;
    fatfs_cfg.out_rb_size = 16 * 1024;
    fatfs_stream = fatfs_stream_init(&fatfs_cfg);

    wav_decoder_cfg_t wav_cfg = DEFAULT_WAV_DECODER_CONFIG();
    wav_cfg.stack_in_ext = false; // Disable PSRAM stack to avoid FreeRTOS patch requirement
    wav_cfg.out_rb_size = 16 * 1024;
    wav_decoder = wav_decoder_init(&wav_cfg);

    #ifdef __GNUC__
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    #endif
    audio_element_cfg_t gain_cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    #ifdef __GNUC__
    #pragma GCC diagnostic pop
    #endif
    gain_cfg.open = gain_open;
    gain_cfg.close = gain_close;
    gain_cfg.process = gain_process;
    gain_cfg.task_stack = 4096;
    gain_cfg.task_prio = 5;
    gain_cfg.stack_in_ext = false;
    gain_element = audio_element_init(&gain_cfg);

    #ifdef __GNUC__
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    #endif
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    #ifdef __GNUC__
    #pragma GCC diagnostic pop
    #endif
    i2s_cfg.pdm_tx_cfg = {};
    i2s_cfg.pdm_rx_cfg = {};
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.stack_in_ext = false; // Use internal RAM
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO; // Duplicate Mono to Stereo
    i2s_cfg.out_rb_size = 16 * 1024;
    i2s_cfg.buffer_len = 7200;
    i2s_writer = i2s_stream_init(&i2s_cfg);

    audio_pipeline_register(pipeline, fatfs_stream, "file");
    audio_pipeline_register(pipeline, wav_decoder, "wav");
    audio_pipeline_register(pipeline, gain_element, "gain");
    audio_pipeline_register(pipeline, i2s_writer, "i2s");

    const char *link_tag[4] = {"file", "wav", "gain", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 4);

    // Event Config
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline, evt);

    // Start Codec
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

    // Default Output: Base Speaker (Rout)
    audio_board_select_output(false);

    // Play Startup Sound
    ESP_LOGI(TAG, "Playing Startup Sound...");
    g_startup_silence_playing = true;
    audio_element_set_uri(fatfs_stream, "/sdcard/system/silence_300ms.wav");
    audio_pipeline_run(pipeline);

        // Initialize TimeManager (SNTP)
        TimeManager::init();
    
    // --- Start Input Task Last ---
    // Start Input Task with Higher Priority (10) to avoid starvation by Audio
    // Started here to ensure 'pipeline' is initialized before any callback tries to stop it
    xTaskCreate(input_task, "input_task", 4096, &dial, 10, NULL);

    // --- 5. Main Event Loop ---
    g_led_booting = false;
    while (1) {
#if APP_ENABLE_TASK_WDT
    if (g_wdt_main_registered) {
        esp_task_wdt_reset();
    }
#endif
        // --- Regular Tasks ---
        // Check Daily Alarm
        if (TimeManager::checkAlarm()) {
             ESP_LOGI(TAG, "Daily Alarm Triggered! Starting Ring...");
             if (!g_alarm_active) {
                 g_alarm_active = true;
                 g_alarm_source = ALARM_DAILY;
                 g_snooze_active = false;
                 g_alarm_end_ms = (esp_timer_get_time() / 1000) + (int64_t)APP_DAILY_ALARM_LOOP_MINUTES * 60 * 1000;
                 g_alarm_retry_last_ms = 0;
                 
                 // Stop anything currently playing
                 audio_lock();
                 audio_pipeline_stop(pipeline);
                 audio_pipeline_wait_for_stop(pipeline);
                is_playing = false;
                g_last_playback_finished_ms = esp_timer_get_time() / 1000;
                 audio_pipeline_reset_ringbuffer(pipeline);
                 audio_pipeline_reset_items_state(pipeline);
                 audio_unlock();
                 
                 // Enable Base Speaker for Alarm
                 update_audio_output();

                 // Get specifics for today
                 struct tm now_tm = TimeManager::getCurrentTime();
                 DayAlarm today = TimeManager::getAlarm(now_tm.tm_wday);
                 
                 // Handle Fade
                 g_alarm_fade_active = today.volumeRamp;
                 if (g_alarm_fade_active) {
                     g_alarm_fade_start_time = esp_timer_get_time() / 1000;
                     g_alarm_fade_factor = 0.05f;
                 } else {
                     g_alarm_fade_factor = 1.0f;
                 }
                 
                 // FORCE VOLUME FOR ALARM
                 force_alarm_volume();

                 char path[128];
                 if (!today.ringtone.empty()) {
                     snprintf(path, sizeof(path), "/sdcard/ringtones/%s", today.ringtone.c_str());
                     struct stat st;
                     if (stat(path, &st) != 0) {
                         std::string fallback = get_first_ringtone_path();
                        ESP_LOGW(TAG, "Alarm ringtone missing, falling back: %s", path);
                        if (!fallback.empty()) {
                            ESP_LOGI(TAG, "Using first ringtone fallback: %s", fallback.c_str());
                        }
                         g_current_alarm_file = fallback.empty() ? std::string(path) : fallback;
                     } else {
                         g_current_alarm_file = std::string(path);
                     }
                 } else {
                     std::string fallback = get_first_ringtone_path();
                    if (!fallback.empty()) {
                        ESP_LOGI(TAG, "Using first ringtone fallback: %s", fallback.c_str());
                    }
                     g_current_alarm_file = fallback.empty()
                         ? std::string("/sdcard/ringtones/") + APP_DEFAULT_TIMER_RINGTONE
                         : fallback;
                 }
                 play_file(g_current_alarm_file.c_str());
             }
        }

        audio_event_iface_msg_t msg;
        
        // Listen with 100ms timeout to allow polling logic
        esp_err_t ret = audio_event_iface_listen(evt, &msg, 100 / portTICK_PERIOD_MS);

        // --- Logic: Check Dial Timeout ---
        if (!dial_buffer.empty()) {
            int64_t now = esp_timer_get_time() / 1000;
            if (!dial.isDialing() && (now - last_digit_time) > DIAL_TIMEOUT_MS) {
                ESP_LOGI(TAG, "Dial Timeout. Processing Number: %s", dial_buffer.c_str());
                
                // Voice menu (single digit)
                if (g_voice_menu_active && g_off_hook) {
                    if (dial_buffer.size() == 1) {
                        int digit = dial_buffer[0] - '0';
                        handle_voice_menu_digit(digit);
                    } else {
                        start_voice_queue({system_path("error_msg")});
                    }
                    g_voice_menu_active = false;
                }
                // On-hook -> Timer mode (1-3 digits, up to 500 minutes)
                else if (!g_off_hook) {
                    if (dial_buffer.size() >= 1 && dial_buffer.size() <= 3) {
                        int minutes = atoi(dial_buffer.c_str());
                        if (minutes >= APP_TIMER_MIN_MINUTES && minutes <= APP_TIMER_MAX_MINUTES) {
                            ESP_LOGI(TAG, "Timer set: %d minutes", minutes);
                            // Reset alarm file to default for kitchen timer
                            g_current_alarm_file = get_timer_ringtone_path();
                            g_snooze_active = false;

                            start_timer_minutes(minutes);
                            g_timer_announce_minutes = minutes;
                            g_timer_announce_pending = true;
                            g_timer_intro_playing = true;
                            play_file(system_path("timer_set").c_str());
                        } else {
                            ESP_LOGW(TAG, "Invalid timer value: %d", minutes);
                            play_file("/sdcard/system/error_tone.wav");
                        }
                    } else {
                        ESP_LOGW(TAG, "Timer requires 1-3 digits. Got: %s", dial_buffer.c_str());
                        play_file("/sdcard/system/error_tone.wav");
                    }
                } else {
                    // Lookup
                    if (phonebook.hasEntry(dial_buffer)) {
                         PhonebookEntry entry = phonebook.getEntry(dial_buffer);
                         ESP_LOGI(TAG, "Phonebook Match: %s (%s)", entry.name.c_str(), entry.value.c_str());
                         process_phonebook_function(entry);
                    } else {
                        ESP_LOGI(TAG, "Number %s not found.", dial_buffer.c_str());
                        std::vector<std::string> sequence;
                        sequence.push_back(system_path("number_invalid"));
                        sequence.push_back("/sdcard/system/busy_tone.wav");
                        sequence.push_back("/sdcard/system/busy_tone.wav");
                        start_voice_queue(sequence);
                    }
                }

                dial_buffer = ""; // Reset buffer
            }
        }

        // Night mode timeout
        if (g_night_mode_active) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now >= g_night_mode_end_ms) {
                set_night_mode(false);
            }
        }

        // Alarm failover: stop if end time passed, retry if playback stalls
        if (g_alarm_active) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now >= g_alarm_end_ms) {
                TimeManager::stopAlarm();
                stop_playback();
                reset_alarm_state(true);
                update_audio_output();
            } else if (!is_playing && !g_current_alarm_file.empty()) {
                if (now - g_alarm_retry_last_ms >= APP_ALARM_RETRY_INTERVAL_MS) {
                    g_alarm_retry_last_ms = now;
                    play_file(g_current_alarm_file.c_str());
                }
            }
        }

        // Busy tone after 5s off-hook without dialing
        if (g_off_hook && !g_line_busy && !g_any_digit_dialed && dial_buffer.empty()) {
            int64_t now = esp_timer_get_time() / 1000;
            if ((now - g_off_hook_start_ms) > APP_BUSY_TIMEOUT_MS) {
                ESP_LOGI(TAG, "Idle timeout -> busy tone");
                play_busy_tone();
            }
        }

        // Timer alarm check
        if (g_timer_active && !g_alarm_active) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now >= g_timer_end_ms) {
                ESP_LOGI(TAG, "Timer expired -> alarm");
                g_timer_active = false;
                g_snooze_active = false;
                play_timer_alarm();
            }
        }

        if (ret != ESP_OK) {
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT) {
            // Handle Music Info (Sample Rate)
            if (msg.source == (void *)wav_decoder && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                audio_element_info_t music_info = {};
                audio_element_getinfo(wav_decoder, &music_info);
                // Propagate decoder info to gain element (so it knows input channels)
                audio_element_setinfo(gain_element, &music_info);
                int out_channels = (music_info.channels == 1) ? 2 : music_info.channels;
                
                ESP_LOGI(TAG, "WAV info: rate=%d, ch=%d, bits=%d (out_ch=%d)", 
                    music_info.sample_rates, music_info.channels, music_info.bits, out_channels);
                i2s_stream_set_clk(i2s_writer, music_info.sample_rates, music_info.bits, out_channels);
                
                // Enforce output selection
                update_audio_output();
            }
            
            // Handle Playback State Changes
            if (msg.cmd == AEL_MSG_CMD_RESUME) {
                 // Playback started/resumed -> Enforce output
                 update_audio_output();
            }
            
            // Handle Stop
            if (msg.source == (void *)i2s_writer && msg.cmd == AEL_MSG_CMD_REPORT_STATUS) {
                if ((int)msg.data == AEL_STATUS_STATE_FINISHED) {
                    ESP_LOGI(TAG, "Audio Finished.");
                    stop_playback();
                    if (play_next_in_queue()) {
                        continue;
                    }
                    if (g_startup_silence_playing) {
                        g_startup_silence_playing = false;
                        play_file(system_path("system_ready").c_str());
                        continue;
                    }
                    if (g_timer_intro_playing && g_timer_announce_pending) {
                        g_timer_intro_playing = false;
                        g_timer_announce_pending = false;
                        announce_timer_minutes(g_timer_announce_minutes);
                        continue;
                    }
                    if (g_force_base_output) {
                        g_force_base_output = false;
                        update_audio_output();
                    }
                    if (g_alarm_active) {
                        int64_t now = esp_timer_get_time() / 1000;
                        if (now < g_alarm_end_ms) {
                            play_file(g_current_alarm_file.c_str());
                        } else {
                            TimeManager::stopAlarm();
                            reset_alarm_state(true);
                            update_audio_output(); // Restore routing
                        }
                        continue;
                    }
                    if (g_persona_playback_active && g_off_hook) {
                        g_persona_playback_active = false;
                        ESP_LOGI(TAG, "Persona finished -> busy tone (pause)");
                        vTaskDelay(pdMS_TO_TICKS(PERSONA_PAUSE_MS));
                        play_busy_tone();
                        continue;
                    }
                    if (g_line_busy && g_off_hook && !g_alarm_active) {
                        play_busy_tone();
                    }
                }
            }
        }
    }
}
