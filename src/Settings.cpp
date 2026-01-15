#include "Settings.h"
#include "config.h"

Settings settings;

void Settings::begin() {
    _prefs.begin(_ns, false);
}

String Settings::getWifiSSID() {
    return _prefs.getString("ssid", "");
}

void Settings::setWifiSSID(String ssid) {
    _prefs.putString("ssid", ssid);
}

String Settings::getWifiPass() {
    return _prefs.getString("pass", "");
}

void Settings::setWifiPass(String pass) {
    _prefs.putString("pass", pass);
}

int Settings::getTimezoneOffset() {
    return _prefs.getInt("tz_offset", CONF_DEFAULT_TZ); 
}

void Settings::setTimezoneOffset(int offset) {
    _prefs.putInt("tz_offset", offset);
}

String Settings::getGeminiKey() {
    return _prefs.getString("gemini_key", "");
}

void Settings::setGeminiKey(String key) {
    _prefs.putString("gemini_key", key);
}

int Settings::getVolume() {
    return _prefs.getInt("vol", CONF_DEFAULT_VOL);
}

void Settings::setVolume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 42) vol = 42;
    _prefs.putInt("vol", vol);
}

int Settings::getBaseVolume() {
    return _prefs.getInt("base_vol", 30); // Default reasonable vol
}

void Settings::setBaseVolume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 21) vol = 21; // MAX98357 is LOUD, maybe cap smaller or use same scale 0-42. 
                            // Audio lib is 0-21. Let's use 0-21 native for this one or map?
                            // Consistency: Handset is 0-42 mapped to 0-21. Let's stick to 0-42.
    if (vol > 42) vol = 42;
    _prefs.putInt("base_vol", vol);
}

int Settings::getRingtone() {
    return _prefs.getInt("ring", CONF_DEFAULT_RING);
}

void Settings::setRingtone(int toneIndex) {
    _prefs.putInt("ring", toneIndex);
}

int Settings::getLedDayBright() {
    return _prefs.getInt("led_day_b", 100); // Default 100 (approx 40%)
}

void Settings::setLedDayBright(int bright) {
    if (bright < 0) bright = 0;
    if (bright > 255) bright = 255;
    _prefs.putInt("led_day_b", bright);
}

int Settings::getLedNightBright() {
    return _prefs.getInt("led_night_b", 10); // Default 10 (very dim)
}

void Settings::setLedNightBright(int bright) {
    if (bright < 0) bright = 0;
    if (bright > 255) bright = 255;
    _prefs.putInt("led_night_b", bright);
}

int Settings::getNightStartHour() {
    return _prefs.getInt("night_start", 22); // 22:00
}

void Settings::setNightStartHour(int hour) {
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;
    _prefs.putInt("night_start", hour);
}

int Settings::getNightEndHour() {
    return _prefs.getInt("night_end", 6); // 06:00
}

void Settings::setNightEndHour(int hour) {
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;
    _prefs.putInt("night_end", hour);
}

void Settings::clear() {
    _prefs.clear();
}
