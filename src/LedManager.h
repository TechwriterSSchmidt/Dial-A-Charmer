#ifndef LEDMANAGER_H
#define LEDMANAGER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class LedManager {
public:
    enum Mode {
        OFF,            // Komplett aus
        IDLE_GLOW,      // Warmes, goldenes Glimmen (Flackernd)
        CONNECTING,     // Langsames Pulsieren (Blau/Gold)
        SNOOZE_MODE,    // Warmweißes Dauerleuchten
        ALARM_CLOCK,    // Warmweißes Pulsieren
        TIMER_ALERT,    // Schnelles rotes Pulsieren
        FULL_ON,        // Test/Flashlight
        SOS             // Rotes SOS Signal
    };

    // Konstruktor nun mit Anzahl der LEDs (meist 1)
    LedManager(uint8_t pin, uint8_t numPixels = 1);
    void begin();
    void update();
    void setMode(Mode mode);
    void reloadSettings();

private:
    Adafruit_NeoPixel _pixels;
    Mode _currentMode;
    uint8_t _pin;
    bool _enabled;

    // Animation Variablen
    unsigned long _lastUpdate;
    float _pulsePhase; // 0 bis 2*PI
    
    // Hilfsfunktion für Farben (R, G, B)
    void setColor(uint8_t r, uint8_t g, uint8_t b);
};

extern LedManager ledManager;

#endif
