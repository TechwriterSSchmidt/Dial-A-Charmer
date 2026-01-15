#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <FastLED.h>
#include "config.h"

class LedStatus {
public:
    void begin();
    void loop();
    
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
    CRGB _leds[CONF_AC_EXC_LED_NUM];
    unsigned long _lastUpdate = 0;
    int _state = 0; // 0=Idle
};

extern LedStatus statusLed;

#endif
