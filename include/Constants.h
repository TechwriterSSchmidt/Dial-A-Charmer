#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <Arduino.h>

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
    constexpr const char* STARTUP_SND = "/system/startup.mp3";
    constexpr const char* READY_SND   = "/system/system_ready.mp3";
    
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