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


// SD Card
#define APP_PIN_SD_CS      13
