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
    
    // Periodic Alarm (Web Interface)
    int getAlarmHour();
    void setAlarmHour(int h);
    int getAlarmMinute();
    void setAlarmMinute(int m);
    int getAlarmDays(); // Bitmask: 1=Mon, 2=Tue ... 64=Sun, 0=Off
    void setAlarmDays(int days); // Bitmask
    
    // AI
    String getGeminiKey();
    void setGeminiKey(String key);
    
    // Audio
    int getVolume();
    void setVolume(int vol);
    int getBaseVolume();
    void setBaseVolume(int vol);
    int getSnoozeMinutes(); // Added
    void setSnoozeMinutes(int min); 
    int getRingtone();
    void setRingtone(int toneIndex);
    int getDialTone(); // New
    void setDialTone(int toneIndex);

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
};

extern Settings settings; // Global instance

#endif
