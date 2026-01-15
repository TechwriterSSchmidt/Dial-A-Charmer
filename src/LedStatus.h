#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <Adafruit_NeoPixel.h>
#include "config.h"

class LedStatus {
public:
    LedStatus() : _pixels(CONF_AC_EXC_LED_NUM, CONF_PIN_LED, NEO_GRB + NEO_KHZ800) {}

    void begin();
    void loop(int currentHour = -1);
    void reloadSettings(); // Refresh cached settings

    // Status setters
    void setBooting();
    void setWifiConnecting();
    void setWifiConnected();
    void setAPMode();
    void setGpsSearching();
    void setGpsLocked();
    void setTalking();
    void setIdle();
    void setWarning();

private:
    Adafruit_NeoPixel _pixels;
    unsigned long _lastUpdate = 0;
    
    // Cache settings to avoid NVS hammering
    int _cachedDayBright;
    int _cachedNightBright;
    int _cachedNightStart;
    int _cachedNightEnd;
    
    enum State {
        STATE_IDLE,
        STATE_BOOTING,
        STATE_WIFI_CONNECTING,
        STATE_WIFI_CONNECTED,
        STATE_AP_MODE,
        STATE_GPS_SEARCHING,
        STATE_GPS_LOCKED,
        STATE_TALKING,
        STATE_WARNING
    };
    
    State _currentState = STATE_BOOTING;
    int _currentBrightness = -1;
    
    void applyState(); // Re-applies color based on state
    void checkBrightness(int hour);
};

extern LedStatus statusLed;

#endif
