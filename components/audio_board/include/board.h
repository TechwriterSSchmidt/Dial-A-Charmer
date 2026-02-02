/*
 * ESP32 Audio Board public interface for Ai-Thinker Audio Kit v2.2
 */
#ifndef _AUDIO_BOARD_H_
#define _AUDIO_BOARD_H_

#include "audio_hal.h"
#include "board_def.h"
#include "board_pins_config.h"
#include "esp_peripherals.h"
#include "periph_sdcard.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio board handle
 */
struct audio_board_handle {
    audio_hal_handle_t audio_hal;
};

typedef struct audio_board_handle *audio_board_handle_t;

/**
 * @brief Initialize the audio board
 * @return The audio board handle
 */
audio_board_handle_t audio_board_init(void);

/**
 * @brief Initialize the SD card
 * 
 * @param set The peripheral set handle
 * @param mode The SD card mode (1-line or 4-line)
 * @return ESP_OK on success
 */
esp_err_t audio_board_sdcard_init(esp_periph_set_handle_t set, periph_sdcard_mode_t mode);

/**
 * @brief Get the audio HAL handle
 * @return The audio HAL handle
 */
audio_hal_handle_t audio_board_codec_init(void);

/**
 * @brief Get the audio board handle
 * @return The audio board handle
 */
audio_board_handle_t audio_board_get_handle(void);

/**
 * @brief Deinitialize the audio board
 * @param audio_board The audio board handle
 * @return ESP_OK
 */
esp_err_t audio_board_deinit(audio_board_handle_t audio_board);

#ifdef __cplusplus
}
#endif

#endif
