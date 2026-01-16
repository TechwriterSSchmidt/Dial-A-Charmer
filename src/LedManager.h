#ifndef LEDMANAGER_H
#define LEDMANAGER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class LedManager {
public:
    enum Mode {
        OFF,            // Komplett aus
        IDLE_GLOW,      // Warmes, goldenes Glimmen
        BREATHE_SLOW,   // Langsames Pulsieren (Blau/Gold) - Verbinden/Snooze
        BREATHE_FAST,   // Schnelles rotes Pulsieren - Alarm
        FULL_ON,        // Weißes Licht (Taschenlampe/Test)
        SOS             // Rotes SOS Signal
    };

    // Konstruktor nun mit Anzahl der LEDs (meist 1)
    LedManager(uint8_t pin, uint8_t numPixels = 1);
    void begin();
    void update();
    void setMode(Mode mode);

private:
    Adafruit_NeoPixel _pixels;
    Mode _currentMode;
    uint8_t _pin;

    // Animation Variablen
    unsigned long _lastUpdate;
    float _pulsePhase; // 0 bis 2*PI
    
    // Hilfsfunktion für Farben (R, G, B)
    void setColor(uint8_t r, uint8_t g, uint8_t b);
};

#endif
