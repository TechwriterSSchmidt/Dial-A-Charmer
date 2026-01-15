#ifndef CONFIG_H
#define CONFIG_H

// --- Hardware Pins ---
// Audio (I2S - MAX98357A / PCM5102)
#define CONF_I2S_BCLK       26
#define CONF_I2S_LRC        25
#define CONF_I2S_DOUT       22

// GNSS (M10 or similar)
#define CONF_GPS_RX         16
#define CONF_GPS_TX         17
#define CONF_GPS_BAUD       9600

// Rotary Dial & Inputs
#define CONF_PIN_DIAL_PULSE 34
#define CONF_PIN_HOOK       32
#define CONF_PIN_EXTRA_BTN  33

// Status LED (WS2812)
#define CONF_PIN_LED        13
#define CONF_AC_EXC_LED_NUM 1 // Atomic Charmer Exclusive LED Number

// SD Card (VSPI Default on Lolin D32 Pro)
#define CONF_PIN_SD_CS      4

// --- System Configuration ---
#define CONF_SERIAL_BAUD    115200
#define CONF_AUDIO_VOLUME   15  // Default volume (0-21)

// --- Rotary Dial Timing (ms) ---
#define CONF_DIAL_DEBOUNCE_PULSE 30
#define CONF_DIAL_TIMEOUT        500
#define CONF_HOOK_DEBOUNCE       50
#define CONF_BTN_DEBOUNCE        50

// --- Web / Network ---
#define CONF_AP_SSID        "AtomicCharmer"
#define CONF_AP_IP          IPAddress(192, 168, 4, 1)
#define CONF_DNS_PORT       53

// --- Persistence ---
#define CONF_PREFS_NS       "charmer"
#define CONF_DEFAULT_TZ     1

#endif
