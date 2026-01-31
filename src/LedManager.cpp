#include "LedManager.h"

LedManager::LedManager(uint8_t pin, uint8_t numPixels) 
    : _pixels(numPixels, pin, NEO_GRB + NEO_KHZ800), _pin(pin), _enabled(pin != 255) {
    _currentMode = OFF;
    _lastUpdate = 0;
    _pulsePhase = 0;
}

void LedManager::begin() {
    if (!_enabled) return;
    _pixels.begin();
    _pixels.show(); // Alles aus initial
}

void LedManager::reloadSettings() {
    if (!_enabled) return;
    // Placeholder - refresh LED state if needed
    if (_currentMode != OFF) {
        _pixels.show();
    }
}

void LedManager::setMode(Mode mode) {
    if (!_enabled) return;
    if (_currentMode != mode) {
        _currentMode = mode;
        _pulsePhase = 0;
        
        if (_currentMode == OFF) setColor(0, 0, 0);
        if (_currentMode == FULL_ON) setColor(255, 255, 255);
    }
}

void LedManager::update() {
    if (!_enabled) return;
    unsigned long now = millis();
    
    // Framerate Begrenzung (ca. 30 FPS saves CPU/RMT load)
    if (now - _lastUpdate < 33) return; 
    _lastUpdate = now;

    float brightness = 0;
    int r = 0, g = 0, b = 0;

    switch (_currentMode) {
        case OFF:
             setColor(0,0,0); return; 
        case FULL_ON:
             setColor(255,255,255); return;

        case IDLE_GLOW:
            // Vintage Glühfaden: Warmes Orange/Gold (255, 120, 0)
            // Leichtes Flackern für Realismus
            brightness = 15 + random(0, 10); 
            // Skaliere Farbe basierend auf Helligkeit
            r = (255 * brightness) / 255;
            g = (100 * brightness) / 255; // Mehr rotteil
            b = 0;
            setColor(r, g, b);
            break;

        case CONNECTING:
            // Cyan/Blau für Verbindung/Wait
            _pulsePhase += 0.10; // Faster for 30fps
            if (_pulsePhase > 6.283) _pulsePhase -= 6.283;
            
            brightness = 10 + (int)((sin(_pulsePhase) + 1.0) / 2.0 * 100.0);
            // Teal/Cyan
            setColor(0, brightness, brightness);
            break;

        case SNOOZE_MODE:
            // Warmweißes Dauerleuchten (Stabil)
            // Warm White: 255, 220, 100
            setColor(100, 80, 20); // Gedimmt aber stabil
            break;

        case ALARM_CLOCK:
            // Warmweißes Pulsieren
            _pulsePhase += 0.20; 
            if (_pulsePhase > 6.283) _pulsePhase -= 6.283;
            
            brightness = (int)((sin(_pulsePhase) + 1.0) / 2.0 * 255.0);
            
            // Warm White R,G,B logic
            r = brightness;
            g = (uint8_t)((200.0/255.0) * brightness);
            b = (uint8_t)((50.0/255.0) * brightness);
            setColor(r, g, b);
            break;

        case TIMER_ALERT:
            // Schnelles rotes Pulsieren
            _pulsePhase += 0.40; 
            if (_pulsePhase > 6.283) _pulsePhase -= 6.283;
            
            brightness = (int)((sin(_pulsePhase) + 1.0) / 2.0 * 255.0);
            setColor((int)brightness, 0, 0);
            break;

        case SOS:
            // Rotes Blinken
            long seq = now % 6000;
            bool on = false;
            
            // S (...) --- O (---) --- S (...) Logik wie vorher
            if (seq < 200 || (seq > 400 && seq < 600) || (seq > 800 && seq < 1000)) on = true; // S
            else if ((seq > 1600 && seq < 2200) || (seq > 2400 && seq < 3000) || (seq > 3200 && seq < 3800)) on = true; // O
            else if ((seq > 4600 && seq < 4800) || (seq > 5000 && seq < 5200) || (seq > 5400 && seq < 5600)) on = true; // S
            
            setColor(on ? 255 : 0, 0, 0);
            break;
    }
}

void LedManager::setColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!_enabled) return;
    // Delta Check: Don't write to RMT if color matches (saves CPU/Crash Risk)
    uint32_t newColor = _pixels.Color(r, g, b);
    if (_pixels.getPixelColor(0) == newColor) return;

    // Da wir nur 1 Pixel haben: Index 0
    _pixels.setPixelColor(0, newColor);
    _pixels.show();
}
