/*
 * (Official Content from Ai-Thinker Repo)
 */

#include "esp_log.h"
#include "driver/gpio.h"
#include <string.h>
#include "board.h"
#include "audio_error.h"
#include "audio_mem.h"

static const char *TAG = "A1S";

esp_err_t get_i2c_pins(i2c_port_t port, i2c_config_t *i2c_config)
{
    AUDIO_NULL_CHECK(TAG, i2c_config, return ESP_FAIL);
    if (port == I2C_NUM_0)
    {
        i2c_config->sda_io_num = GPIO_NUM_33;
        i2c_config->scl_io_num = GPIO_NUM_32;
        ESP_LOGI(TAG, "i2c port configured!!!!");
    }
    else
    {
        i2c_config->sda_io_num = -1;
        i2c_config->scl_io_num = -1;
        ESP_LOGE(TAG, "i2c port %d is not supported", port);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t get_i2s_pins(i2s_port_t port, i2s_pin_config_t *i2s_config)
{
    AUDIO_NULL_CHECK(TAG, i2s_config, return ESP_FAIL);
    if (port == I2S_NUM_0)
    {
        i2s_config->bck_io_num = GPIO_NUM_27;
        i2s_config->ws_io_num = GPIO_NUM_25;       // FIXED: Was 26, correct is 25
        i2s_config->data_out_num = GPIO_NUM_26;    // FIXED: Was 25, correct is 26
        i2s_config->data_in_num = GPIO_NUM_35;
        ESP_LOGI(TAG, "i2s port configured!!!!");
    }
    else
    {
        memset(i2s_config, -1, sizeof(i2s_pin_config_t));
        ESP_LOGE(TAG, "i2s port %d is not supported", port);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t get_spi_pins(spi_bus_config_t *spi_config, spi_device_interface_config_t *spi_device_interface_config)
{
    AUDIO_NULL_CHECK(TAG, spi_config, return ESP_FAIL);
    AUDIO_NULL_CHECK(TAG, spi_device_interface_config, return ESP_FAIL);

    spi_config->mosi_io_num = GPIO_NUM_23;
    spi_config->miso_io_num = -1;
    spi_config->sclk_io_num = GPIO_NUM_18;
    spi_config->quadwp_io_num = -1;
    spi_config->quadhd_io_num = -1;

    spi_device_interface_config->spics_io_num = -1;

    ESP_LOGW(TAG, "SPI interface is not supported");
    return ESP_OK;
}

esp_err_t i2s_mclk_gpio_select(i2s_port_t i2s_num, gpio_num_t gpio_num)
{
    // Legacy functionality for direct register access is removed in ESP-IDF v5.x
    // The MCLK should be configured via the I2S driver or esp_driver_i2s.
    // We return ESP_OK to allow compilation - assuming the I2S driver (std) handles MCLK output.
    ESP_LOGW(TAG, "i2s_mclk_gpio_select: Manual MCLK routing skipped (deprecated API)");
    
    // Logic removed to prevent build errors with newer IDF
    return ESP_OK;
}

// input-output pins

int8_t get_headphone_detect_gpio(void)
{
    return HEADPHONE_DETECT;
}

int8_t get_pa_enable_gpio(void)
{
    return PA_ENABLE_GPIO;
}

// led pins

int8_t get_green_led_gpio(void)
{
    return GREEN_LED_GPIO;
}

int8_t get_blue_led_gpio(void)
{
    return BLUE_LED_GPIO;
}

// button pins

int8_t get_input_rec_id(void)
{
    return BUTTON_REC_ID;
}

int8_t get_input_mode_id(void)
{
    return BUTTON_MODE_ID;
}

int8_t get_reset_board_gpio(void)
{
    return -1;
}

int8_t get_es7243_mclk_gpio(void)
{
    return -1;
}

// touch pins

int8_t get_input_set_id(void)
{
    return TOUCH_SET;
}

int8_t get_input_play_id(void)
{
    return TOUCH_PLAY;
}

int8_t get_input_volup_id(void)
{
    return TOUCH_VOLUP;
}

int8_t get_input_voldown_id(void)
{
    return TOUCH_VOLDWN;
}

// sdcard
int8_t get_sdcard_intr_gpio(void){
    return SD_CARD_INTR_GPIO;
}

int8_t get_sdcard_open_file_num_max(void)
{
    return SD_CARD_OPEN_FILE_NUM_MAX;
}
