#pragma once

// Dial-A-Charmer ESP-IDF Configuration

// Rotary Dial & Inputs
#define APP_PIN_DIAL_PULSE 5   
#define APP_PIN_DIAL_MODE  23
#define APP_PIN_HOOK       19 // Key 3 (IO19)
#define APP_PIN_EXTRA_BTN  36 // Key 1 (IO36)

// Key3 (handset button)
#define APP_PIN_KEY3       19
#define APP_KEY3_ACTIVE_LOW true

// Dial Mode: "Closed to GND" during dialing -> LOW. So Active Low.
#define APP_DIAL_MODE_ACTIVE_LOW true 

// Pulse: "Closed to GND" (Low) when idle. Opens (High) for pulse.
// Logic counts when input matches the "Active Low" setting logic
#define APP_DIAL_PULSE_ACTIVE_LOW true

#define APP_HOOK_ACTIVE_LOW  true 

// Power Amplifier
// GPIO 21 is safe to use for Amp because we moved SDA to 18
#define APP_PIN_PA_ENABLE  21 
#define APP_PA_ENABLE_DELAY_MS 250 // Wait for DAC/I2S voltage to stabilize
#define APP_PA_DISABLE_DELAY_MS 50

// RTC (DS3231)
// Using GPIO 18 (Was Key 5 / Extra Btn) for SDA
// Using GPIO 22 (Green LED) for SCL
#define APP_PIN_RTC_SDA 18
#define APP_PIN_RTC_SCL 22

// Timer alarm loop duration (minutes)
#define APP_TIMER_ALARM_LOOP_MINUTES 5

// Timing settings (ms)
#define APP_DIAL_TIMEOUT_MS 2000
#define APP_PERSONA_PAUSE_MS 1500
#define APP_DIALTONE_SILENCE_MS 1000
#define APP_BUSY_TIMEOUT_MS 5000

// Software gain defaults
#define APP_GAIN_DEFAULT_LEFT 0.5f
#define APP_GAIN_DEFAULT_RIGHT 0.5f
#define APP_GAIN_RAMP_MS 40

// Phonebook default numbers (max 3 digits)
#define APP_PB_NUM_PERSONA_1 "1"
#define APP_PB_NUM_PERSONA_2 "2"
#define APP_PB_NUM_PERSONA_3 "3"
#define APP_PB_NUM_PERSONA_4 "4"
#define APP_PB_NUM_PERSONA_5 "5"
#define APP_PB_NUM_RANDOM_MIX "11"
#define APP_PB_NUM_TIME "110"
#define APP_PB_NUM_GEMINI "000"
#define APP_PB_NUM_VOICE_MENU "900"
#define APP_PB_NUM_TOGGLE_ALARMS "910"
#define APP_PB_NUM_SKIP_ALARM "911"
#define APP_PB_NUM_REBOOT "999"


// SD Card
#define APP_PIN_SD_CS      13

// Debug / Monitor
#define ENABLE_SYSTEM_MONITOR 0
#define SYSTEM_MONITOR_INTERVAL_MS 5000
