#pragma once

// Dial-A-Charmer ESP-IDF Configuration

// Rotary Dial & Inputs
#define APP_PIN_DIAL_PULSE 5   
#define APP_PIN_DIAL_MODE  23  // Key 4 (Re-enabled)
#define APP_PIN_HOOK       19  
#define APP_PIN_EXTRA_BTN  18  

// Key3 (Audio mute test)
#define APP_PIN_KEY3       19
#define APP_KEY3_ACTIVE_LOW true

// Dial Mode: "Closed to GND" during dialing -> LOW. So Active Low.
#define APP_DIAL_MODE_ACTIVE_LOW true 

// Pulse: "Closed to GND" (Low) when idle. Opens (High) for pulse.
// Logic counts when input matches the "Active Low" setting logic:
// (level == (active_low ? 1 : 0)).
// We want to count HIGH (1). So we set this to true.
#define APP_DIAL_PULSE_ACTIVE_LOW true

#define APP_HOOK_ACTIVE_LOW  true 

// Power Amplifier
#define APP_PIN_PA_ENABLE  21

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


// SD Card
#define APP_PIN_SD_CS      13
