#include "LedManager.h"

LedManager::LedManager(uint8_t pin, uint8_t numPixels) 
    : _pixels(numPixels, pin, NEO_GRB + NEO_KHZ800), _pin(pin) {
    _currentMode = OFF;
    _lastUpdate = 0;
    _pulsePhase = 0;
}

void LedManager::begin() {
    _pixels.begin();
    _pixels.show(); // Alles aus initial
}

void LedManager::setMode(Mode mode) {
    if (_currentMode != mode) {
        _currentMode = mode;
        _pulsePhase = 0;
        
        if (_currentMode == OFF) setColor(0, 0, 0);
        if (_currentMode == FULL_ON) setColor(255, 255, 255);
    }
}

void LedManager::update() {
    unsigned long now = millis();
    
    // Framerate Begrenzung (ca. 60 FPS)
    if (now - _lastUpdate < 16) return;
    _lastUpdate = now;

    float brightness = 0;
    int r = 0, g = 0, b = 0;

    switch (_currentMode) {
        case OFF:
        case FULL_ON:
            // Statisch, nichts zu tun
            return;

        case IDLE_GLOW:
            // Vintage Glühfaden: Warmes Orange/Gold (255, 140, 0)
            // Leichtes Flackern für Realismus
            brightness = 20 + random(0, 5); 
            // Skaliere Farbe basierend auf Helligkeit
            r = (255 * brightness) / 255;
            g = (100 * brightness) / 255; // Weniger Grün macht es oranger
            b = 0;
            setColor(r, g, b);
            break;

        case BREATHE_SLOW:
            // Cyan/Blau für Verbindung/Wait oder Warmweiß für Snooze
            _pulsePhase += 0.05; 
            if (_pulsePhase > 6.283) _pulsePhase -= 6.283;
            
            // Helligkeit 10 bis 150
            brightness = 10 + (int)((sin(_pulsePhase) + 1.0) / 2.0 * 140.0);
            
            // Gold passend zum Thema
            r = (255 * ((int)brightness)) / 255;
            g = (160 * ((int)brightness)) / 255;
            b = (20 * ((int)brightness)) / 255;
            setColor(r, g, b);
            break;

        case BREATHE_FAST:
            // ALARM: Rot (255, 0, 0)
            _pulsePhase += 0.15; 
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
    // Da wir nur 1 Pixel haben: Index 0
    _pixels.setPixelColor(0, _pixels.Color(r, g, b));
    _pixels.show();
}
