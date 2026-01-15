#include "RotaryDial.h"

RotaryDial* RotaryDial::_instance = nullptr;

RotaryDial::RotaryDial(int pulsePin, int hookPin, int extraButtonPin) {
    _pulsePin = pulsePin;
    _hookPin = hookPin;
    _btnPin = extraButtonPin;
    _instance = this;
}

void IRAM_ATTR RotaryDial::isrPulse() {
    if (_instance) {
        if (millis() - _instance->_lastPulseTime > 30) { // Simple debounce for pulse
            _instance->_pulseCount++;
            _instance->_lastPulseTime = millis();
            _instance->_dialing = true;
        }
    }
}

void RotaryDial::begin() {
    pinMode(_pulsePin, INPUT_PULLUP); // Note: If pin 34-39, external pullup needed
    pinMode(_hookPin, INPUT_PULLUP);
    pinMode(_btnPin, INPUT_PULLUP);
    
    attachInterrupt(digitalPinToInterrupt(_pulsePin), isrPulse, FALLING); // Dial sends pulses by breaking circuit -> Falling edge? Or closing? Usually normally closed pulses open. Let's assume standard break-to-pulse.
    
    // Read initial hook state
    _offHook = digitalRead(_hookPin) == LOW; // Assuming Switch closes when picked up? Or opens? 
    // Standard Gabeltaster: Hörer drauf = gedrückt (Circuit Open/Closed depends on switch). 
    // Usually: Hörer drauf -> Switch pressed. Hörer ab -> Switch released.
    // Let's assume switch connects to GND when pressed (active low).
    // Hörer drauf (On Hook) = Pressed = LOW.
    // Hörer ab (Off Hook) = Released = HIGH.
    // NOTE: Need to verify switch type. Assuming Active LOW for Pressed (On Hook). 
    // Warning: If INPUT_PULLUP is used, Released = HIGH.
    // So On Hook = LOW, Off Hook = HIGH.
    // Logic below: _offHook = (digitalRead == HIGH)
    
    _offHook = (digitalRead(_hookPin) == HIGH); 
}

void RotaryDial::loop() {
    unsigned long now = millis();

    // 1. Process Dials
    if (_dialing && (now - _lastPulseTime > 500)) { // Timeout after last pulse
        // Dial sequence finished
        int number = _pulseCount;
        if (number == 10) number = 0; // 10 pulses = 0
        
        if (_dialCallback && number > 0) { // Filter noise
             _dialCallback(number);
        }
        
        _pulseCount = 0;
        _dialing = false;
    }

    // 2. Process Hook Switch
    bool currentHookInfo = (digitalRead(_hookPin) == HIGH); // HIGH = Off Hook (Released)
    if (currentHookInfo != _offHook) {
        if (now - _lastHookDebounce > 50) {
            _offHook = currentHookInfo;
            if (_hookCallback) {
                _hookCallback(_offHook);
            }
            _lastHookDebounce = now;
        }
    } else {
        _lastHookDebounce = now;
    }

    // 3. Process Button
    bool currentBtn = (digitalRead(_btnPin) == LOW); // Pressed = LOW
    if (currentBtn != _btnState) {
        if (now - _lastBtnDebounce > 50) {
            _btnState = currentBtn;
            if (_btnState && _btnCallback) { // Trigger on press
                _btnCallback();
            }
            _lastBtnDebounce = now;
        }
    } else {
        _lastBtnDebounce = now;
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
