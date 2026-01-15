#include "RotaryDial.h"
#include "config.h"

RotaryDial* RotaryDial::_instance = nullptr;

RotaryDial::RotaryDial(int pulsePin, int hookPin, int extraButtonPin) 
    : _pulsePin(pulsePin), _hookPin(hookPin), _btnPin(extraButtonPin) {
    _instance = this;
}

void IRAM_ATTR RotaryDial::isrPulse() {
    if (_instance) {
        // Simple debounce
        static unsigned long lastIsr = 0;
        unsigned long now = millis();
        if (now - lastIsr > CONF_DIAL_DEBOUNCE_PULSE) {
            _instance->_pulseCount++;
            _instance->_lastPulseTime = now;
            _instance->_dialing = true;
            lastIsr = now;
        }
    }
}

void RotaryDial::begin() {
    pinMode(_pulsePin, INPUT_PULLUP);
    pinMode(_hookPin, INPUT_PULLUP);
    pinMode(_btnPin, INPUT_PULLUP); 
    
    attachInterrupt(digitalPinToInterrupt(_pulsePin), isrPulse, FALLING);
}

void RotaryDial::loop() {
    unsigned long now = millis();
    
    // 1. Dial Logic
    if (_dialing && (now - _lastPulseTime > CONF_DIAL_TIMEOUT)) {
        // Timeout -> Digit Complete
        _dialing = false;
        int digit = _pulseCount;
        if (digit > 9) digit = 0; 
        if (digit == 10) digit = 0; 
        
        _pulseCount = 0;
        
        if (_dialCallback) _dialCallback(digit);
    }
    
    // 2. Hook Logic
    bool rawHook = digitalRead(_hookPin); 
    static bool lastRawHook = false;
    static unsigned long hookChangeTime = 0;
    
    if (rawHook != lastRawHook) {
        hookChangeTime = now;
        lastRawHook = rawHook;
    }
    
    if ((now - hookChangeTime) > CONF_HOOK_DEBOUNCE) {
        bool stableHook = rawHook;
        // Hook Pin Logic: HIGH (Open) = Off Hook? or LOW?
        // Let's assume HIGH = Off Hook (Switch Open)
        bool isPickedUp = (stableHook == HIGH); 
        
        if (isPickedUp != _offHook) {
            _offHook = isPickedUp;
            if (_hookCallback) _hookCallback(_offHook);
        }
    }
    
    // 3. Button Logic
    bool rawBtn = digitalRead(_btnPin); 
    static bool lastRawBtn = HIGH;
    static unsigned long btnChangeTime = 0;
    
    if (rawBtn != lastRawBtn) {
        btnChangeTime = now;
        lastRawBtn = rawBtn;
    }
    
    if ((now - btnChangeTime) > CONF_BTN_DEBOUNCE) {
        bool stableBtn = rawBtn;
        bool isPressed = (stableBtn == LOW); // Active Low
        
        if (isPressed != _btnState) {
            _btnState = isPressed;
            if (_btnState && _btnCallback) { // Trigger on Press
                _btnCallback();
            }
        }
    }
}

void RotaryDial::onDialComplete(void (*callback)(int)) {
    _dialCallback = callback;
}

void RotaryDial::onHookChange(void (*callback)(bool)) {
    _hookCallback = callback;
}

void RotaryDial::onButtonPress(void (*callback)()) {
    _btnCallback = callback;
}

bool RotaryDial::isOffHook() {
    return _offHook;
}

bool RotaryDial::isButtonDown() {
    return _btnState;
}
