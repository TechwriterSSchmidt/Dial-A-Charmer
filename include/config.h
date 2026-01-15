#ifndef CONFIG_H
#define CONFIG_H

// --- Hardware Pins ---
// Audio (I2S - ES8311 Codec + NS4150B Amp)
#define CONF_I2S_BCLK       26
#define CONF_I2S_LRC        25
// Pin 22 ist SCL beim D32 Pro, daher DOUT auf 27 verlegt (IO27/TFT_DC)
#define CONF_I2S_DOUT       27 

// ES8311 requires I2C for configuration
// Bestätigt durch D32 Pro Schematic: SDA=21, SCL=22
#define CONF_I2C_SDA        21
#define CONF_I2C_SCL        22 

// Audio Speaker (MAX98357)
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
#define CONF_GPS_TX         0   // Moved to 0 to free Pin 5 (GPIO 0 is Boot-Safe if Idle High)
#define CONF_GPS_BAUD       9600

// Battery Monitoring (Lolin D32 Pro)
#define CONF_PIN_BATTERY    35  // 100k/100k Divider internally connected

// Rotary Dial & Inputs
#define CONF_PIN_DIAL_PULSE 5   // Changed to 5 (Supports INPUT_PULLUP) - No resistor needed!
#define CONF_PIN_HOOK       32
#define CONF_PIN_EXTRA_BTN  33

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
#define CONF_DIAL_TIMEOUT        500
#define CONF_HOOK_DEBOUNCE       50
#define CONF_BTN_DEBOUNCE        50

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
