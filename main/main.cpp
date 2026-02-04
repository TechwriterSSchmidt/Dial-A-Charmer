#include <string.h>
#include <string>
#include <vector>
#include <dirent.h>
#include <stdlib.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "board.h"
#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
// #include "driver/i2s.h" // Removed to avoid CONFLICT with legacy driver used by ADF
#include "wav_decoder.h"

// App Includes
#include "app_config.h"
#include "RotaryDial.h"
#include "PhonebookManager.h"
#include "WebManager.h"
#include "TimeManager.h" // Added TimeManager

static const char *TAG = "DIAL_A_CHARMER_ESP";

// Global Objects
RotaryDial dial(APP_PIN_DIAL_PULSE, APP_PIN_HOOK, APP_PIN_EXTRA_BTN, APP_PIN_DIAL_MODE);
audio_pipeline_handle_t pipeline;
audio_element_handle_t fatfs_stream, wav_decoder, i2s_writer, gain_element;
audio_board_handle_t board_handle = NULL; // Globale Reference

// Logic State
std::string dial_buffer = "";
int64_t last_digit_time = 0;
const int DIAL_TIMEOUT_MS = APP_DIAL_TIMEOUT_MS;
const int PERSONA_PAUSE_MS = APP_PERSONA_PAUSE_MS;
const int DIALTONE_SILENCE_MS = APP_DIALTONE_SILENCE_MS;
bool is_playing = false;
bool g_output_mode_handset = false; // False = Speaker, True = Handset
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
bool g_timer_alarm_active = false;
int64_t g_timer_alarm_end_ms = 0;
std::string g_current_alarm_file = "/sdcard/ringtones/digital_alarm.wav";

// Helpers
void stop_playback(); // forward decl
void play_file(const char* path); // forward decl
static void start_timer_minutes(int minutes) {
    // Override any existing timer or pending timer announcements
    g_timer_active = true;
    g_timer_minutes = minutes;
    g_timer_end_ms = (esp_timer_get_time() / 1000) + (int64_t)minutes * 60 * 1000;
    g_timer_announce_pending = false;
    g_timer_intro_playing = false;
}

static void announce_timer_minutes(int minutes) {
    char path[128];
    if (minutes < 10) {
        snprintf(path, sizeof(path), "/sdcard/time/de/m_0%d.wav", minutes);
    } else {
        snprintf(path, sizeof(path), "/sdcard/time/de/m_%d.wav", minutes);
    }
    play_file(path);
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
// Alarm Fade Logic
static bool g_alarm_fade_active = false;
static float g_alarm_fade_factor = 1.0f; 
static int64_t g_alarm_fade_start_time = 0;
const int64_t ALARM_FADE_DURATION_MS = 60000; // 60 Seconds ramp up

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
    if (g_timer_alarm_active || g_timer_announce_pending) {
        effective_handset = false; 
    }

    // Re-apply the current output selection
    audio_board_select_output(effective_handset);

    // Update Volume based on EFFECTIVE mode
    nvs_handle_t my_handle;
    uint8_t vol = 60; // Default Base Speaker
    
    if (nvs_open("dialcharm", NVS_READONLY, &my_handle) == ESP_OK) {
        if (effective_handset) {
            if (nvs_get_u8(my_handle, "volume_handset", &vol) != ESP_OK) vol = 60;
        } else {
            if (nvs_get_u8(my_handle, "volume", &vol) != ESP_OK) vol = 60;
        }
        nvs_close(my_handle);
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
    g_timer_alarm_active = true;
    g_timer_alarm_end_ms = (esp_timer_get_time() / 1000) + (int64_t)APP_TIMER_ALARM_LOOP_MINUTES * 60 * 1000;
    stop_playback();
    update_audio_output(); // Force Base Speaker
    // Play the currently selected alarm (Daily or Default)
    if (g_current_alarm_file.empty()) {
        g_current_alarm_file = "/sdcard/ringtones/digital_alarm.wav";
    }
    play_file(g_current_alarm_file.c_str());
}

static int get_snooze_minutes() {
    nvs_handle_t my_handle;
    int32_t val = 5; // Default
    if (nvs_open("dialcharm", NVS_READONLY, &my_handle) == ESP_OK) {
        nvs_get_i32(my_handle, "snooze_min", &val);
        nvs_close(my_handle);
    }
    if (val < 1) val = 1;
    if (val > 60) val = 60;
    return (int)val;
}


void process_phonebook_function(PhonebookEntry entry) {
    if (entry.type == "FUNCTION") {
        if (entry.value == "COMPLIMENT_CAT") {
            // Parameter is "1", "2", etc. Map to persona_0X
            std::string folder = "/sdcard/persona_0" + entry.parameter + "/de";
            std::string file = get_random_file(folder);
            if (!file.empty()) {
                wait_for_dialtone_silence_if_needed();
                g_persona_playback_active = true;
                play_file(file.c_str());
            }
            else play_file("/sdcard/system/error_msg_de.wav");
        }
        else if (entry.value == "COMPLIMENT_MIX") {
            // Pick random persona 1-5
            int persona = (esp_random() % 5) + 1;
            std::string folder = "/sdcard/persona_0" + std::to_string(persona) + "/de";
            std::string file = get_random_file(folder);
            if (!file.empty()) {
                wait_for_dialtone_silence_if_needed();
                g_persona_playback_active = true;
                play_file(file.c_str());
            }
        }
        else if (entry.value == "ANNOUNCE_TIME") {
            play_file("/sdcard/system/time_unavailable_de.wav");
        }
        else if (entry.value == "REBOOT") {
             ESP_LOGW(TAG, "Rebooting system...");
             play_file("/sdcard/system/system_ready_de.wav"); // Ack before reboot
             vTaskDelay(pdMS_TO_TICKS(2000));
             esp_restart();
        }
        else if (entry.value == "VOICE_MENU") {
            play_file("/sdcard/system/menu_de.wav");
        }
        else {
            ESP_LOGW(TAG, "Unknown Function: %s", entry.value.c_str());
            play_file("/sdcard/system/error_msg_de.wav");
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
        audio_pipeline_stop(pipeline);
        audio_pipeline_wait_for_stop(pipeline);
        is_playing = false;
        g_last_playback_finished_ms = esp_timer_get_time() / 1000;
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
    if (g_timer_alarm_active) {
        ESP_LOGI(TAG, "Snoozing Alarm via Key 5");
        TimeManager::stopAlarm();
        g_timer_alarm_active = false;
        stop_playback();        update_audio_output(); // Restore audio routing        
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
        dial_buffer = "";
        g_line_busy = false;
        g_persona_playback_active = false;
        g_any_digit_dialed = false;
        g_off_hook_start_ms = esp_timer_get_time() / 1000;
        if (g_timer_alarm_active) {
            TimeManager::stopAlarm();
            g_timer_alarm_active = false;
            update_audio_output(); // Restore audio routing (e.g. back to handset if off-hook)
        }
        // Play dial tone
        play_file("/sdcard/system/dialtone_1.wav");
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
        if (g_timer_alarm_active && g_alarm_fade_active) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now < g_alarm_fade_start_time) g_alarm_fade_start_time = now;
            
            int64_t elapsed = now - g_alarm_fade_start_time;
            if (elapsed < 0) elapsed = 0;
            if (elapsed > ALARM_FADE_DURATION_MS) {
                g_alarm_fade_factor = 1.0f;
            } else {
                g_alarm_fade_factor = (float)elapsed / (float)ALARM_FADE_DURATION_MS;
            }
            // Ensure strictly minimum volume so it's audible
            if (g_alarm_fade_factor < 0.05f) g_alarm_fade_factor = 0.05f;
            
            target_left *= g_alarm_fade_factor;
            target_right *= g_alarm_fade_factor;
        }

        g_gain_left_target = target_left;
        g_gain_right_target = target_right;
        
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

// Helper to get WAV sample rate manually
static int get_wav_sample_rate(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 44100; // Default if open fails
    
    uint8_t header[28];
    if (fread(header, 1, 28, f) < 28) {
        fclose(f);
        return 44100;
    }
    fclose(f);
    
    // Offset 24 is Sample Rate (4 bytes little endian)
    int sample_rate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    
    // Sanity check
    if (sample_rate < 8000 || sample_rate > 96000) return 44100;
    
    return sample_rate;
}

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_LOGI(TAG, "Initializing Dial-A-Charmer (ESP-ADF version)...");

    #if defined(ENABLE_SYSTEM_MONITOR) && (ENABLE_SYSTEM_MONITOR == 1)
    xTaskCreate(monitor_task, "monitor_task", 2048, NULL, 1, NULL);
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
    
    audio_hal_set_volume(board_handle->audio_hal, 60); // Set volume

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
    
    while (!periph_sdcard_is_mounted(sdcard_handle)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "SD Card mounted successfully");

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
    pipeline = audio_pipeline_init(&pipeline_cfg);

    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_stream = fatfs_stream_init(&fatfs_cfg);

    wav_decoder_cfg_t wav_cfg = DEFAULT_WAV_DECODER_CONFIG();
    wav_cfg.stack_in_ext = false; // Disable PSRAM stack to avoid FreeRTOS patch requirement
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

    // Initialize TimeManager (SNTP & RTC) before audio starts to avoid I2C/CPU contention
    TimeManager::init();

    // Play Startup Sound
    ESP_LOGI(TAG, "Playing Startup Sound...");

    int start_rate = get_wav_sample_rate("/sdcard/system/system_ready_en.wav");
    ESP_LOGI(TAG, "Pre-detected startup sample rate: %d", start_rate);
    
    // Explicitly set I2S/Codec to correct rate
    
    // 1. Update Element Info so the open() or subsequent logic knows we are at 22k/44k
    audio_element_info_t i2s_info = {};
    audio_element_getinfo(i2s_writer, &i2s_info);
    i2s_info.sample_rates = start_rate;
    audio_element_setinfo(i2s_writer, &i2s_info);

    // 2. Configure Hardware
    i2s_stream_set_clk(i2s_writer, start_rate, 16, 2);
    
    // 3. Force Codec Resync
    // The ES8388 PLL needs time to lock onto the MCLK/BCLK from I2s.
    // We restart the codec to force a re-lock, enabling the output.
    if (board_handle && board_handle->audio_hal) {
         audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_STOP);
         audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
         
         // Start I2S clock (BCLK) immediately
         // i2s_start((i2s_port_t)0); // Removed to avoid CONFLICT

         // Wait for PLL lock (Increased to 600ms as 300ms was insufficient)
         vTaskDelay(pdMS_TO_TICKS(600)); 
    }
    
    // 3. Pre-roll Silence (Anti-Chipmunk)
    // Send a tiny buffer of silence to flush the DMA and force clock sync before real audio starts
    // NOTE: This usually requires the pipeline to be running, but we can try to "prime" the codec.
    // Given ADF limitations, we will rely on the longer delay and order.
    
    audio_element_set_uri(fatfs_stream, "/sdcard/system/system_ready_en.wav");
    audio_pipeline_run(pipeline);
    
    // --- Start Input Task Last ---
    // Start Input Task with Higher Priority (10) to avoid starvation by Audio
    // Started here to ensure 'pipeline' is initialized before any callback tries to stop it
    xTaskCreate(input_task, "input_task", 4096, &dial, 10, NULL);

    // --- 5. Main Event Loop ---
    while (1) {
        // --- Regular Tasks ---
        // Check Daily Alarm
        if (TimeManager::checkAlarm()) {
             ESP_LOGI(TAG, "Daily Alarm Triggered! Starting Ring...");
             if (!g_timer_alarm_active) {
                 // Reuse the timer alarm logic for simplicity
                 g_timer_alarm_active = true;
                 g_timer_alarm_end_ms = (esp_timer_get_time() / 1000) + (60 * 1000); // Ring for 60s
                 
                 // Stop anything currently playing
                 audio_pipeline_stop(pipeline);
                 audio_pipeline_wait_for_stop(pipeline);
                 audio_pipeline_terminate(pipeline);
                 
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

                 char path[128];
                 if (!today.ringtone.empty()) {
                     snprintf(path, sizeof(path), "/sdcard/ringtones/%s", today.ringtone.c_str());
                 } else {
                     snprintf(path, sizeof(path), "/sdcard/ringtones/digital_alarm.wav");
                 }
                 
                 g_current_alarm_file = std::string(path);
                 play_file(g_current_alarm_file.c_str());
             }
        }

        audio_event_iface_msg_t msg;
        
        // Listen with 100ms timeout to allow polling logic
        esp_err_t ret = audio_event_iface_listen(evt, &msg, 100 / portTICK_PERIOD_MS);

        // --- Logic: Check Dial Timeout ---
        if (!dial_buffer.empty()) {
            int64_t now = esp_timer_get_time() / 1000;
            if ((now - last_digit_time) > DIAL_TIMEOUT_MS) {
                ESP_LOGI(TAG, "Dial Timeout. Processing Number: %s", dial_buffer.c_str());
                
                // On-hook -> Timer mode (1-3 digits, up to 500 minutes)
                if (!g_off_hook) {
                    if (dial_buffer.size() >= 1 && dial_buffer.size() <= 3) {
                        int minutes = atoi(dial_buffer.c_str());
                        if (minutes >= 1 && minutes <= 500) {
                            ESP_LOGI(TAG, "Timer set: %d minutes", minutes);
                            // Reset alarm file to default for kitchen timer
                            g_current_alarm_file = "/sdcard/ringtones/digital_alarm.wav";
                            
                            start_timer_minutes(minutes);
                            g_timer_announce_minutes = minutes;
                            g_timer_announce_pending = true;
                            g_timer_intro_playing = true;
                            play_file("/sdcard/system/timer_set_de.wav");
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
                        play_file("/sdcard/system/call_terminated.wav");
                    }
                }
                
                dial_buffer = ""; // Reset buffer
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
        if (g_timer_active && !g_timer_alarm_active) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now >= g_timer_end_ms) {
                ESP_LOGI(TAG, "Timer expired -> alarm");
                g_timer_active = false;
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
                
                // Check if we need to update I2S to avoid glitches if already correct
                audio_element_info_t i2s_info = {};
                audio_element_getinfo(i2s_writer, &i2s_info);
                
                // Only reconfigure if rate differs significantly
                if (i2s_info.sample_rates != music_info.sample_rates) {
                    ESP_LOGI(TAG, "WAV info Update: rate=%d, ch=%d (out_ch=%d)", music_info.sample_rates, music_info.channels, out_channels);
                    
                    // Update I2S Element Info so next getinfo returns new rate
                    audio_element_setinfo(i2s_writer, &music_info); 
                    
                    // Configure I2S Clock
                    i2s_stream_set_clk(i2s_writer, music_info.sample_rates, music_info.bits, out_channels);
                    
                    // Restart Codec (Fix for fast playback on A1S)
                    if (board_handle && board_handle->audio_hal) {
                        audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_STOP);
                        audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
                    }
                } else {
                     ESP_LOGI(TAG, "WAV info match (%d Hz) - Skipping reconfig", music_info.sample_rates);
                }
                
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
                    if (g_timer_intro_playing && g_timer_announce_pending) {
                        g_timer_intro_playing = false;
                        g_timer_announce_pending = false;
                        announce_timer_minutes(g_timer_announce_minutes);
                        continue;
                    }
                    if (g_timer_alarm_active) {
                        int64_t now = esp_timer_get_time() / 1000;
                        if (now < g_timer_alarm_end_ms) {
                            play_file(g_current_alarm_file.c_str());
                        } else {
                            TimeManager::stopAlarm();
                            g_timer_alarm_active = false;
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
                    if (g_line_busy && g_off_hook && !g_timer_alarm_active) {
                        play_busy_tone();
                    }
                }
            }
        }
    }
}
