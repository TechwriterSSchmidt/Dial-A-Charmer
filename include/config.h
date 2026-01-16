#ifndef CONFIG_H
#define CONFIG_H

// --- Hardware Pins ---
// Audio (I2S - ES8311 Codec + Headset)
#define CONF_I2S_BCLK       26
#define CONF_I2S_LRC        25  // WS
#define CONF_I2S_DOUT       27  // ESP DOUT -> Codec DI (Sound to Headset)
#define CONF_I2S_DIN        39  // ESP DIN <- Codec DO (Mic from Headset) - Pin 39 is Input Only (VN)
#define CONF_I2S_MCLK       0   // MCK - GPIO0 (Requires boot strapping care, but standard for Audio Kit)

// ES8311 requires I2C for configuration
// Bestätigt durch D32 Pro Schematic: SDA=21, SCL=22
#define CONF_I2C_SDA        21
#define CONF_I2C_SCL        22 

// Audio Speaker (MAX98357A - Base)
#define CONF_I2S_SPK_BCLK   14
#define CONF_I2S_SPK_LRC    12
#define CONF_I2S_SPK_DOUT   15

// Vibration Motor
#define CONF_PIN_VIB_MOTOR  2

// GNSS (M10 or similar)
// WARNUNG: 16/17 sind PSRAM beim D32 Pro!
// GPIO 35 ist intern Batterie-Spannungsteiler (HÄLFTE der Batteriespannung)
// Wir nutzen GPIO 34 (Input Only) für GPS RX, um Konflikte zu vermeiden.
#define CONF_GPS_RX         34  // Input Only - Safe
#define CONF_GPS_TX         -1  // Disabled to free GPIO 0 for MCLK
#define CONF_GPS_BAUD       9600

// Battery Monitoring (Lolin D32 Pro)
#define CONF_PIN_BATTERY    35  // 100k/100k Divider internally connected

// Rotary Dial & Inputs
#define CONF_PIN_DIAL_PULSE 5   // Changed to 5 (Supports INPUT_PULLUP) - No resistor needed!
#define CONF_PIN_HOOK       32
#define CONF_PIN_EXTRA_BTN  33
#define CONF_PIN_DIAL_MODE  36  // Optional: Contact for dialing active/finished (nsr)
#define CONF_DIAL_MODE_ACTIVE_LOW true // true = LOW means dialing active (Normally Open contact that closes while dialing?) check hardware!

// Status LED (WS2812)
#define CONF_PIN_LED        13
#define CONF_AC_EXC_LED_NUM 1 // Dial-A-Charmer Exclusive LED Number

// SD Card (VSPI Default on Lolin D32 Pro)
#define CONF_PIN_SD_CS      4

// --- System Configuration ---
#define CONF_SERIAL_BAUD    115200
#define CONF_AUDIO_VOLUME   15  // Default volume (0-21)

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
