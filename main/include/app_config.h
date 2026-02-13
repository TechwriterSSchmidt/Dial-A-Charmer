#pragma once

// Dial-A-Charmer ESP-IDF Configuration

// Rotary Dial & Inputs
#define APP_PIN_DIAL_PULSE 5   
#define APP_PIN_DIAL_MODE  -1 // Disabled to free GPIO 23
#define APP_PIN_HOOK       19 // Key 3 (IO19)
#define APP_PIN_EXTRA_BTN  36 // Key 1 (IO36)

// Key3 (handset button)
#define APP_PIN_KEY3       19
#define APP_KEY3_ACTIVE_LOW true
#define APP_HOOK_ACTIVE_LOW  true 

// Dial Mode behavior (Ignored if pin is -1)
#define APP_DIAL_MODE_ACTIVE_LOW true 

// Pulse: "Closed to GND" (Low) when idle. Opens (High) for pulse.
// Logic counts when input matches the "Active Low" setting logic
#define APP_DIAL_PULSE_ACTIVE_LOW true

// WS2812 LED (Boot Indicator and Night Mode)
#define APP_PIN_LED        23

// SD Card
#define APP_PIN_SD_CS      13

// Power Amplifier
#define APP_PIN_PA_ENABLE  21 
#define APP_PA_ENABLE_DELAY_MS 250 // Wait for DAC/I2S voltage to stabilize
#define APP_PA_DISABLE_DELAY_MS 50

// RTC (DS3231)
// I2C Pins
#define APP_PIN_RTC_SDA 18
#define APP_PIN_RTC_SCL 22

// Timer alarm loop duration (minutes)
#define APP_TIMER_ALARM_LOOP_MINUTES 3

// Daily alarm loop duration (minutes)
#define APP_DAILY_ALARM_LOOP_MINUTES 5

// Daily alarm volume ramp duration (ms)
#define APP_ALARM_RAMP_DURATION_MS 120000
#define APP_ALARM_FADE_MIN_FACTOR 0.05f

// Alarm playback retry interval (ms)
#define APP_ALARM_RETRY_INTERVAL_MS 2000

// Timing settings (ms)
#define APP_DIAL_TIMEOUT_MS 2000
#define APP_DIAL_PULSE_DEBOUNCE_MS 60
#define APP_DIAL_DIGIT_GAP_MS 500
#define APP_PERSONA_PAUSE_MS 1500
#define APP_DIALTONE_SILENCE_MS 1000
#define APP_BUSY_TIMEOUT_MS 5000
#define APP_WAV_SWITCH_DELAY_MS 30
#define APP_OUTPUT_MUTE_DELAY_MS 25
#define APP_WAV_FADE_OUT_EXTRA_MS 20
#define APP_WAV_FADE_IN_MS 55
#define APP_VOICE_MENU_REANNOUNCE_DELAY_MS 200

// Extra button (Key 5) long-press handling
#define APP_EXTRA_BTN_DEEPSLEEP_MS 10000

// Timer settings
#define APP_TIMER_MIN_MINUTES 1
#define APP_TIMER_MAX_MINUTES 500
#define APP_DEFAULT_TIMER_RINGTONE "standard_ringtone.wav"

// Snooze settings
#define APP_SNOOZE_DEFAULT_MINUTES 5
#define APP_SNOOZE_MIN_MINUTES 1
#define APP_SNOOZE_MAX_MINUTES 60

// Software gain defaults
#define APP_GAIN_DEFAULT_LEFT 0.5f
#define APP_GAIN_DEFAULT_RIGHT 0.6f
#define APP_GAIN_RAMP_MS 40

// Handset noise gate (reduce hiss during quiet passages)
#define APP_HANDSET_NOISE_GATE_THRESHOLD 700
#define APP_HANDSET_NOISE_GATE_FLOOR 0.2f
#define APP_HANDSET_NOISE_GATE_SMOOTH 0.08f

// Volume defaults (0-100)
#define APP_DEFAULT_BASE_VOLUME 60
#define APP_DEFAULT_HANDSET_VOLUME 50
#define APP_ALARM_MIN_VOLUME 55
#define APP_ALARM_DEFAULT_VOLUME 90

// LED signal lamp defaults
#define APP_LED_DEFAULT_ENABLED 1
#define APP_LED_DAY_PERCENT 100
#define APP_LED_NIGHT_PERCENT 10
#define APP_LED_DAY_START_HOUR 7
#define APP_LED_NIGHT_START_HOUR 22

// OTA
#define APP_OTA_PASSWORD "dialy1935"

// Phonebook default numbers (max 3 digits)
#define APP_PB_NUM_PERSONA_1 "1"
#define APP_PB_NUM_PERSONA_2 "2"
#define APP_PB_NUM_PERSONA_3 "3"
#define APP_PB_NUM_PERSONA_4 "4"
#define APP_PB_NUM_PERSONA_5 "5"
#define APP_PB_NUM_RANDOM_MIX "11"
#define APP_PB_NUM_TIME "110"
#define APP_PB_NUM_VOICE_MENU "0"
#define APP_PB_NUM_REBOOT "999"


// Debug / Monitor
#define ENABLE_SYSTEM_MONITOR 1
#define SYSTEM_MONITOR_INTERVAL_MS 30000
#define APP_DIAL_DEBUG_SERIAL 0
#define APP_OTA_DEBUG 0

// Task watchdog
#define APP_ENABLE_TASK_WDT 1
#define APP_TASK_WDT_TIMEOUT_SEC 10
#define APP_TASK_WDT_PANIC 1

// SD log capture (debug)
#define APP_ENABLE_SD_LOG 1
#define APP_SD_LOG_PATH "/sdcard/logs/app.log"
#define APP_SD_LOG_MAX_BYTES (2 * 1024 * 1024)
#define APP_SD_LOG_FLUSH_INTERVAL_MS 5000
#define APP_SD_LOG_BUFFER_LINES 120
