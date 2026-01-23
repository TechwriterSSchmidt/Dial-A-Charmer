#include "RotaryDial.h"
#include "config.h"

RotaryDial* RotaryDial::_instance = nullptr;

RotaryDial::RotaryDial(int pulsePin, int hookPin, int extraButtonPin, int modePin) 
    : _pulsePin(pulsePin), _hookPin(hookPin), _btnPin(extraButtonPin), _modePin(modePin) {
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
            _instance->_newPulse = true; // Signal new pulse
            lastIsr = now;
        }
    }
}

void RotaryDial::begin() {
    // GPIO 34-39 are input only and have NO internal pullups.
    // Ensure you have external pullups or use pins that support internal pullups.
    if (_pulsePin >= 34 && _pulsePin <= 39) pinMode(_pulsePin, INPUT); else pinMode(_pulsePin, INPUT_PULLUP);
    if (_hookPin >= 34 && _hookPin <= 39) pinMode(_hookPin, INPUT); else pinMode(_hookPin, INPUT_PULLUP);
    if (_btnPin >= 34 && _btnPin <= 39) pinMode(_btnPin, INPUT); else pinMode(_btnPin, INPUT_PULLUP); 
    
    if (_modePin >= 0) {
        if (_modePin >= 34 && _modePin <= 39) pinMode(_modePin, INPUT); else pinMode(_modePin, INPUT_PULLUP);
    }

    attachInterrupt(digitalPinToInterrupt(_pulsePin), isrPulse, FALLING);
}

void RotaryDial::loop() {
    unsigned long now = millis();
    
    // 1. Dial Logic
    // If Mode Pin is defined, we use it to determine "Finished" instead of Timeout
    if (_modePin >= 0) {
        bool rawMode = (digitalRead(_modePin) == (CONF_DIAL_MODE_ACTIVE_LOW ? LOW : HIGH));
        
        // Debounce Mode Pin
        static bool lastRawMode = false;
        static bool stableModeActive = false;
        static unsigned long modeChangeTime = 0;

        if (rawMode != lastRawMode) {
             modeChangeTime = now;
             lastRawMode = rawMode;
        }
        
        if ((now - modeChangeTime) > 20) { // 20ms Debounce
             stableModeActive = rawMode;
        }
        
        // State Machine via Pin
        if (stableModeActive) {
            _dialing = true;
            // We are dialing, just collect pulses (via ISR)
            // Reset timeout timer just in case we fallback
            _lastPulseTime = now; 
        } else {
            // Not dialing (Idle)
            if (_dialing) {
                // Falling Edge of "Dialing Active" -> Dailing Finished immediately
                _dialing = false;
                
                // Process Digit
                int digit = _pulseCount;
                if (digit > 0) { // Only if we actually counted something
                    if (digit > 9) digit = 0; 
                    if (digit == 10) digit = 0; 
                    if(_dialCallback) _dialCallback(digit);
                }
                _pulseCount = 0;
            }
        }
    } else {
        // Fallback: Timeout Logic
        if (_dialing && (now - _lastPulseTime > CONF_DIAL_TIMEOUT)) {
            _dialing = false;
            int digit = _pulseCount;
            if (digit > 9) digit = 0; 
            if (digit == 10) digit = 0; 
            
            _pulseCount = 0;
            
            if (_dialCallback) _dialCallback(digit);
        }
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
