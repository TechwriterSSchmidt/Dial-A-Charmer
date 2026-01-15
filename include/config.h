#ifndef CONFIG_H
#define CONFIG_H

// --- Hardware Pins ---
// Audio (I2S - ES8311 Codec + NS4150B Amp)
#define CONF_I2S_BCLK       26
#define CONF_I2S_LRC        25
#define CONF_I2S_DOUT       22
// ES8311 requires I2C for configuration
#define CONF_I2C_SDA        21
#define CONF_I2C_SCL        22 // Note: Check your board! Usually 22 is SCL on ESP32, but above I2S_DOUT is 22. Conflict?
                               // Wemos D32 Pro Default: SDA=21, SCL=22. 
                               // If using ES8311 module, ensure I2S pins don't conflict. 
                               // Common I2S mappings: BCLK=26, LRC=25, DOUT=22 is standard for internal DAC or PCM5102.
                               // If SCL is 22, move I2S_DOUT to another pin (e.g., 19 or 27).
                               // ADJUSTING I2S_DOUT to 27 to avoid conflict with SCL(22).
#undef CONF_I2S_DOUT
#define CONF_I2S_DOUT       27 

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
#define CONF_DEFAULT_VOL    30 // Scale 0-42
#define CONF_DEFAULT_RING   1

#endif
