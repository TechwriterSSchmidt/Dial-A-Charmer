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
        gpio_set_level((gpio_num_t)PA_ENABLE_GPIO, 0); // Default OFF
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
    // We need to match the handle creation to the driver being used.
    // Ensure 'es8388' component is available in ADF or we need to add it.
    // Standard ADF supports es8388.
    board_handle->audio_hal = audio_hal_init(&audio_codec_cfg, &AUDIO_CODEC_ES8388_DEFAULT_HANDLE);
    
    if (!board_handle->audio_hal) {
        ESP_LOGE(TAG, "Audio HAL init failed");
        return NULL;
    }
    
    // Enable PA after codec init if needed, or controlled by application
    // audio_gpio_set_pa_gpio(PA_ENABLE_GPIO, true); // Optionally turn on here
    
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
        // We can allow 4-line if user insists, but warn them.
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
    // Register 0x1D (29): DAC Control 26
    // Bit 2: DAC Left Mute (1=Mute, 0=Normal)
    // Bit 1: DAC Right Mute (1=Mute, 0=Normal)
    // We want to MUTE the UNUSED channel.
    
    uint8_t reg_addr = 0x1D; 
    uint8_t val = 0;
    
    if (use_handset) {
        // Handset: Use Left (Lout). Mute Right.
        val = 0x02; // Bit 1 set
        ESP_LOGI(TAG, "Audio Output: HANDSET (Lout)");
    } else {
        // Base: Use Right (Rout). Mute Left.
        val = 0x04; // Bit 2 set
        ESP_LOGI(TAG, "Audio Output: SPEAKER (Rout)");
    }
    
    // Attempt to use es8388_write_reg directly
    // If this fails specifically during link, it means the driver is hidden.
    return es8388_write_reg(reg_addr, val);
}
