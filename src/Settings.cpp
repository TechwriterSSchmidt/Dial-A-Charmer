#include "Settings.h"

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
    return _prefs.getInt("tz_offset", 1); // Default UTC+1
}

void Settings::setTimezoneOffset(int offset) {
    _prefs.putInt("tz_offset", offset);
}

void Settings::clear() {
    _prefs.clear();
}
