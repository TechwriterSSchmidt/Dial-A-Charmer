#ifndef ROTARY_DIAL_H
#define ROTARY_DIAL_H

#include <Arduino.h>

class RotaryDial {
public:
    RotaryDial(int pulsePin, int hookPin, int extraButtonPin, int modePin = -1);
    void begin();
    void loop();
    
    // Callbacks
    void onDialComplete(void (*callback)(int number));
    void onHookChange(void (*callback)(bool offHook)); // true = off hook (picked up)
    void onButtonPress(void (*callback)());

    bool isOffHook();
    bool isButtonDown();
    bool isDialing() const { return _dialing; }
    
    // Check if a new pulse happened (clears flag on read)
    bool hasNewPulse() {
        if (_newPulse) {
            _newPulse = false;
            return true;
        }
        return false;
    }

private:
    int _pulsePin;
    int _hookPin;
    int _btnPin;
    int _modePin;
    
    // Debounce & Pulse Counting
    int _pulseCount = 0;
    unsigned long _lastPulseTime = 0;
    volatile bool _dialing = false;
    volatile bool _newPulse = false;
    
    // Hook State
    bool _offHook = false;
    unsigned long _lastHookDebounce = 0;
    
    // Button State
    bool _btnState = false;
    unsigned long _lastBtnDebounce = 0;

    void (*_dialCallback)(int) = nullptr;
    void (*_hookCallback)(bool) = nullptr;
    void (*_btnCallback)() = nullptr;
    
    static void IRAM_ATTR isrPulse();
    static RotaryDial* _instance;
};

#endif
