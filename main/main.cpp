#include <string.h>
#include <string>
#include "esp_timer.h"
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
#include "wav_decoder.h"

// App Includes
#include "app_config.h"
#include "RotaryDial.h"
#include "PhonebookManager.h"

static const char *TAG = "DIAL_A_CHARMER_ESP";

// Global Objects
RotaryDial dial(APP_PIN_DIAL_PULSE, APP_PIN_HOOK, APP_PIN_EXTRA_BTN, APP_PIN_DIAL_MODE);
audio_pipeline_handle_t pipeline;
audio_element_handle_t fatfs_stream, wav_decoder, i2s_writer;

// Logic State
std::string dial_buffer = "";
int64_t last_digit_time = 0;
const int DIAL_TIMEOUT_MS = 2000;
bool is_playing = false;

// Helpers
void play_file(const char* path) {
    ESP_LOGI(TAG, "Requesting playback: %s", path);
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_reset_ringbuffer(pipeline);
    audio_pipeline_reset_items_state(pipeline);
    audio_element_set_uri(fatfs_stream, path);
    audio_pipeline_run(pipeline);
    is_playing = true;
}

void stop_playback() {
    if (is_playing) {
        audio_pipeline_stop(pipeline);
        audio_pipeline_wait_for_stop(pipeline);
        is_playing = false;
    }
}

// Callbacks
void on_dial_complete(int number) {
    ESP_LOGI(TAG, "--- DIALED DIGIT: %d ---", number);
    dial_buffer += std::to_string(number);
    last_digit_time = esp_timer_get_time() / 1000; // Update timestamp (ms)
}

void on_hook_change(bool off_hook) {
    ESP_LOGI(TAG, "--- HOOK STATE: %s ---", off_hook ? "OFF HOOK (Active/Pickup)" : "ON HOOK (Idle/Hangup)");
    if (off_hook) {
        // Receiver Picked Up
        dial_buffer = "";
        // Play dial tone
        play_file("/sdcard/system/dialtone_1.wav");
    } else {
        // Receiver Hung Up
        stop_playback();
        dial_buffer = "";
    }
}

// Input Task
void input_task(void *pvParameters) {
    ESP_LOGI(TAG, "Input Task Started");
    while(1) {
        dial.loop();
        vTaskDelay(pdMS_TO_TICKS(10)); // Poll every 10ms
    }
}

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_LOGI(TAG, "Initializing Dial-A-Charmer (ESP-ADF version)...");

    // --- 1. Input Initialization ---
    ESP_LOGI(TAG, "Initializing Rotary Dial...");
    dial.begin();
    dial.onDialComplete(on_dial_complete);
    dial.onHookChange(on_hook_change);
    
    // Start Input Task
    xTaskCreate(input_task, "input_task", 4096, &dial, 5, NULL);

    // --- 2. Audio Board Init ---
    ESP_LOGI(TAG, "Starting Audio Board...");
    audio_board_handle_t board_handle = audio_board_init();
    
    // Enable Power Amplifier (GPIO21)
    gpio_set_level((gpio_num_t)APP_PIN_PA_ENABLE, 1);
    ESP_LOGI(TAG, "Power Amplifier enabled on GPIO %d", APP_PIN_PA_ENABLE);
    
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

    // Initialize Phonebook (Now that SD is ready)
    phonebook.begin();

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

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.stack_in_ext = false; // Use internal RAM
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO; // Duplicate Mono to Stereo
    i2s_writer = i2s_stream_init(&i2s_cfg);

    audio_pipeline_register(pipeline, fatfs_stream, "file");
    audio_pipeline_register(pipeline, wav_decoder, "wav");
    audio_pipeline_register(pipeline, i2s_writer, "i2s");

    const char *link_tag[3] = {"file", "wav", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    // Event Config
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline, evt);

    // Start Codec
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

    // Play Startup Sound
    ESP_LOGI(TAG, "Playing Startup Sound...");
    audio_element_set_uri(fatfs_stream, "/sdcard/system/system_ready_en.wav");
    audio_pipeline_run(pipeline);

    // --- 5. Main Event Loop ---
    while (1) {
        audio_event_iface_msg_t msg;
        
        // Listen with 100ms timeout to allow polling logic
        esp_err_t ret = audio_event_iface_listen(evt, &msg, 100 / portTICK_PERIOD_MS);

        // --- Logic: Check Dial Timeout ---
        if (!dial_buffer.empty()) {
            int64_t now = esp_timer_get_time() / 1000;
            if ((now - last_digit_time) > DIAL_TIMEOUT_MS) {
                ESP_LOGI(TAG, "Dial Timeout. Processing Number: %s", dial_buffer.c_str());
                
                // Lookup
                if (phonebook.hasEntry(dial_buffer)) {
                     PhonebookEntry entry = phonebook.getEntry(dial_buffer);
                     ESP_LOGI(TAG, "Playing Entry: %s", entry.name.c_str());
                     
                     // If value starts with "/", treat as file.
                     if (entry.value.rfind("/", 0) == 0) {
                         play_file(entry.value.c_str());
                     } else {
                         // Fallback check: is it a special command?
                         // For now, assume anything else is a file path too.
                         play_file(entry.value.c_str());
                     }
                } else {
                    ESP_LOGI(TAG, "Number %s not found.", dial_buffer.c_str());
                    play_file("/sdcard/system/call_terminated.wav");
                }
                
                dial_buffer = ""; // Reset buffer
            }
        }

        if (ret != ESP_OK) {
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT) {
            // Handle Music Info (Sample Rate)
            if (msg.source == (void *)wav_decoder && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                audio_element_info_t music_info = {0};
                audio_element_getinfo(wav_decoder, &music_info);
                ESP_LOGI(TAG, "WAV info: rate=%d, ch=%d, bits=%d", 
                    music_info.sample_rates, music_info.channels, music_info.bits);
                i2s_stream_set_clk(i2s_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            }
            
            // Handle Stop
            if (msg.source == (void *)i2s_writer && msg.cmd == AEL_MSG_CMD_REPORT_STATUS) {
                if ((int)msg.data == AEL_STATUS_STATE_FINISHED) {
                    ESP_LOGI(TAG, "Audio Finished.");
                    stop_playback();
                }
            }
        }
    }
}
