/*
 * ESP32 Audio Board Pins Config
 */
#ifndef _BOARD_PINS_CONFIG_H_
#define _BOARD_PINS_CONFIG_H_

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_types.h"
#include "driver/i2s_types_legacy.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "driver/spi_slave.h"

#ifdef __cplusplus
extern "C" {
#endif

// Define board_i2s_pin_t for backward compatibility or ADF requirements
// If ADF expects it to be i2s_pin_config_t compatible
typedef i2s_pin_config_t board_i2s_pin_t;

esp_err_t get_i2c_pins(i2c_port_t port, i2c_config_t *i2c_cfg);
esp_err_t get_i2s_pins(i2s_port_t port, i2s_pin_config_t *i2s_config);
esp_err_t i2s_mclk_gpio_select(i2s_port_t i2s_num, gpio_num_t gpio_num);
esp_err_t get_spi_pins(spi_bus_config_t *spi_config, spi_device_interface_config_t *spi_device_interface_config);

int8_t get_headphone_detect_gpio(void);
int8_t get_pa_enable_gpio(void);

int8_t get_green_led_gpio(void);
int8_t get_blue_led_gpio(void);

int8_t get_input_rec_id(void);
int8_t get_input_mode_id(void);
int8_t get_input_set_id(void);
int8_t get_input_play_id(void);
int8_t get_input_volup_id(void);
int8_t get_input_voldown_id(void);

int8_t get_sdcard_intr_gpio(void);
int8_t get_sdcard_open_file_num_max(void);

// Fix for implicit declaration errors in ADF drivers
int8_t get_reset_board_gpio(void);
int8_t get_es7243_mclk_gpio(void);

#ifdef __cplusplus
}
#endif

#endif
