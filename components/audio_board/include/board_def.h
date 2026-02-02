/*
 * ESP32 Audio Board Definition for Ai-Thinker Audio Kit v2.2
 */
#ifndef _AUDIO_BOARD_DEFINITION_H_
#define _AUDIO_BOARD_DEFINITION_H_

/* SD card related */
#define SD_CARD_INTR_GPIO GPIO_NUM_NC ///< Disable Card Detect for stability
#define SD_CARD_INTR_SEL GPIO_SEL_NC
#define SD_CARD_OPEN_FILE_NUM_MAX 5

#define HEADPHONE_DETECT GPIO_NUM_5
#define PA_ENABLE_GPIO GPIO_NUM_21

// Added definitions for Ai-Thinker v2.2
#define BOARD_PA_GAIN (10)

/* SD Card Pins for Ai-Thinker v2.2 (1-Line Mode default) */
#define ESP_SD_PIN_CLK GPIO_NUM_14
#define ESP_SD_PIN_CMD GPIO_NUM_15
#define ESP_SD_PIN_D0  GPIO_NUM_2
#define ESP_SD_PIN_D1  GPIO_NUM_4
#define ESP_SD_PIN_D2  GPIO_NUM_12
#define ESP_SD_PIN_D3  GPIO_NUM_13 // CS in 1-line or SPI mode

#define GREEN_LED_GPIO GPIO_NUM_22
#define BLUE_LED_GPIO GPIO_NUM_19

#define BUTTON_REC_ID GPIO_NUM_36
#define BUTTON_MODE_ID GPIO_NUM_13

/* Touch pad related */
#define TOUCH_SEL_SET GPIO_SEL_19
#define TOUCH_SEL_PLAY GPIO_SEL_23
#define TOUCH_SEL_VOLUP GPIO_SEL_18
#define TOUCH_SEL_VOLDWN GPIO_SEL_5

#define TOUCH_SET GPIO_NUM_19
#define TOUCH_PLAY GPIO_NUM_23
#define TOUCH_VOLUP GPIO_NUM_18
#define TOUCH_VOLDWN GPIO_NUM_5

extern audio_hal_func_t AUDIO_CODEC_ES8388_DEFAULT_HANDLE;

#define AUDIO_CODEC_DEFAULT_CONFIG() {       \
    .adc_input = AUDIO_HAL_ADC_INPUT_LINE1,  \
    .dac_output = AUDIO_HAL_DAC_OUTPUT_ALL,  \
    .codec_mode = AUDIO_HAL_CODEC_MODE_BOTH, \
    .i2s_iface = {                           \
        .mode = AUDIO_HAL_MODE_SLAVE,        \
        .fmt = AUDIO_HAL_I2S_NORMAL,         \
        .samples = AUDIO_HAL_48K_SAMPLES,    \
        .bits = AUDIO_HAL_BIT_LENGTH_16BITS, \
    },                                       \
};

#endif
