/*
 * ESP32 Audio Board Implementation for Ai-Thinker Audio Kit v2.2
 */
#include <string.h>
#include "esp_log.h"
#include "board.h"
// #include "audio_gpio.h"
#include "driver/gpio.h"
#include "es8388.h"
#include "audio_mem.h"

extern audio_hal_func_t AUDIO_CODEC_ES8388_DEFAULT_HANDLE;


static const char *TAG = "AUDIO_BOARD";

static audio_board_handle_t board_handle = 0;

audio_board_handle_t audio_board_init(void)
{
    if (board_handle) {
        ESP_LOGW(TAG, "The audio board has already been initialized!");
        return board_handle;
    }
    board_handle = (audio_board_handle_t) audio_calloc(1, sizeof(struct audio_board_handle));
    if (!board_handle) {
        ESP_LOGE(TAG, "Memory Allocation Failed!");
        return NULL;
    }
    
    // Initialize PA Enable Pin if defined
    if (PA_ENABLE_GPIO >= 0) {
        gpio_reset_pin((gpio_num_t)PA_ENABLE_GPIO);
        gpio_set_direction((gpio_num_t)PA_ENABLE_GPIO, GPIO_MODE_OUTPUT);
        // Start muted to avoid pop/noise during boot and reboot
        gpio_set_level((gpio_num_t)PA_ENABLE_GPIO, 0);
        ESP_LOGI(TAG, "PA Enable Pin (GPIO %d) set to LOW (Muted on boot)", PA_ENABLE_GPIO);
    }
    
    board_handle->audio_hal = audio_board_codec_init();
    return board_handle;
}

audio_hal_handle_t audio_board_codec_init(void)
{
    if (!board_handle) {
        ESP_LOGE(TAG, "Please Initialize Audio Board First");
        return NULL;
    }
    
    // Codec configuration
    audio_hal_codec_config_t audio_codec_cfg = AUDIO_CODEC_DEFAULT_CONFIG();
    
    // The Ai-Thinker v2.2 uses ES8388
    // Handle creation must match the active codec driver.
    // ADF must provide the 'es8388' component.
    // Standard ADF supports es8388.
    board_handle->audio_hal = audio_hal_init(&audio_codec_cfg, &AUDIO_CODEC_ES8388_DEFAULT_HANDLE);
    
    if (!board_handle->audio_hal) {
        ESP_LOGE(TAG, "Audio HAL init failed");
        return NULL;
    }
    
    // Start codec muted with zero volume to avoid boot/reboot pop or hiss
    audio_hal_set_volume(board_handle->audio_hal, 0);
    audio_hal_set_mute(board_handle->audio_hal, true);

    // No manual codec register writes here (restore minimal defaults from the 440Hz test)
    
    return board_handle->audio_hal;
}

audio_board_handle_t audio_board_get_handle(void)
{
    return board_handle;
}

esp_err_t audio_board_deinit(audio_board_handle_t audio_board)
{
    if (board_handle) {
        audio_hal_deinit(board_handle->audio_hal);
        free(board_handle);
        board_handle = NULL;
    }
    return ESP_OK;
}

esp_err_t audio_board_sdcard_init(esp_periph_set_handle_t set, periph_sdcard_mode_t mode)
{
    if (mode >= SD_MODE_4_LINE) {
        ESP_LOGE(TAG, "Ai-Thinker v2.2 typically uses 1-line SD mode due to pin conflicts");
        // 4-line mode remains available and emits a warning.
    }
    periph_sdcard_cfg_t sdcard_cfg = {
        .root = "/sdcard",
        .card_detect_pin = SD_CARD_INTR_GPIO,
        .mode = mode
    };
    esp_periph_handle_t sdcard_handle = periph_sdcard_init(&sdcard_cfg);
    return esp_periph_start(set, sdcard_handle);
}

esp_err_t audio_board_select_output(bool use_handset)
{
    // Sinus test baseline: no manual ES8388 register writes
    if (use_handset) {
        ESP_LOGI(TAG, "Audio Output: HANDSET (default codec routing)");
    } else {
        ESP_LOGI(TAG, "Audio Output: SPEAKER (default codec routing)");
    }
    
    return ESP_OK;
}
