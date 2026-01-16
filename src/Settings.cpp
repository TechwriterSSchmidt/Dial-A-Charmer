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

// Periodic Alarm
int Settings::getAlarmHour(int day) {
    if(day < 0 || day > 6) return 7;
    char key[10]; sprintf(key, "alm_h_%d", day);
    return _prefs.getInt(key, 7);
}

void Settings::setAlarmHour(int day, int h) {
    if(day < 0 || day > 6) return;
    char key[10]; sprintf(key, "alm_h_%d", day);
    _prefs.putInt(key, h);
}

int Settings::getAlarmMinute(int day) {
    if(day < 0 || day > 6) return 0;
    char key[10]; sprintf(key, "alm_m_%d", day);
    return _prefs.getInt(key, 0);
}

void Settings::setAlarmMinute(int day, int m) {
    if(day < 0 || day > 6) return;
    char key[10]; sprintf(key, "alm_m_%d", day);
    _prefs.putInt(key, m);
}

bool Settings::isAlarmEnabled(int day) {
    if(day < 0 || day > 6) return false;
    char key[10]; sprintf(key, "alm_en_%d", day);
    return _prefs.getBool(key, false); 
}

void Settings::setAlarmEnabled(int day, bool enabled) {
    if(day < 0 || day > 6) return;
    char key[10]; sprintf(key, "alm_en_%d", day);
    _prefs.putBool(key, enabled);
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
    if (vol > 42) vol = 42;
    _prefs.putInt("base_vol", vol);
}

int Settings::getSnoozeMinutes() {
    return _prefs.getInt("snooze", 9); // Default 9
}

void Settings::setSnoozeMinutes(int min) {
    if (min < 0) min = 0;
    if (min > 20) min = 20;
    _prefs.putInt("snooze", min);
}

int Settings::getRingtone() {
    return _prefs.getInt("ring", CONF_DEFAULT_RING);
}

void Settings::setRingtone(int toneIndex) {
    _prefs.putInt("ring", toneIndex);
}

int Settings::getDialTone() {
    return _prefs.getInt("dt_idx", 1); // Default 1
}

void Settings::setDialTone(int toneIndex) {
    _prefs.putInt("dt_idx", toneIndex);
}

bool Settings::getHalfDuplex() {
    return _prefs.getBool("half_duplex", true); // Default ON
}

void Settings::setHalfDuplex(bool enabled) {
    _prefs.putBool("half_duplex", enabled);
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

String Settings::getLanguage() {
    return _prefs.getString("lang", "de");
}

void Settings::setLanguage(String lang) {
    if (lang != "de" && lang != "en") lang = "de";
    _prefs.putString("lang", lang);
}

void Settings::clear() {
    _prefs.clear();
}
