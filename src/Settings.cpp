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

int Settings::getRingtone() {
    return _prefs.getInt("ring", CONF_DEFAULT_RING);
}

void Settings::setRingtone(int toneIndex) {
    _prefs.putInt("ring", toneIndex);
}

void Settings::clear() {
    _prefs.clear();
}
