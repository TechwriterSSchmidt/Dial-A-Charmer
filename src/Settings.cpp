#include "Settings.h"
#include "config.h"

Settings settings;

// --- Interne Cache-Struktur (Trick, um Header nicht ändern zu müssen) ---
struct InternalCache {
    bool loaded = false;
    
    // Alle Werte, die wir cachen müssen
    String ssid;
    String pass;
    String geminiKey;
    String personaName; // New
    String lang;
    int tzOffset;
    int vol;
    int baseVol;
    int snooze;
    String ringtone;
    String dialTone;
    int ledDay;
    int ledNight;
    int nightStart;
    int nightEnd;
    bool halfDuplex;
};

static InternalCache ic; // Statische Instanz, nur in dieser Datei sichtbar

static bool isNumericString(const String& value) {
    if (value.length() == 0) return false;
    for (size_t i = 0; i < value.length(); i++) {
        if (!isDigit(value[i])) return false;
    }
    return true;
}

void Settings::begin() {
    _prefs.begin(_ns, false);
    loadCache();
}

void Settings::loadCache() {
    // 1. Alarme Cachen (wie gehabt)
    if (!_cacheLoaded) {
        for(int i=0; i<7; i++) {
            char kH[10], KM[10], kE[10], kT[10];
            sprintf(kH, "alm_h_%d", i);
            sprintf(KM, "alm_m_%d", i);
            sprintf(kE, "alm_en_%d", i);
            sprintf(kT, "alm_t_%d", i);
            
            _alarms[i].h = (int8_t)_prefs.getInt(kH, 7);
            _alarms[i].m = (int8_t)_prefs.getInt(KM, 0);
            _alarms[i].en = _prefs.getBool(kE, false);
            _alarms[i].tone = (int8_t)_prefs.getInt(kT, 1); // Default to tone index 1
        }
        _cacheLoaded = true;
    }

    // 2. Restliche Einstellungen in unseren internen Cache laden
    if (!ic.loaded) {
        ic.ssid = _prefs.getString("ssid", "");
        ic.pass = _prefs.getString("pass", "");
        ic.geminiKey = _prefs.getString("gemini_key", "");
        ic.personaName = _prefs.getString("persona", "Gast"); // Default unpersönlich
        ic.lang = _prefs.getString("lang", "de");
        ic.tzOffset = _prefs.getInt("tz_offset", CONF_DEFAULT_TZ);
        ic.vol = _prefs.getInt("vol", CONF_DEFAULT_VOL);
        ic.baseVol = _prefs.getInt("base_vol", 30);
        ic.snooze = _prefs.getInt("snooze", 9);
        ic.ringtone = _prefs.getString("ring", "");
        if (ic.ringtone.length() == 0) {
            int legacyRing = _prefs.getInt("ring", CONF_DEFAULT_RING);
            ic.ringtone = String(legacyRing) + ".wav";
            _prefs.putString("ring", ic.ringtone);
        } else if (isNumericString(ic.ringtone)) {
            ic.ringtone = ic.ringtone + ".wav";
            _prefs.putString("ring", ic.ringtone);
        } else if (ic.ringtone.indexOf('.') < 0) {
            ic.ringtone = ic.ringtone + ".wav";
            _prefs.putString("ring", ic.ringtone);
        }

        ic.dialTone = _prefs.getString("dt_idx", "");
        if (ic.dialTone.length() == 0) {
            int legacyDial = _prefs.getInt("dt_idx", 2);
            ic.dialTone = "dialtone_" + String(legacyDial) + ".wav";
            _prefs.putString("dt_idx", ic.dialTone);
        } else if (isNumericString(ic.dialTone)) {
            ic.dialTone = "dialtone_" + ic.dialTone + ".wav";
            _prefs.putString("dt_idx", ic.dialTone);
        } else if (ic.dialTone.indexOf('.') < 0) {
            ic.dialTone = ic.dialTone + ".wav";
            _prefs.putString("dt_idx", ic.dialTone);
        }
        ic.ledDay = _prefs.getInt("led_day_b", 100);
        ic.ledNight = _prefs.getInt("led_night_b", 10);
        ic.nightStart = _prefs.getInt("night_start", 22);
        ic.nightEnd = _prefs.getInt("night_end", 6);
        ic.halfDuplex = _prefs.getBool("half_duplex", true);
        
        ic.loaded = true;
        Serial.println("[Settings] Full Cache Loaded (RAM)");
    }
}

String Settings::getWifiSSID() {
    if(!ic.loaded) loadCache();
    return ic.ssid;
}

void Settings::setWifiSSID(String ssid) {
    ic.ssid = ssid; // Update Cache
    _prefs.putString("ssid", ssid); // Update Flash
}

String Settings::getWifiPass() {
    if(!ic.loaded) loadCache();
    return ic.pass;
}

void Settings::setWifiPass(String pass) {
    ic.pass = pass;
    _prefs.putString("pass", pass);
}

int Settings::getTimezoneOffset() {
    if(!ic.loaded) loadCache();
    return ic.tzOffset;
}

void Settings::setTimezoneOffset(int offset) {
    ic.tzOffset = offset;
    _prefs.putInt("tz_offset", offset);
}

// Periodic Alarm
int Settings::getAlarmHour(int day) {
    if(day < 0 || day > 6) return 7;
    if(!_cacheLoaded) loadCache();
    return _alarms[day].h;
}

void Settings::setAlarmHour(int day, int h) {
    if(day < 0 || day > 6) return;
    if(!_cacheLoaded) loadCache();
    _alarms[day].h = (int8_t)h;
    
    char key[10]; sprintf(key, "alm_h_%d", day);
    _prefs.putInt(key, h);
}

int Settings::getAlarmMinute(int day) {
    if(day < 0 || day > 6) return 0;
    if(!_cacheLoaded) loadCache();
    return _alarms[day].m;
}

void Settings::setAlarmMinute(int day, int m) {
    if(day < 0 || day > 6) return;
    if(!_cacheLoaded) loadCache();
    _alarms[day].m = (int8_t)m;

    char key[10]; sprintf(key, "alm_m_%d", day);
    _prefs.putInt(key, m);
}

int Settings::getAlarmTone(int day) {
    if(day < 0 || day > 6) return 1;
    if(!_cacheLoaded) loadCache();
    return _alarms[day].tone;
}

void Settings::setAlarmTone(int day, int toneIndex) {
    if(day < 0 || day > 6) return;
    if(!_cacheLoaded) loadCache();
    _alarms[day].tone = (int8_t)toneIndex;

    char key[10]; sprintf(key, "alm_t_%d", day);
    _prefs.putInt(key, toneIndex);
}

bool Settings::isAlarmEnabled(int day) {
    if(day < 0 || day > 6) return false;
    if(!_cacheLoaded) loadCache();
    return _alarms[day].en;
}

void Settings::setAlarmEnabled(int day, bool enabled) {
    if(day < 0 || day > 6) return;
    if(!_cacheLoaded) loadCache();
    _alarms[day].en = enabled;

    char key[10]; sprintf(key, "alm_en_%d", day);
    _prefs.putBool(key, enabled);
}

String Settings::getGeminiKey() {
    if(!ic.loaded) loadCache();
    return ic.geminiKey;
}

void Settings::setGeminiKey(String key) {
    ic.geminiKey = key;
    _prefs.putString("gemini_key", key);
}

int Settings::getVolume() {
    if(!ic.loaded) loadCache();
    return ic.vol;
}

void Settings::setVolume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 42) vol = 42;
    ic.vol = vol;
    _prefs.putInt("vol", vol);
}

int Settings::getBaseVolume() {
     if(!ic.loaded) loadCache();
    return ic.baseVol;
}

void Settings::setBaseVolume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 42) vol = 42;
    ic.baseVol = vol;
    _prefs.putInt("base_vol", vol);
}

int Settings::getSnoozeMinutes() {
    if(!ic.loaded) loadCache();
    return ic.snooze;
}

void Settings::setSnoozeMinutes(int min) {
    if (min < 0) min = 0;
    if (min > 20) min = 20;
    ic.snooze = min;
    _prefs.putInt("snooze", min);
}

String Settings::getRingtone() {
    if(!ic.loaded) loadCache();
    return ic.ringtone;
}

void Settings::setRingtone(String toneFilename) {
    ic.ringtone = toneFilename;
    _prefs.putString("ring", toneFilename);
}

String Settings::getDialTone() {
    if(!ic.loaded) loadCache();
    return ic.dialTone;
}

void Settings::setDialTone(String toneFilename) {
    ic.dialTone = toneFilename;
    _prefs.putString("dt_idx", toneFilename);
}

bool Settings::getHalfDuplex() {
    if(!ic.loaded) loadCache();
    return ic.halfDuplex;
}

void Settings::setHalfDuplex(bool enabled) {
    ic.halfDuplex = enabled;
    _prefs.putBool("half_duplex", enabled);
}

int Settings::getLedDayBright() {
    if(!ic.loaded) loadCache();
    return ic.ledDay;
}

void Settings::setLedDayBright(int bright) {
    if (bright < 0) bright = 0;
    if (bright > 255) bright = 255;
    ic.ledDay = bright;
    _prefs.putInt("led_day_b", bright);
}

int Settings::getLedNightBright() {
     if(!ic.loaded) loadCache();
    return ic.ledNight;
}

void Settings::setLedNightBright(int bright) {
    if (bright < 0) bright = 0;
    if (bright > 255) bright = 255;
    ic.ledNight = bright;
    _prefs.putInt("led_night_b", bright);
}

int Settings::getNightStartHour() {
    if(!ic.loaded) loadCache();
    return ic.nightStart;
}

void Settings::setNightStartHour(int hour) {
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;
    ic.nightStart = hour;
    _prefs.putInt("night_start", hour);
}

int Settings::getNightEndHour() {
    if(!ic.loaded) loadCache();
    return ic.nightEnd;
}

void Settings::setNightEndHour(int hour) {
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;
    ic.nightEnd = hour;
    _prefs.putInt("night_end", hour);
}

String Settings::getLanguage() {
    if(!ic.loaded) loadCache();
    return ic.lang;
}

void Settings::setLanguage(String lang) {
    if (lang != "de" && lang != "en") lang = "de";
    ic.lang = lang;
    _prefs.putString("lang", lang);
}

void Settings::clear() {
    _prefs.clear();
    ic.loaded = false;
    _cacheLoaded = false;
}

String Settings::getPersonaName() {
    if(!ic.loaded) loadCache();
    return ic.personaName;
}

void Settings::setPersonaName(String name) {
    ic.personaName = name;
    _prefs.putString("persona", name);
}
