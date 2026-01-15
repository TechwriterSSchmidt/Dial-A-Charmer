#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>

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
    
    // System
    void clear();

private:
    Preferences _prefs;
    const char* _ns = "charmer";
};

extern Settings settings; // Global instance

#endif
