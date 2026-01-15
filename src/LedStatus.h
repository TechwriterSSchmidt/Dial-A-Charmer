#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <FastLED.h>

#define LED_PIN 13
#define NUM_LEDS 1

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
    CRGB _leds[NUM_LEDS];
    unsigned long _lastUpdate = 0;
    int _state = 0; // 0=Idle
};

extern LedStatus statusLed;

#endif
