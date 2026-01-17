#ifndef CONFIG_H
#define CONFIG_H

// --- Hardware Pins (New Architecture: PCM5100A + MAX9814) ---

// 1. Audio Output (Via PCM5100A DAC)
// Uses I2S_NUM_1 (Swapped to avoid conflict with Internal ADC)
#define CONF_I2S_BCLK       26
#define CONF_I2S_LRC        25  // WS
#define CONF_I2S_DOUT       27  // ESP DOUT -> Codec DIN
// Note: MCLK is NOT used (Generated internally by PCM5100A)

// 2. Audio Input (Via MAX9814 Analog Preamp)
// Uses I2S_NUM_0 (Required for ADC Built-In Mode)
#define CONF_PIN_MIC_ANALOG 36  // VP / ADC1_CH0

// 3. I2C (Only if needed for Port Extenders/Sensors, NOT for Audio anymore)
#define CONF_I2C_SDA        21
#define CONF_I2C_SCL        22 

// Vibration Motor
#define CONF_PIN_VIB_MOTOR  2

// GNSS (M10)
#define CONF_GPS_RX         34  // Input Only
#define CONF_GPS_TX         -1  // Disabled
#define CONF_GPS_BAUD       9600

// Battery Monitoring
#define CONF_PIN_BATTERY    35

// Rotary Dial & Inputs
#define CONF_PIN_DIAL_PULSE 5   
#define CONF_PIN_HOOK       32
#define CONF_PIN_EXTRA_BTN  33
#define CONF_PIN_DIAL_MODE  -1  // Disabled (Using Pulse Count Timeout)
#define CONF_DIAL_MODE_ACTIVE_LOW true

// Status LED (WS2812)
#define CONF_PIN_LED        13
#define CONF_AC_EXC_LED_NUM 1 

// SD Card
#define CONF_PIN_SD_CS      4

// --- System Configuration ---
#define CONF_SERIAL_BAUD    115200
#define CONF_AUDIO_VOLUME   15  

// --- Rotary Dial Timing (ms) ---
#define CONF_DIAL_DEBOUNCE_PULSE 30
// CONF_DIAL_TIMEOUT is used if PIN_DIAL_MODE is -1 or fails
#define CONF_DIAL_TIMEOUT        500
// #define CONF_DIAL_FINISHED_TIMEOUT 2500 // Disabled per user request (using contact)
#define CONF_HOOK_DEBOUNCE       50
#define CONF_BTN_DEBOUNCE        50

// --- Power Management ---
#define CONF_SLEEP_TIMEOUT_MS    60000 // 60 Seconds Idle before Deep Sleep

// --- Web / Network ---
#define CONF_AP_SSID        "Dial-A-Charmer"
#define CONF_AP_IP          IPAddress(192, 168, 4, 1)
#define CONF_DNS_PORT       53

// --- Persistence ---
#define CONF_PREFS_NS       "charmer"
#define CONF_DEFAULT_TZ     1
#define CONF_DEFAULT_VOL    30 // Scale 0-42
#define CONF_DEFAULT_RING   1

#endif
