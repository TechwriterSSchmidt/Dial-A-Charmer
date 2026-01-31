#ifndef CONFIG_H
#define CONFIG_H

#define FIRMWARE_VERSION "v0.7.2-beta"

// Target: Ai-Thinker Audio Kit V2.2 (ES8388)

// I2S for Audio Output (Internal DAC/Codec)
#define CONF_I2S_BCLK       27
#define CONF_I2S_LRC        25  
#define CONF_I2S_DOUT       26  

// I2C for Internal Codec (ES8388) Control
#define CONF_I2C_CODEC_SDA  33
#define CONF_I2C_CODEC_SCL  32
#define HAS_ES8388_CODEC    true

// External I2C (RTC) - disabled to avoid PA noise on IO21
#define CONF_I2C_SDA        -1 
#define CONF_I2C_SCL        -1 
#define CONF_RTC_USE_WIRE1  false 

// Rotary Dial & Inputs - Header P2
#define CONF_PIN_DIAL_PULSE 5   
#define CONF_PIN_DIAL_MODE  23  // Key 4 - Dial State Contact (temporary test)
#define CONF_PIN_HOOK       19  
#define CONF_PIN_EXTRA_BTN  18  
#define CONF_DIAL_MODE_ACTIVE_LOW true
#define CONF_DIAL_PULSE_ACTIVE_LOW true
#define CONF_HOOK_ACTIVE_LOW  true // Floating = HIGH = On Hook (Idle). Low = Off Hook (Active)

// Status LED (WS2812) - Header P2 (GPIO 23 used as Output, ignore Key 4)
#define CONF_PIN_LED        -1  // Disabled to free GPIO23 for dial mode
#define CONF_AC_EXC_LED_NUM 1 

// Features Disabled/Changed
#define CONF_PIN_BATTERY    -1  // Disabled
#define CONF_PIN_PA_ENABLE  21  // Enable Power Amplifier

// SD Card (Onboard Slot - HSPI)
#define CONF_PIN_SD_CS      13  
// MOSI=15, MISO=2, CLK=14 default for HSPI

// 2. Audio Input (Mic)
// Reserved for future use (Voice Control)
// #define CONF_PIN_MIC_ANALOG 36

// --- System Configuration ---
#define CONF_SERIAL_BAUD    115200
#define CONF_AUDIO_VOLUME   15  

// --- Rotary Dial Timing (ms) ---
#define CONF_DIAL_DEBOUNCE_PULSE 50
#define CONF_DIAL_TIMEOUT        700
#define CONF_HOOK_DEBOUNCE       50
#define CONF_BTN_DEBOUNCE        50

// --- Power Management ---
#define CONF_SLEEP_TIMEOUT_MS    60000 // 60 Seconds Idle before Deep Sleep

// --- Persistence ---
#define CONF_PREFS_NS       "charmer"
#define CONF_AP_SSID        "Dial-A-Charmer"
#define CONF_DNS_PORT       53
#define CONF_DEFAULT_TZ     1
#define CONF_DEFAULT_VOL    30
#define CONF_DEFAULT_RING   1

// --- Debugging ---
#define DEBUG_AUDIO         1

#endif
