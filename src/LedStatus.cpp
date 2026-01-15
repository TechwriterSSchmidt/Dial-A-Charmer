#include "LedStatus.h"
#include "Settings.h"

LedStatus statusLed;

void LedStatus::begin() {
    _pixels.begin();
    reloadSettings();
    checkBrightness(-1); // Init brightness
    setBooting();
}

void LedStatus::reloadSettings() {
    _cachedDayBright = settings.getLedDayBright();
    _cachedNightBright = settings.getLedNightBright();
    _cachedNightStart = settings.getNightStartHour();
    _cachedNightEnd = settings.getNightEndHour();
}

void LedStatus::loop(int currentHour) {
    checkBrightness(currentHour);
    // Basic pulse/animation logic could go here
}

void LedStatus::checkBrightness(int hour) {
    int target = _cachedDayBright; // Default
    
    if (hour != -1) {
        int start = _cachedNightStart;
        int end = _cachedNightEnd;
        int night = _cachedNightBright;
        int day = _cachedDayBright;
        
        bool isNight = false;
        if (start < end) {
            // e.g., 22 to 23? No usually night crosses midnight -> 22 to 6
            // If start < end (e.g. 0 to 6), then night is between them.
            if (hour >= start && hour < end) isNight = true;
        } else {
            // Crossing midnight, e.g. 22 to 6
            if (hour >= start || hour < end) isNight = true;
        }
        
        target = isNight ? night : day;
    }

    if (target != _currentBrightness) {
        _currentBrightness = target;
        _pixels.setBrightness(_currentBrightness);
        applyState(); // Re-render with new brightness
    }
}

void LedStatus::applyState() {
    uint32_t color = 0;
    switch(_currentState) {
        case STATE_IDLE: color = _pixels.Color(0, 0, 0); break;
        case STATE_BOOTING: color = _pixels.Color(0, 0, 255); break;
        case STATE_WIFI_CONNECTING: color = _pixels.Color(255, 255, 0); break;
        case STATE_WIFI_CONNECTED: color = _pixels.Color(0, 255, 0); break;
        case STATE_AP_MODE: color = _pixels.Color(128, 0, 128); break;
        case STATE_GPS_SEARCHING: color = _pixels.Color(255, 165, 0); break;
        case STATE_GPS_LOCKED: color = _pixels.Color(0, 255, 255); break;
        case STATE_TALKING: color = _pixels.Color(255, 255, 255); break;
        case STATE_WARNING: color = _pixels.Color(255, 0, 0); break;
    }
    _pixels.setPixelColor(0, color);
    _pixels.show();
}

void LedStatus::setBooting() {
    _currentState = STATE_BOOTING;
    applyState();
}

void LedStatus::setWifiConnecting() {
    _currentState = STATE_WIFI_CONNECTING;
    applyState();
}

void LedStatus::setWifiConnected() {
    _currentState = STATE_WIFI_CONNECTED;
    applyState();
    delay(1000); // Blocking delay ok for short transient
    setIdle();
}

void LedStatus::setAPMode() {
    _currentState = STATE_AP_MODE;
    applyState();
}

void LedStatus::setGpsSearching() {
    _currentState = STATE_GPS_SEARCHING;
    applyState();
}

void LedStatus::setGpsLocked() {
    _currentState = STATE_GPS_LOCKED;
    applyState();
}

void LedStatus::setTalking() {
    _currentState = STATE_TALKING;
    applyState();
}

void LedStatus::setIdle() {
    _currentState = STATE_IDLE;
    applyState();
}

void LedStatus::setWarning() {
    _currentState = STATE_WARNING;
    applyState();
}
