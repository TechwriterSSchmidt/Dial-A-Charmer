#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <Arduino.h>

// --- Hardware & System Limits ---
#define VOL_MAX_USER 42
#define VOL_MAX_INTERNAL 21
#define MAX_SUBFOLDER_SCAN 100

// --- System Codes ---
#define SYS_CODE_TOGGLE_ALARMS    "90"
#define SYS_CODE_SKIP_NEXT_ALARM  "91"
#define SYS_CODE_STATUS           "8"

// --- Timeouts ---
#define DIAL_CMD_TIMEOUT 3000   // Time to wait after last digit before processing
#define OFF_HOOK_TIMEOUT 5000   // Time until busy tone starts
#define WDT_TIMEOUT      30     // Watchdog

// --- File System Paths ---
namespace Path {
    constexpr const char* SYSTEM      = "/system";
    constexpr const char* FONTS       = "/system/fonts";
    constexpr const char* RINGTONES   = "/ringtones";
    constexpr const char* PLAYLISTS   = "/playlists";
    constexpr const char* PHONEBOOK   = "/phonebook.json";
    
    // Specific Files
    constexpr const char* DIAL_TONE   = "/system/dial_tone.wav"; 
    constexpr const char* BUSY_TONE   = "/system/busy_tone.wav";
    constexpr const char* STARTUP_SND = "/system/startup.wav";
    constexpr const char* READY_SND   = "/system/system_ready.wav";
    
    // Additional System Sounds
    constexpr const char* ERROR_TONE        = "/system/error_tone.wav";
    constexpr const char* BEEP              = "/system/beep.wav";
    constexpr const char* CLICK             = "/system/click.wav";
    constexpr const char* COMPUTING         = "/system/computing.wav";
    constexpr const char* TIME_UNAVAILABLE_DE = "/system/time_unavailable_de.wav";
    constexpr const char* TIME_UNAVAILABLE_EN = "/system/time_unavailable_en.wav";
    
    constexpr const char* TIMER_STOPPED_DE  = "/system/timer_stopped_de.wav";
    constexpr const char* TIMER_STOPPED_EN  = "/system/timer_stopped_en.wav";
    constexpr const char* ALARM_STOPPED_DE  = "/system/alarm_stopped_de.wav";
    constexpr const char* ALARM_STOPPED_EN  = "/system/alarm_stopped_en.wav";
    
    constexpr const char* REINDEX_WARNING_DE = "/system/reindex_warning_de.wav";
    constexpr const char* REINDEX_WARNING_EN = "/system/reindex_warning_en.wav";

    constexpr const char* DEFAULT_DIALTONE  = "/system/dialtone_1.wav";
    constexpr const char* FALLBACK_ALARM    = "/system/fallback_alarm.wav";

    // Fonts
    constexpr const char* FONT_MAIN   = "/system/fonts/ZenTokyoZoo-Regular.ttf";
    constexpr const char* FONT_SEC    = "/system/fonts/Pompiere-Regular.ttf";
}

// --- JSON Keys (Phonebook) ---
namespace JsonKey {
    constexpr const char* TYPE        = "type";
    constexpr const char* VALUE       = "value";
    constexpr const char* PARAM       = "parameter";
    constexpr const char* NAME        = "name";
}

#endif