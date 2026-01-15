#include "LedStatus.h"

LedStatus statusLed;

void LedStatus::begin() {
    FastLED.addLeds<WS2812, CONF_PIN_LED, GRB>(_leds, CONF_AC_EXC_LED_NUM);
    setBooting();
}

void LedStatus::loop() {
    // Basic pulse/animation logic could go here
}

void LedStatus::setBooting() {
    _leds[0] = CRGB::Blue;
    FastLED.show();
}

void LedStatus::setWifiConnecting() {
    _leds[0] = CRGB::Yellow;
    FastLED.show();
}

void LedStatus::setWifiConnected() {
    _leds[0] = CRGB::Green;
    FastLED.show();
    delay(1000);
    setIdle();
}

void LedStatus::setAPMode() {
    _leds[0] = CRGB::Purple;
    FastLED.show();
}

void LedStatus::setGpsSearching() {
    _leds[0] = CRGB::Orange;
    FastLED.show();
}

void LedStatus::setGpsLocked() {
    _leds[0] = CRGB::Cyan;
    FastLED.show();
}

void LedStatus::setTalking() {
    _leds[0] = CRGB::White;
    FastLED.show();
}

void LedStatus::setIdle() {
    _leds[0] = CRGB::Black; // Off to save power/annoyance
    FastLED.show();
}

void LedStatus::setWarning() {
    _leds[0] = CRGB::Red;
    FastLED.show();
}
