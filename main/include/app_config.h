#pragma once

// Dial-A-Charmer ESP-IDF Configuration

// Rotary Dial & Inputs
#define APP_PIN_DIAL_PULSE 5   
#define APP_PIN_DIAL_MODE  23  // Key 4
#define APP_PIN_HOOK       19  
#define APP_PIN_EXTRA_BTN  18  

#define APP_DIAL_MODE_ACTIVE_LOW true
#define APP_DIAL_PULSE_ACTIVE_LOW true
#define APP_HOOK_ACTIVE_LOW  true 

// Power Amplifier
#define APP_PIN_PA_ENABLE  21

// SD Card
#define APP_PIN_SD_CS      13
