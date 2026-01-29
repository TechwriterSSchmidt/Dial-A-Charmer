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
    
    // Per-Day Alarms (0=Monday ... 6=Sunday)
    int getAlarmHour(int day);
    void setAlarmHour(int day, int h);
    int getAlarmMinute(int day);
    void setAlarmMinute(int day, int m);
    String getAlarmTone(int day); // Changed to String
    void setAlarmTone(int day, String tone);
    bool isAlarmEnabled(int day);
    void setAlarmEnabled(int day, bool enabled);
    
    // AI
    String getGeminiKey();
    void setGeminiKey(String key);
    String getPersonaName(); // New: Configurable Name
    void setPersonaName(String name);
    
    // Audio
    int getVolume();
    void setVolume(int vol);
    int getBaseVolume();
    void setBaseVolume(int vol);
    int getSnoozeMinutes(); // Added
    void setSnoozeMinutes(int min); 
    String getRingtone(); // Changed to String
    void setRingtone(String tone);
    String getDialTone(); // Changed to String
    void setDialTone(String tone);

    // Half Duplex Mode
    bool getHalfDuplex(); // New
    void setHalfDuplex(bool enabled);

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
    String getLanguage(); // "de" or "en"
    void setLanguage(String lang);
    void clear();

private:
    Preferences _prefs;
    const char* _ns = CONF_PREFS_NS;
    
    // RAM Cache to prevent NVS saturation/crashes
    struct AlarmCache { 
        int8_t h; 
        int8_t m; 
        String tone; // Changed to String
        bool en; 
    };
    AlarmCache _alarms[7];
    bool _cacheLoaded = false;
    
    void loadCache();
};

extern Settings settings; // Global instance

#endif
