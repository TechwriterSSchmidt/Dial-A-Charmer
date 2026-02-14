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
#include "esp_sleep.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "board.h"
#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "wav_decoder.h"

// App Includes
#include "app_config.h"
#include "AppSharedUtils.h"
#include "led_strip.h" 
#include "RotaryDial.h"
#include "PhonebookManager.h"
#include "WebManager.h"
#include "TimeManager.h" 
#include "driver/rtc_io.h"

static const char *TAG = "DIAL_A_CHARMER_ESP";
static const int kNightModeDurationHours = 6;
static const uint8_t kNightBaseVolumeDefault = 50;

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
const int DIALTONE_SILENCE_MS = APP_DIALTONE_SILENCE_MS;
bool is_playing = false;
bool g_output_mode_handset = false; 
int g_last_effective_handset = -1;
static volatile bool g_effective_handset = false;
bool g_off_hook = false;
bool g_line_busy = false;
bool g_persona_playback_active = false;
bool g_any_digit_dialed = false;
uint32_t g_off_hook_start_ms = 0;
int64_t g_last_playback_finished_ms = 0;
bool g_last_playback_was_dialtone = false;

struct TimerState {
    bool active = false;
    int minutes = 0;
    int64_t end_ms = 0;
    bool announce_pending = false;
    bool intro_playing = false;
    int announce_minutes = 0;
};

static TimerState g_timer_state;

bool g_startup_silence_playing = false;
bool g_voice_menu_active = false;
std::vector<std::string> g_voice_queue;
bool g_voice_queue_active = false;
bool g_voice_menu_reannounce = false;
bool g_night_mode_active = false;
bool g_night_mode_manual = false;
int64_t g_night_mode_end_ms = 0;
uint8_t g_led_r = 50;
uint8_t g_led_g = 20;
uint8_t g_led_b = 0;
bool g_led_enabled = (APP_LED_DEFAULT_ENABLED != 0);
uint8_t g_led_day_percent = APP_LED_DAY_PERCENT;
uint8_t g_led_night_percent = APP_LED_NIGHT_PERCENT;
uint8_t g_led_day_start_hour = APP_LED_DAY_START_HOUR;
uint8_t g_led_night_start_hour = APP_LED_NIGHT_START_HOUR;
bool g_led_schedule_night = false;
uint8_t g_night_prev_r = 50;
uint8_t g_night_prev_g = 20;
uint8_t g_night_prev_b = 0;
bool g_led_booting = true;
bool g_snooze_active = false;
bool g_sd_error = false;
bool g_force_base_output = false;
bool g_pending_handset_restore = false;
bool g_pending_handset_state = false;
bool g_reboot_pending = false;
int64_t g_reboot_request_time = 0;
bool g_extra_btn_active = false;
bool g_extra_btn_long_handled = false;
int64_t g_extra_btn_press_start_ms = 0;
// Alarm State
int g_saved_volume = -1;

enum AlarmSource {
    ALARM_NONE = 0,
    ALARM_TIMER,
    ALARM_DAILY,
};

struct AlarmState {
    bool active = false;
    AlarmSource source = ALARM_NONE;
    bool msg_active = false;
    int64_t end_ms = 0;
    int64_t retry_last_ms = 0;
    bool fade_active = false;
    float fade_factor = 1.0f;
    int64_t fade_start_time = 0;
    std::string current_file;
};

static AlarmState g_alarm_state;

static bool g_persona_hangup_pending = false;

static int64_t g_fade_in_end_time = 0;

RTC_DATA_ATTR static uint32_t g_boot_count = 0;

#if APP_ENABLE_TASK_WDT
static bool g_wdt_main_registered = false;
static bool g_wdt_input_registered = false;
#if APP_WDT_DIAG_LOG
static int64_t g_wdt_diag_last_main_loop_ms = 0;
static int64_t g_wdt_diag_last_input_loop_ms = 0;
static int64_t g_wdt_diag_last_main_warn_ms = 0;
static int64_t g_wdt_diag_last_input_warn_ms = 0;
static int64_t g_wdt_diag_last_main_heartbeat_ms = 0;
static int64_t g_wdt_diag_last_input_heartbeat_ms = 0;
#endif
#endif

// Helper Forward Declarations
static void start_voice_queue(const std::vector<std::string> &files);

static const char *reset_reason_to_str(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "poweron";
        case ESP_RST_EXT: return "ext";
        case ESP_RST_SW: return "sw";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "int_wdt";
        case ESP_RST_TASK_WDT: return "task_wdt";
        case ESP_RST_WDT: return "wdt";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        default: return "unknown";
    }
}

#if APP_ENABLE_TASK_WDT && APP_WDT_DIAG_LOG
static void wdt_diag_log_boot_context(esp_reset_reason_t reason) {
    bool watchdog_related = (reason == ESP_RST_INT_WDT) ||
                            (reason == ESP_RST_TASK_WDT) ||
                            (reason == ESP_RST_WDT) ||
                            (reason == ESP_RST_PANIC);
    if (!watchdog_related) {
        return;
    }

    ESP_LOGW(TAG,
             "WDT-DIAG boot context: reason=%s(%d) timeout_ms=%d panic=%d free_heap=%u min_heap=%u",
             reset_reason_to_str(reason),
             (int)reason,
             APP_TASK_WDT_TIMEOUT_SEC * 1000,
             (int)APP_TASK_WDT_PANIC,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
}

static inline void wdt_diag_check_loop_stall(const char *loop_name,
                                             int64_t *last_loop_ms,
                                             int64_t *last_warn_ms,
                                             int64_t warn_threshold_ms) {
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (*last_loop_ms > 0) {
        int64_t dt_ms = now_ms - *last_loop_ms;
        if (dt_ms > warn_threshold_ms && (now_ms - *last_warn_ms) > warn_threshold_ms) {
            ESP_LOGW(TAG,
                     "WDT-DIAG loop stall: %s dt=%lldms (threshold=%lldms) heap=%u min_heap=%u",
                     loop_name,
                     dt_ms,
                     warn_threshold_ms,
                     (unsigned)esp_get_free_heap_size(),
                     (unsigned)esp_get_minimum_free_heap_size());
            *last_warn_ms = now_ms;
        }
    }
    *last_loop_ms = now_ms;
}

static inline void wdt_diag_heartbeat(const char *loop_name, int64_t *last_heartbeat_ms) {
    int64_t now_ms = esp_timer_get_time() / 1000;
    if ((now_ms - *last_heartbeat_ms) < APP_WDT_HEARTBEAT_MS) {
        return;
    }

    ESP_LOGI(TAG,
             "WDT-DIAG heartbeat: %s stack_hwm=%u free_heap=%u min_heap=%u",
             loop_name,
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
    *last_heartbeat_ms = now_ms;
}
#endif

void play_file(const char* path);
void update_audio_output();
void safe_reboot();
static void audio_lock();
static void audio_unlock();
static void handle_extra_button_short_press();
static void enter_deep_sleep();

static const char *lang_code() {
    return app_lang_code();
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

    files.push_back(time_path("and.wav"));

    snprintf(buf, sizeof(buf), "m_%02d.wav", now.tm_min);
    files.push_back(time_path(buf));
    files.push_back(time_path("minutes.wav"));

    files.push_back(time_path("date_intro.wav"));
    snprintf(buf, sizeof(buf), "day_%d.wav", now.tm_mday);
    files.push_back(time_path(buf));
    snprintf(buf, sizeof(buf), "month_%d.wav", now.tm_mon);
    files.push_back(time_path(buf));
    files.push_back("/sdcard/system/silence_300ms.wav");
    snprintf(buf, sizeof(buf), "year_%d.wav", now.tm_year + 1900);
    files.push_back(time_path(buf));

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

static void load_led_settings_from_nvs() {
    nvs_handle_t my_handle;
    if (nvs_open("dialcharm", NVS_READONLY, &my_handle) != ESP_OK) {
        return;
    }

    AppLedSettings led_settings = app_default_led_settings();
    app_load_led_settings_from_handle(my_handle, &led_settings);
    g_led_enabled = (led_settings.enabled != 0);
    g_led_day_percent = led_settings.day_pct;
    g_led_night_percent = led_settings.night_pct;
    g_led_day_start_hour = led_settings.day_start;
    g_led_night_start_hour = led_settings.night_start;

    nvs_close(my_handle);
}

static bool is_led_night_hour(int hour) {
    if (g_led_day_start_hour == g_led_night_start_hour) return false;
    if (g_led_day_start_hour < g_led_night_start_hour) {
        return (hour < g_led_day_start_hour) || (hour >= g_led_night_start_hour);
    }
    return (hour >= g_led_night_start_hour) && (hour < g_led_day_start_hour);
}

static uint8_t get_led_active_percent() {
    uint8_t pct = g_led_schedule_night ? g_led_night_percent : g_led_day_percent;
    if (g_night_mode_active) {
        pct = g_led_night_percent;
    }
    return (pct > 100) ? 100 : pct;
}

enum LedState {
    LED_BOOTING,
    LED_IDLE,
    LED_ALARM,
    LED_TIMER,
    LED_SNOOZE,
    LED_ERROR,
};

static void set_pa_enable(bool enable) {
#if APP_PIN_PA_ENABLE >= 0
    static bool s_pa_enabled = false;
    if (enable != s_pa_enabled) {
        if (enable) {
            gpio_set_level((gpio_num_t)APP_PIN_PA_ENABLE, 1);
            vTaskDelay(pdMS_TO_TICKS(APP_PA_ENABLE_DELAY_MS));
            ESP_LOGI(TAG, "PA Enabled");
        } else {
            vTaskDelay(pdMS_TO_TICKS(APP_PA_DISABLE_DELAY_MS));
            gpio_set_level((gpio_num_t)APP_PIN_PA_ENABLE, 0);
            ESP_LOGI(TAG, "PA Disabled");
        }
        s_pa_enabled = enable;
    }
#endif
}

static inline void set_codec_mute(bool mute) {
    if (board_handle && board_handle->audio_hal) {
        audio_hal_set_mute(board_handle->audio_hal, mute);
    }
}

static void pipeline_stop_and_reset(bool reset_items_state, bool take_audio_lock) {
    if (!pipeline) {
        return;
    }

    if (take_audio_lock) {
        audio_lock();
    }

    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    is_playing = false;
    g_last_playback_finished_ms = esp_timer_get_time() / 1000;

    if (reset_items_state) {
        audio_pipeline_reset_ringbuffer(pipeline);
        audio_pipeline_reset_items_state(pipeline);
    }

    if (take_audio_lock) {
        audio_unlock();
    }
}

static void apply_led_color(uint8_t r, uint8_t g, uint8_t b) {
    if (!g_led_enabled) {
        set_led_color(0, 0, 0);
        return;
    }
    uint8_t pct = get_led_active_percent();
    r = (uint8_t)((r * pct) / 100);
    g = (uint8_t)((g * pct) / 100);
    b = (uint8_t)((b * pct) / 100);
    set_led_color(r, g, b);
}

static LedState get_led_state() {
    if (g_led_booting) return LED_BOOTING;
    if (g_sd_error) return LED_ERROR;
    if (g_alarm_state.active) return (g_alarm_state.source == ALARM_DAILY) ? LED_ALARM : LED_TIMER;
    if (g_snooze_active) return LED_SNOOZE;
    return LED_IDLE;
}

static void led_task(void *pvParameters) {
    (void)pvParameters;
    int step = 0;
    int error_phase = 0;
    int error_ticks = 0;
    const int tick_ms = 50;
    int64_t last_settings_ms = 0;
    int64_t last_mode_ms = 0;

    while (true) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if ((now_ms - last_settings_ms) > 2000) {
            load_led_settings_from_nvs();
            last_settings_ms = now_ms;
        }
        if ((now_ms - last_mode_ms) > 1000) {
            struct tm now_tm = TimeManager::getCurrentTime();
            g_led_schedule_night = is_led_night_hour(now_tm.tm_hour);
            last_mode_ms = now_ms;
        }

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
            // Slow warm white breathing
            int phase = step % 160;
            int t = (phase <= 80) ? phase : (160 - phase);
            uint8_t intensity = (uint8_t)(18 + ((52 * t) / 80)); // 18%..70%
            uint8_t r = (uint8_t)((255 * intensity) / 100);
            uint8_t g = (uint8_t)((220 * intensity) / 100);
            uint8_t b = (uint8_t)((180 * intensity) / 100);
            apply_led_color(r, g, b);
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

static void set_night_mode(bool enable, bool manual_override) {
    if (enable) {
        g_night_mode_active = true;
        g_night_mode_end_ms = (esp_timer_get_time() / 1000) +
            (int64_t)kNightModeDurationHours * 60 * 60 * 1000;
        g_night_prev_r = g_led_r;
        g_night_prev_g = g_led_g;
        g_night_prev_b = g_led_b;
        apply_led_color(g_night_prev_r, g_night_prev_g, g_night_prev_b);
    } else {
        g_night_mode_active = false;
        apply_led_color(g_night_prev_r, g_night_prev_g, g_night_prev_b);
    }
    g_night_mode_manual = manual_override;
    nvs_handle_t my_handle;
    if (nvs_open("dialcharm", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_u8(my_handle, "night_mode_active", g_night_mode_active ? 1 : 0);
        nvs_set_u8(my_handle, "night_mode_manual", g_night_mode_manual ? 1 : 0);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
    update_audio_output();
}

static void load_night_mode_from_nvs() {
    nvs_handle_t my_handle;
    uint8_t active = 0;
    uint8_t manual = 0;
    if (nvs_open("dialcharm", NVS_READONLY, &my_handle) == ESP_OK) {
        nvs_get_u8(my_handle, "night_mode_active", &active);
        nvs_get_u8(my_handle, "night_mode_manual", &manual);
        nvs_close(my_handle);
    }
    g_night_mode_manual = (manual != 0);
    if (active) {
        g_night_mode_active = true;
        g_night_mode_end_ms = (esp_timer_get_time() / 1000) +
            (int64_t)kNightModeDurationHours * 60 * 60 * 1000;
    }
}

static void refresh_night_mode_from_schedule() {
    if (g_night_mode_manual) {
        struct tm now = TimeManager::getCurrentTime();
        if (now.tm_year >= 120 && !is_led_night_hour(now.tm_hour)) {
            set_night_mode(false, false);
        }
        return;
    }
    struct tm now = TimeManager::getCurrentTime();
    if (now.tm_year < 120) return;
    bool should_be_night = is_led_night_hour(now.tm_hour);
    if (should_be_night != g_night_mode_active) {
        set_night_mode(should_be_night, false);
    }
}

static void start_voice_queue(const std::vector<std::string> &files) {
    if (files.empty()) return;
    g_voice_queue = files;
    g_voice_queue_active = true;
    play_file(g_voice_queue.front().c_str());
    g_voice_queue.erase(g_voice_queue.begin());
}

static void play_voice_menu_prompt() {
    start_voice_queue({
        system_path("menu"),
        "/sdcard/system/silence_300ms.wav",
        system_path("menu_options"),
        "/sdcard/system/silence_300ms.wav",
        system_path("menu_exit"),
    });
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
    if (value > APP_TIMER_MAX_MINUTES) value = APP_TIMER_MAX_MINUTES;
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

        if (!app_is_lang_en()) {
            files.push_back(time_path("uhr.wav"));
        }

        start_voice_queue(files);
    } else if (digit == 2) {
        if (g_night_mode_active) {
            set_night_mode(false, true);
            start_voice_queue({system_path("night_off")});
        } else {
            set_night_mode(true, true);
            start_voice_queue({system_path("night_on")});
        }
    } else if (digit == 3) {
        std::vector<std::string> files;
        files.push_back(system_path("pb_menu_title"));
        files.push_back(system_path("pb_persona1_opt"));
        files.push_back(system_path("pb_persona2_opt"));
        files.push_back(system_path("pb_persona3_opt"));
        files.push_back(system_path("pb_persona4_opt"));
        files.push_back(system_path("pb_persona5_opt"));
        files.push_back(system_path("pb_random_mix_opt"));
        files.push_back(system_path("pb_time_opt"));
        files.push_back(system_path("pb_menu_opt"));
        files.push_back(system_path("pb_reboot_opt"));
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

    if (g_voice_menu_active) {
        g_voice_menu_reannounce = true;
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
    g_alarm_state.active = false;
    g_alarm_state.source = ALARM_NONE;
    g_alarm_state.msg_active = false;
    g_alarm_state.fade_active = false;
    g_alarm_state.fade_factor = 1.0f;
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
    g_timer_state.active = true;
    g_timer_state.minutes = minutes;
    g_timer_state.end_ms = (esp_timer_get_time() / 1000) + (int64_t)minutes * 60 * 1000;
    g_timer_state.announce_pending = false;
    g_timer_state.intro_playing = false;
}


static bool cancel_timer_with_feedback(const char *reason) {
    if (!g_timer_state.active) return false;

    ESP_LOGI(TAG, "Timer active -> deleted via %s", reason ? reason : "unknown");
    g_timer_state.active = false;
    g_timer_state.minutes = 0;
    g_timer_state.end_ms = 0;
    g_timer_state.announce_pending = false;
    g_timer_state.intro_playing = false;
    g_timer_state.announce_minutes = 0;
    g_snooze_active = false;

    stop_playback();
    g_pending_handset_restore = true;
    g_pending_handset_state = g_output_mode_handset;
    g_output_mode_handset = false;
    g_force_base_output = true;
    update_audio_output();
    play_file(system_path("timer_deleted").c_str());
    return true;
}

static std::string get_first_ringtone_path() {
    return app_get_first_wav_path("/sdcard/ringtones");
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
static float g_noise_gate_factor = 1.0f;

static void fade_out_audio_soft(int delay_ms) {
    if (delay_ms < 1) return;
    g_gain_left_target = 0.0f;
    g_gain_right_target = 0.0f;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

static bool handset_noise_gate_enabled() {
    if (g_alarm_state.active || g_timer_state.announce_pending || g_force_base_output) {
        return false;
    }
    return g_output_mode_handset;
}

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

    int peak = 0;
    for (int i = 0; i < sample_count; ++i) {
        int v = samples[i];
        if (v < 0) v = -v;
        if (v > peak) peak = v;
    }
    float gate_target = 1.0f;
    if (handset_noise_gate_enabled() && peak < APP_HANDSET_NOISE_GATE_THRESHOLD) {
        gate_target = APP_HANDSET_NOISE_GATE_FLOOR;
    }
    g_noise_gate_factor += (gate_target - g_noise_gate_factor) * APP_HANDSET_NOISE_GATE_SMOOTH;
    if (g_noise_gate_factor < APP_HANDSET_NOISE_GATE_FLOOR) g_noise_gate_factor = APP_HANDSET_NOISE_GATE_FLOOR;
    if (g_noise_gate_factor > 1.0f) g_noise_gate_factor = 1.0f;

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
            float l = (float)samples[i] * g_gain_left_cur * g_noise_gate_factor;
            float rch = (float)samples[i + 1] * g_gain_right_cur * g_noise_gate_factor;
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
            float l = v * g_gain_left_cur * g_noise_gate_factor;
            float rch = v * g_gain_right_cur * g_noise_gate_factor;
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
    if (g_alarm_state.active || g_timer_state.announce_pending || g_force_base_output) {
        effective_handset = false; 
    }
    g_effective_handset = effective_handset;

    bool output_changed = (g_last_effective_handset < 0) || (effective_handset != (g_last_effective_handset != 0));
    if (output_changed) {
        fade_out_audio_soft(APP_GAIN_RAMP_MS + APP_WAV_FADE_OUT_EXTRA_MS);
        set_codec_mute(true);
        vTaskDelay(pdMS_TO_TICKS(APP_OUTPUT_MUTE_DELAY_MS));
    }

    // Re-apply the current output selection
    audio_board_select_output(effective_handset);

    // Update Volume based on EFFECTIVE mode
    nvs_handle_t my_handle;
    uint8_t vol = APP_DEFAULT_BASE_VOLUME; // Default Base Speaker
    
    uint8_t night_base_vol = kNightBaseVolumeDefault;
    if (nvs_open("dialcharm", NVS_READONLY, &my_handle) == ESP_OK) {
        if (effective_handset) {
            if (nvs_get_u8(my_handle, "volume_handset", &vol) != ESP_OK) vol = APP_DEFAULT_HANDSET_VOLUME;
        } else {
            if (nvs_get_u8(my_handle, "volume", &vol) != ESP_OK) vol = APP_DEFAULT_BASE_VOLUME;
        }
        nvs_get_u8(my_handle, "night_base_volume", &night_base_vol);
        nvs_close(my_handle);
    }
    
    if (g_night_mode_active && !effective_handset) {
        vol = night_base_vol;
    }

    audio_hal_set_volume(board_handle->audio_hal, vol);

    if (output_changed) {
        vTaskDelay(pdMS_TO_TICKS(APP_OUTPUT_MUTE_DELAY_MS));
        set_codec_mute(false);
        g_last_effective_handset = effective_handset ? 1 : 0;
    }
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

    set_codec_mute(true);

    // Optimized Stop Logic for Fast Switching
    audio_lock();

    // Short soft fade + mute to reduce clicks between WAVs
    if (is_playing) {
        fade_out_audio_soft(APP_GAIN_RAMP_MS + APP_WAV_FADE_OUT_EXTRA_MS);
        set_codec_mute(true);
        vTaskDelay(pdMS_TO_TICKS(APP_OUTPUT_MUTE_DELAY_MS));
    }

    pipeline_stop_and_reset(true, false);

    if (APP_WAV_SWITCH_DELAY_MS > 0) {
        vTaskDelay(pdMS_TO_TICKS(APP_WAV_SWITCH_DELAY_MS));
    }

    // Soft fade-in to reduce clicks
    g_gain_left_cur = 0.0f;
    g_gain_right_cur = 0.0f;
    
    // Apply initial fade-in ramp
    g_gain_ramp_ms = APP_WAV_FADE_IN_MS;
    g_fade_in_end_time = (esp_timer_get_time() / 1000) + APP_WAV_FADE_IN_MS + 100;


    // Track last playback type
    g_last_playback_was_dialtone = (strcmp(path, "/sdcard/system/dial_tone.wav") == 0);

    // Force Output Selection immediately after run logic
    update_audio_output();
    ESP_LOGI(TAG, "Requesting playback: %s", path);

    audio_element_set_uri(fatfs_stream, path);
    
    if (audio_pipeline_run(pipeline) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start pipeline for: %s", path);
        // Clean up state if run failed
        pipeline_stop_and_reset(false, false);
        set_pa_enable(false);
    } else {
        is_playing = true;
        set_pa_enable(true);
        set_codec_mute(false);
    }
    
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
    g_alarm_state.active = true;
    g_alarm_state.source = ALARM_TIMER;
    g_snooze_active = false;
    g_alarm_state.end_ms = (esp_timer_get_time() / 1000) + (int64_t)APP_TIMER_ALARM_LOOP_MINUTES * 60 * 1000;
    g_alarm_state.fade_active = false;
    g_alarm_state.fade_factor = 1.0f;
    g_alarm_state.retry_last_ms = 0;
    stop_playback();
    update_audio_output(); // Force Base Speaker
    // Play the currently selected alarm (Daily or Default)
    if (g_alarm_state.current_file.empty()) {
        g_alarm_state.current_file = get_timer_ringtone_path();
    }
    play_file(g_alarm_state.current_file.c_str());
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

static void play_persona_with_hook_sfx(const std::string &file) {
    if (file.empty()) return;
    g_persona_playback_active = true;
    start_voice_queue({
        "/sdcard/system/hook_pickup.wav", 
        file 
        // Hangup is handled by event loop logic after queue finishes
    });
}


void process_phonebook_function(PhonebookEntry entry) {
    if (entry.type == "FUNCTION") {
        if (entry.value == "COMPLIMENT_CAT") {
            // Parameter is "1", "2", etc. Map to persona_0X
            std::string folder = "/sdcard/persona_0" + entry.parameter + "/" + lang_code();
            std::string file = get_random_file(folder);
            if (!file.empty()) {
                wait_for_dialtone_silence_if_needed();
                play_persona_with_hook_sfx(file);
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
                play_persona_with_hook_sfx(file);
            }
        }
        else if (entry.value == "ANNOUNCE_TIME") {
            announce_time_now();
        }
        else if (entry.value == "REBOOT") {
             ESP_LOGW(TAG, "Reboot requested. Playing ack...");
             play_file("/sdcard/system/pb_reboot_en.wav");
             g_reboot_pending = true;
             g_reboot_request_time = esp_timer_get_time() / 1000;
        }
        else if (entry.value == "VOICE_MENU") {
            g_voice_menu_active = true;
            g_voice_menu_reannounce = false;
            play_voice_menu_prompt();
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
        fade_out_audio_soft(APP_GAIN_RAMP_MS + APP_WAV_FADE_OUT_EXTRA_MS);
        set_codec_mute(true);
        vTaskDelay(pdMS_TO_TICKS(APP_OUTPUT_MUTE_DELAY_MS));
        pipeline_stop_and_reset(false, true);
    }
}

void safe_reboot() {
    ESP_LOGW(TAG, "Safe reboot: muting audio and disabling PA");
    stop_playback();
    set_codec_mute(true);
    set_pa_enable(false);
    vTaskDelay(pdMS_TO_TICKS(APP_OUTPUT_MUTE_DELAY_MS));
    vTaskDelay(pdMS_TO_TICKS(APP_PA_DISABLE_DELAY_MS));
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
}

// Callbacks
void on_dial_complete(int number) {
    if (g_line_busy) {
        return;
    }
    g_any_digit_dialed = true;
    ESP_LOGI(TAG, "--- DIALED DIGIT: %d ---", number);
    if (dial_buffer.empty() && g_last_playback_was_dialtone) {
        stop_playback();
        g_last_playback_was_dialtone = false;
    }
    if (g_voice_menu_active && g_off_hook) {
        if (g_voice_queue_active) {
            g_voice_queue_active = false;
            g_voice_queue.clear();
        }
        if (is_playing) {
            stop_playback();
        }
        if (dial_buffer.empty() && number >= 1 && number <= 4) {
            handle_voice_menu_digit(number);
            dial_buffer.clear();
            return;
        }
    }
    dial_buffer += std::to_string(number);
    last_digit_time = esp_timer_get_time() / 1000; // Update timestamp (ms)
}

void on_button_press() {
    ESP_LOGI(TAG, "--- EXTRA BUTTON (Key 5) PRESSED ---");
    g_extra_btn_active = true;
    g_extra_btn_long_handled = false;
    g_extra_btn_press_start_ms = esp_timer_get_time() / 1000;
}

static void handle_extra_button_short_press() {
    if (!g_alarm_state.active && g_timer_state.active) {
        cancel_timer_with_feedback("Key 5");
        return;
    }
    // Handle Timer/Alarm Mute
    if (g_alarm_state.active) {
        TimeManager::stopAlarm();
        stop_playback();

        if (g_alarm_state.source == ALARM_TIMER) {
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
        // Important: start_timer_minutes keeps timer announcement flags disabled by default
        // The main loop may override flags after a dial flow.
        // This path must not trigger the "Timer Set" voice.
        // The helper "start_timer_minutes" sets:
        // g_timer_state.announce_pending = false;
        // g_timer_state.intro_playing = false;
        // This keeps snooze start silent.
    }
}

static void enter_deep_sleep() {
    ESP_LOGI(TAG, "Entering deep sleep (Key 5 long press)");
    play_file("/sdcard/system/system_sleep_en.wav");
    vTaskDelay(pdMS_TO_TICKS(3000));
    stop_playback();
    set_pa_enable(false);
    set_led_color(0, 0, 0);
    
    // Wait for button release before entering sleep
    ESP_LOGI(TAG, "Waiting for button release before sleep...");
    while (gpio_get_level((gpio_num_t)APP_PIN_EXTRA_BTN) == 0) {
        #if APP_ENABLE_TASK_WDT
        if (g_wdt_input_registered) {
            esp_task_wdt_reset();
        }
        #endif
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGI(TAG, "Button released, entering sleep now");
    
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_deep_sleep_start();
}

void on_hook_change(bool off_hook) {
    ESP_LOGI(TAG, "--- HOOK STATE: %s ---", off_hook ? "OFF HOOK (Active/Pickup)" : "ON HOOK (Idle/Hangup)");

    g_off_hook = off_hook;

    if (off_hook) {
        // CRITICAL: Set timestamp IMMEDIATELY to prevent race with busy-timeout check in main loop
        g_off_hook_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
        
        // Switch Audio Output to handset before any playback
        g_output_mode_handset = true;
        update_audio_output();

        // Boot Bug Fix: Prevent "System Ready" from overriding Dial Tone if picked up during startup
        if (g_startup_silence_playing) {
            ESP_LOGI(TAG, "Pickup during startup -> Cancelling startup sequence");
            g_startup_silence_playing = false;
        }

        // Receiver Picked Up
        bool skip_dialtone = false;
        dial_buffer = "";
        g_line_busy = false;
        g_persona_playback_active = false;
        g_any_digit_dialed = false;

        if (g_alarm_state.active) {
            TimeManager::stopAlarm();
            if (g_alarm_state.source == ALARM_TIMER) {
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
                g_force_base_output = true;
                update_audio_output(); 
                
                // Play specific message if enabled for this alarm
                if (g_alarm_state.msg_active) {
                    // Logic from "11" (COMPLIMENT_MIX)
                    int persona = (esp_random() % 5) + 1;
                    std::string folder = "/sdcard/persona_0" + std::to_string(persona) + "/" + lang_code();
                    std::string file = get_random_file(folder);
                    
                    if (!file.empty()) {
                        play_file(file.c_str());
                    }
                }
                
                skip_dialtone = true;
            }
        }
        // Play dial tone
        if (!skip_dialtone) {
            play_file("/sdcard/system/dial_tone.wav");
        }
    } else {
        // Receiver Hung Up
        stop_playback();
        set_pa_enable(false);

        if (g_voice_menu_active) {
            g_voice_menu_active = false;
            g_voice_menu_reannounce = false;
            g_voice_queue_active = false;
            g_voice_queue.clear();
        }
        g_output_mode_handset = false;
        update_audio_output();
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
#if APP_ENABLE_TASK_WDT && APP_WDT_DIAG_LOG
        wdt_diag_check_loop_stall("input_task", &g_wdt_diag_last_input_loop_ms, &g_wdt_diag_last_input_warn_ms, APP_WDT_LOOP_WARN_MS);
        wdt_diag_heartbeat("input_task", &g_wdt_diag_last_input_heartbeat_ms);
#endif
        // Reset ramp duration if fade-in finished
        if (g_gain_ramp_ms != APP_GAIN_RAMP_MS) {
            int64_t now_check = esp_timer_get_time() / 1000;
            if (now_check > g_fade_in_end_time) {
                g_gain_ramp_ms = APP_GAIN_RAMP_MS;
            }
        }

        dial.loop(); // Normal Logic restored
        // dial.debugLoop(); // DEBUG LOGIC DISABLED

        int64_t now_ms = esp_timer_get_time() / 1000;
        bool btn_down = dial.isButtonDown();
        if (btn_down) {
            if (!g_extra_btn_active) {
                g_extra_btn_active = true;
                g_extra_btn_long_handled = false;
                g_extra_btn_press_start_ms = now_ms;
            }
            if (!g_extra_btn_long_handled &&
                (now_ms - g_extra_btn_press_start_ms) >= APP_EXTRA_BTN_DEEPSLEEP_MS) {
                g_extra_btn_long_handled = true;
                enter_deep_sleep();
            }
        } else if (g_extra_btn_active) {
            if (!g_extra_btn_long_handled) {
                handle_extra_button_short_press();
            }
            g_extra_btn_active = false;
        }

        // Calculate Target Gain Base
        float target_left = APP_GAIN_DEFAULT_LEFT;
        float target_right = APP_GAIN_DEFAULT_RIGHT;

        if (g_effective_handset) {
            target_right = 0.0f;
        } else {
            target_left = 0.0f;
        }

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
        if (g_alarm_state.active && g_alarm_state.source == ALARM_DAILY && g_alarm_state.fade_active) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now < g_alarm_state.fade_start_time) g_alarm_state.fade_start_time = now;
            
            int64_t elapsed = now - g_alarm_state.fade_start_time;
            if (elapsed < 0) elapsed = 0;
            int64_t ramp_ms = APP_ALARM_RAMP_DURATION_MS;
            if (ramp_ms < 1) ramp_ms = 1;
            if (elapsed > ramp_ms) {
                g_alarm_state.fade_factor = 1.0f;
            } else {
                g_alarm_state.fade_factor = (float)elapsed / (float)ramp_ms;
            }
            // Ensure strictly minimum volume so it's audible
            if (g_alarm_state.fade_factor < APP_ALARM_FADE_MIN_FACTOR) g_alarm_state.fade_factor = APP_ALARM_FADE_MIN_FACTOR;
            
            target_left *= g_alarm_state.fade_factor;
            target_right *= g_alarm_state.fade_factor;
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
    webManager.startLogCapture();
    g_boot_count++;
    esp_reset_reason_t reset_reason = esp_reset_reason();
    webManager.setResetInfo(g_boot_count, reset_reason_to_str(reset_reason), (int)reset_reason);
    ESP_LOGW(TAG, "Boot #%u reset_reason=%s (%d)",
             (unsigned)g_boot_count,
             reset_reason_to_str(reset_reason),
             (int)reset_reason);
#if APP_ENABLE_TASK_WDT && APP_WDT_DIAG_LOG
    wdt_diag_log_boot_context(reset_reason);
#endif
    ESP_LOGI(TAG, "Initializing Dial-A-Charmer (ESP-ADF version)...");

    load_night_mode_from_nvs();

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
    
    // Attempt init, suppressing error log if already initialized
    esp_log_level_set("task_wdt", ESP_LOG_NONE);
    esp_err_t wdt_err = esp_task_wdt_init(&wdt_cfg);
    esp_log_level_set("task_wdt", ESP_LOG_INFO);

    if (wdt_err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "TWDT already initialized, reconfiguring...");
        esp_task_wdt_reconfigure(&wdt_cfg);
    } else if (wdt_err != ESP_OK) {
        ESP_LOGE(TAG, "Task WDT init failed: %s", esp_err_to_name(wdt_err));
    }

    if (esp_task_wdt_add(NULL) == ESP_OK) {
        g_wdt_main_registered = true;
    } else {
        ESP_LOGW(TAG, "Task WDT add failed for app_main");
    }
#endif

    #if defined(ENABLE_SYSTEM_MONITOR) && (ENABLE_SYSTEM_MONITOR == 1)
        xTaskCreate(monitor_task, "monitor_task", 4096, NULL, 1, NULL);
    #endif

    // --- WS2812 Init ---
    #ifdef APP_PIN_LED
    ESP_LOGI(TAG, "Initializing WS2812 LED on GPIO %d", APP_PIN_LED);
    load_led_settings_from_nvs();
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
        apply_led_color(50, 20, 0); // Orange startup
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
    set_pa_enable(false); // Start Disabled (Anti-Hiss)
    // gpio_set_level((gpio_num_t)APP_PIN_PA_ENABLE, 1);
    // ESP_LOGI(TAG, "Power Amplifier enabled on GPIO %d (Anti-Pop Delay: %d ms)", APP_PIN_PA_ENABLE, APP_PA_ENABLE_DELAY_MS);
#else
    ESP_LOGW(TAG, "Power Amplifier control DISABLED via config");
#endif
    
    // Keep codec muted on boot; volume will be set during first playback
    audio_hal_set_volume(board_handle->audio_hal, 0);
    audio_hal_set_mute(board_handle->audio_hal, true);

    // --- 3. SD Card Peripheral ---
    ESP_LOGI(TAG, "Starting SD Card...");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    periph_sdcard_cfg_t sdcard_cfg = {
        .card_detect_pin = SD_CARD_INTR_GPIO,
        .root = "/sdcard",
        .mode = SD_MODE_SPI,
    };
    const int sd_max_attempts = 3;
    for (int attempt = 1; attempt <= sd_max_attempts; ++attempt) {
        esp_periph_handle_t sdcard_handle = periph_sdcard_init(&sdcard_cfg);
        esp_periph_start(set, sdcard_handle);

        int64_t sd_start_ms = esp_timer_get_time() / 1000;
        const int64_t sd_timeout_ms = 5000;
        while (!periph_sdcard_is_mounted(sdcard_handle)) {
            if ((esp_timer_get_time() / 1000) - sd_start_ms > sd_timeout_ms) {
                ESP_LOGW(TAG, "SD Card mount timeout (attempt %d/%d)", attempt, sd_max_attempts);
                g_sd_error = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (periph_sdcard_is_mounted(sdcard_handle)) {
            ESP_LOGI(TAG, "SD Card mounted successfully");
            g_sd_error = false;
            break;
        }

        esp_periph_stop(sdcard_handle);
        esp_periph_destroy(sdcard_handle);
        vTaskDelay(pdMS_TO_TICKS(500));
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
#if APP_ENABLE_TASK_WDT && APP_WDT_DIAG_LOG
        wdt_diag_check_loop_stall("app_main", &g_wdt_diag_last_main_loop_ms, &g_wdt_diag_last_main_warn_ms, APP_WDT_LOOP_WARN_MS);
        wdt_diag_heartbeat("app_main", &g_wdt_diag_last_main_heartbeat_ms);
#endif
#if APP_ENABLE_TASK_WDT
    if (g_wdt_main_registered) {
        esp_task_wdt_reset();
    }
#endif
        // --- Regular Tasks ---
        refresh_night_mode_from_schedule();
        // Check Daily Alarm
        if (TimeManager::checkAlarm()) {
             ESP_LOGI(TAG, "Daily Alarm Triggered! Starting Ring...");
             if (!g_alarm_state.active) {
                 g_alarm_state.active = true;
                 g_alarm_state.source = ALARM_DAILY;
                 g_snooze_active = false;
                 g_alarm_state.end_ms = (esp_timer_get_time() / 1000) + (int64_t)APP_DAILY_ALARM_LOOP_MINUTES * 60 * 1000;
                 g_alarm_state.retry_last_ms = 0;
                 
                 // Stop anything currently playing
                 pipeline_stop_and_reset(true, true);
                 
                 // Enable Base Speaker for Alarm
                 update_audio_output();

                 // Get specifics for today
                 struct tm now_tm = TimeManager::getCurrentTime();
                 DayAlarm today = TimeManager::getAlarm(now_tm.tm_wday);
                 g_alarm_state.msg_active = today.useRandomMsg;
                 
                 // Handle Fade
                 g_alarm_state.fade_active = today.volumeRamp;
                 if (g_alarm_state.fade_active) {
                     g_alarm_state.fade_start_time = esp_timer_get_time() / 1000;
                     g_alarm_state.fade_factor = APP_ALARM_FADE_MIN_FACTOR;
                 } else {
                     g_alarm_state.fade_factor = 1.0f;
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
                         g_alarm_state.current_file = fallback.empty() ? std::string(path) : fallback;
                     } else {
                         g_alarm_state.current_file = std::string(path);
                     }
                 } else {
                     std::string fallback = get_first_ringtone_path();
                    if (!fallback.empty()) {
                        ESP_LOGI(TAG, "Using first ringtone fallback: %s", fallback.c_str());
                    }
                     g_alarm_state.current_file = fallback.empty()
                         ? std::string("/sdcard/ringtones/") + APP_DEFAULT_TIMER_RINGTONE
                         : fallback;
                 }
                 play_file(g_alarm_state.current_file.c_str());
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
                    if (dial_buffer == APP_PB_NUM_REBOOT && phonebook.hasEntry(APP_PB_NUM_REBOOT)) {
                        process_phonebook_function(phonebook.getEntry(APP_PB_NUM_REBOOT));
                    } else if (dial_buffer.size() == 1) {
                        int digit = dial_buffer[0] - '0';
                        handle_voice_menu_digit(digit);
                    } else {
                        start_voice_queue({system_path("error_msg")});
                    }
                }
                // On-hook -> Timer mode (1-3 digits, up to 500 minutes)
                else if (!g_off_hook) {
                    if (dial_buffer == APP_PB_NUM_REBOOT && phonebook.hasEntry(APP_PB_NUM_REBOOT)) {
                        process_phonebook_function(phonebook.getEntry(APP_PB_NUM_REBOOT));
                    } else if (dial_buffer.size() >= 1 && dial_buffer.size() <= 3) {
                        int minutes = atoi(dial_buffer.c_str());
                        if (minutes >= APP_TIMER_MIN_MINUTES && minutes <= APP_TIMER_MAX_MINUTES) {
                            ESP_LOGI(TAG, "Timer set: %d minutes", minutes);
                            // Reset alarm file to default for kitchen timer
                            g_alarm_state.current_file = get_timer_ringtone_path();
                            g_snooze_active = false;

                            start_timer_minutes(minutes);
                            g_timer_state.announce_minutes = minutes;
                            g_timer_state.announce_pending = true;
                            g_timer_state.intro_playing = true;

                            // Force Base Speaker for announcement
                            g_pending_handset_restore = true;
                            // Dial flow uses off-hook state as handset intent.
                            // Restore target is captured before forced base output.
                            g_pending_handset_state = true; 
                            g_force_base_output = true;
                            update_audio_output();

                            play_file(system_path("timer_set").c_str());
                        } else {
                            ESP_LOGW(TAG, "Invalid timer value: %d", minutes);
                            std::vector<std::string> files;
                            files.push_back(system_path("timer_invalid"));
                            files.push_back(system_path("timer_max"));
                            add_number_audio(files, APP_TIMER_MAX_MINUTES);
                            files.push_back(system_path("minutes"));
                            start_voice_queue(files);
                        }
                    } else {
                        ESP_LOGW(TAG, "Timer requires 1-3 digits. Got: %s", dial_buffer.c_str());
                        std::vector<std::string> files;
                        files.push_back(system_path("timer_invalid"));
                        files.push_back(system_path("timer_max"));
                        add_number_audio(files, APP_TIMER_MAX_MINUTES);
                        files.push_back(system_path("minutes"));
                        start_voice_queue(files);
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
        if (g_night_mode_active && !g_night_mode_manual) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now >= g_night_mode_end_ms) {
                set_night_mode(false, false);
            }
        }

        // Alarm failover: stop if end time passed, retry if playback stalls
        if (g_alarm_state.active) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now >= g_alarm_state.end_ms) {
                TimeManager::stopAlarm();
                stop_playback();
                reset_alarm_state(true);
                update_audio_output();
            } else if (!is_playing && !g_alarm_state.current_file.empty()) {
                if (now - g_alarm_state.retry_last_ms >= APP_ALARM_RETRY_INTERVAL_MS) {
                    g_alarm_state.retry_last_ms = now;
                    play_file(g_alarm_state.current_file.c_str());
                }
            }
        }

        // Busy tone after 5s off-hook without dialing
        if (g_off_hook && !g_voice_menu_active && !g_line_busy && !g_any_digit_dialed && dial_buffer.empty()) {
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            if ((uint32_t)(now - g_off_hook_start_ms) > (uint32_t)APP_BUSY_TIMEOUT_MS) {
                ESP_LOGI(TAG, "Idle timeout -> busy tone");
                play_busy_tone();
            }
        }

        // Timer alarm check
        if (g_timer_state.active && !g_alarm_state.active) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now >= g_timer_state.end_ms) {
                ESP_LOGI(TAG, "Timer expired -> alarm");
                g_timer_state.active = false;
                g_snooze_active = false;
                play_timer_alarm();
            }
        }

        if (ret != ESP_OK) {
            continue;
        }
        
        // Handle Reboot Sequence (Allow pipeline to drain)
        if (g_reboot_pending) {
            int64_t now_ms = esp_timer_get_time() / 1000;
            // Reboot if not playing OR timeout (5s)
            if (!is_playing || (now_ms - g_reboot_request_time) > 5000) {
                 ESP_LOGW(TAG, "PERFORMING SYSTEM REBOOT NOW");
                 safe_reboot();
            }
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
                        play_file("/sdcard/system/system_ready_en.wav");
                        continue;
                    }
                    if (g_timer_state.intro_playing && g_timer_state.announce_pending) {
                        g_timer_state.intro_playing = false;
                        g_timer_state.announce_pending = false;
                        announce_timer_minutes(g_timer_state.announce_minutes);
                        continue;
                    }
                    if (g_voice_menu_reannounce && g_voice_menu_active && g_off_hook) {
                        g_voice_menu_reannounce = false;
                        vTaskDelay(pdMS_TO_TICKS(APP_VOICE_MENU_REANNOUNCE_DELAY_MS));
                        play_voice_menu_prompt();
                        continue;
                    }
                    if (g_force_base_output) {
                        g_force_base_output = false;
                        if (g_pending_handset_restore) {
                            g_output_mode_handset = g_pending_handset_state;
                            g_pending_handset_restore = false;
                        }
                        update_audio_output();
                    }
                    if (g_alarm_state.active) {
                        int64_t now = esp_timer_get_time() / 1000;
                        if (now < g_alarm_state.end_ms) {
                            play_file(g_alarm_state.current_file.c_str());
                        } else {
                            TimeManager::stopAlarm();
                            reset_alarm_state(true);
                            update_audio_output(); // Restore routing
                        }
                        continue;
                    }
                    if (g_persona_playback_active && g_off_hook) {
                        g_persona_playback_active = false;
                        ESP_LOGI(TAG, "Persona finished -> playing hangup click -> busy tone");
                        
                        // Play soft hangup click
                        play_file("/sdcard/system/hook_hangup.wav");
                        
                        // IMPORTANT: play_file is blocking or async? 
                        // In this loop context, play_file restarts the pipeline.
                        // Transition runs on the next FINISHED event.
                        // However, play_file sends a command and returns. 
                        // The loop will receive another FINISHED event for the hangup sound.
                        // Pending state marks handoff from hangup sound to busy tone.
                        g_persona_hangup_pending = true; 
                        continue;
                    }

                    if (g_persona_hangup_pending) {
                        g_persona_hangup_pending = false;
                        if (g_off_hook) {
                             vTaskDelay(pdMS_TO_TICKS(500)); // Short pause after click before busy tone
                             play_busy_tone();
                        }
                        continue;
                    }

                    if (g_line_busy && g_off_hook && !g_alarm_state.active) {
                        play_busy_tone();
                        continue;
                    }
                    
                    // Idle State reached - disable PA to prevent hiss
                    set_pa_enable(false);
                }
            }
        }
    }
}
