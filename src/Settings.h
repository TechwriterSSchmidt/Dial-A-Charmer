#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

class Settings {
public:
    void begin();
    
    // WiFi
    String getWifiSSID();
    void setWifiSSID(String ssid);
    String getWifiPass();
    void setWifiPass(String pass);
    
    // Time
    int getTimezoneOffset();
    void setTimezoneOffset(int offset); // in hours
    
    // AI
    String getGeminiKey();
    void setGeminiKey(String key);
    
    // Audio
    int getVolume();
    void setVolume(int vol);
    int getBaseVolume();
    void setBaseVolume(int vol);
    int getRingtone();
    void setRingtone(int toneIndex);

    // LED Brightness
    int getLedDayBright();     // 0-255
    void setLedDayBright(int bright);
    int getLedNightBright();   // 0-255
    void setLedNightBright(int bright);
    int getNightStartHour();   // 0-23
    void setNightStartHour(int hour);
    int getNightEndHour();     // 0-23
    void setNightEndHour(int hour);

    // System
    void clear();

private:
    Preferences _prefs;
    const char* _ns = CONF_PREFS_NS;
};

extern Settings settings; // Global instance

#endif
