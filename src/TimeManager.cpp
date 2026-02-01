#include "TimeManager.h"
#include <WiFi.h>

TimeManager timeManager;

// NTP Servers
const char* ntpServer = "pool.ntp.org";

void TimeManager::begin() {
    // 1. Init External RTC
#ifdef CONF_RTC_USE_WIRE1
    // Use Secondary I2C Bus (Wire1) for RTC to avoid conflict with Internal Codec (Wire)
    Wire1.begin(CONF_I2C_SDA, CONF_I2C_SCL);
    bool rtcFound = rtc.begin(&Wire1);
#else
    // Use Default I2C Bus (Wire)
    // Ensure pins are correct (though 21/22 is default for ESP32, explicit init is safer if main didn't do it)
    Wire.begin(CONF_I2C_SDA, CONF_I2C_SCL);
    bool rtcFound = rtc.begin();
#endif

    if (!rtcFound) {
        Serial.println("[Time] RTC DS3231 not found. Using internal only.");
    } else {
        if (rtc.lostPower()) {
             Serial.println("[Time] RTC lost power, needs sync.");
        } else {
             ::DateTime now = rtc.now();
             if (now.year() < 2025) {
                 Serial.println("[Time] RTC invalid (<2025). Waiting for NTP.");
             } else {
                 Serial.printf("[Time] RTC Valid: %04d-%02d-%02d %02d:%02d\n", now.year(), now.month(), now.day(), now.hour(), now.minute());
                 // Sync MSP32 internal clock from RTC
                 struct timeval tv;
                 tv.tv_sec = now.unixtime();
                 tv.tv_usec = 0;
                 settimeofday(&tv, NULL);
                 _currentSource = RTC;
             }
        }
    }

    // 2. Init NTP (System Time)
    // We use settings for timezone.
    configTime(settings.getTimezoneOffset() * 3600, 0, ntpServer);
    
    // 3. Load Alarms
    loadAlarms();
}

// --- PERSISTENCE ---

void TimeManager::loadAlarms() {
    alarms.clear();
    
    // NEW: Load strictly from Settings (Web Interface Source of Truth)
    // Settings use 0=Mon .. 6=Sun
    // TM uses simple vector of alarms. Each alarm is specific to a day+minute.
    
    Serial.println("[Time] Syncing alarms from Settings...");
    
    for (int dayIndex = 0; dayIndex < 7; dayIndex++) {
        if (settings.isAlarmEnabled(dayIndex)) {
            Alarm a;
            a.hour = settings.getAlarmHour(dayIndex);
            a.minute = settings.getAlarmMinute(dayIndex);
            a.active = true;
            
            // Clear all days
            for(int k=0; k<7; k++) a.days[k] = false;
            
            // Map Settings (0=Mon) to tm_wday (0=Sun, 1=Mon)
            // Mon(0) -> 1
            // ...
            // Sun(6) -> 0
            int tmWday = (dayIndex + 1) % 7;
            a.days[tmWday] = true;
            
            alarms.push_back(a);
            Serial.printf("[Time] Added Alarm for Day %d (tm_wday %d): %02d:%02d\n", dayIndex, tmWday, a.hour, a.minute);
        }
    }
    Serial.printf("[Time] Total active alarms loaded: %d\n", alarms.size());
}

void TimeManager::saveAlarms() {
    prefs.begin("alarms", false); // R/W
    prefs.clear();
    prefs.putInt("count", alarms.size());
    
    for (int i = 0; i < alarms.size(); i++) {
        String p = String(i);
        prefs.putInt((p + "_h").c_str(), alarms[i].hour);
        prefs.putInt((p + "_m").c_str(), alarms[i].minute);
        prefs.putBool((p + "_act").c_str(), alarms[i].active);
        
        uint8_t dMap = 0;
        for(int k=0; k<7; k++) if(alarms[i].days[k]) dMap |= (1<<k);
        prefs.putUChar((p + "_days").c_str(), dMap);
    }
    prefs.end();
    Serial.println("[Time] Alarms saved.");
}

void TimeManager::loop() {
    // 1. Sync Check
    // If we have WiFi and system time is updated by NTP, update RTC
    static unsigned long lastRtcSync = 0;
    if (WiFi.status() == WL_CONNECTED && (millis() - lastRtcSync > 3600000)) { // Once per hour
        struct tm timeinfo;
        if (::getLocalTime(&timeinfo, 0)) {
            _currentSource = NTP;
            rtc.adjust(::DateTime(timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
            lastRtcSync = millis();
            Serial.println("[Time] Synced RTC from NTP");
        }
    } 
}

TimeManager::DateTime TimeManager::getLocalTime() {
    DateTime dt = {0,0,0,0,0, 0, 0, false};
    struct tm timeinfo;
    
    if (::getLocalTime(&timeinfo, 10)) { 
        dt.year = timeinfo.tm_year + 1900;
        dt.month = timeinfo.tm_mon + 1;
        dt.day = timeinfo.tm_mday;
        dt.hour = timeinfo.tm_hour;
        dt.minute = timeinfo.tm_min;
        dt.second = timeinfo.tm_sec;
        dt.rawTime = mktime(&timeinfo);
        dt.valid = true;
        return dt;
    }
    return dt;
}

bool TimeManager::isTimeSet() {
    return getLocalTime().valid;
}

TimeManager::TimeSource TimeManager::getSource() {
    return _currentSource;
}

// --- Alarm ---

void TimeManager::setAlarm(int hour, int minute) {
    // Legacy mapping: Update ALL entries in Settings to this time and reload.
    // This supports the "Hold Extra Button + Dial 4 digits" simple mode.
    // We assume the user wants this time for all currently enabled days?
    // Or just force enable everything? Let's just set the time for all slots 
    // to match the "Simple Alarm Clock" metaphor.
    
    Serial.printf("[Time] Simple Alarm Set via Dial to %02d:%02d\n", hour, minute);
    
    for(int i=0; i<7; i++) {
        settings.setAlarmHour(i, hour);
        settings.setAlarmMinute(i, minute);
        settings.setAlarmEnabled(i, true); // Force enable
    }
    
    loadAlarms(); // Reload internal vector
}

void TimeManager::addAlarm(int h, int m, bool active, uint8_t daysBitmap) {
    // Deprecated - do not use for new logic
}

void TimeManager::deleteAlarm(int index) {
    // Deprecated
}

// ... unchanged ...

bool TimeManager::isAlarmSet() {
    return !alarms.empty();
}

std::vector<TimeManager::Alarm> TimeManager::getAlarms() {
    return alarms;
}

String TimeManager::getAlarmString() {
    if (alarms.empty()) return "--:--";
    char buf[10];
    sprintf(buf, "%02d:%02d", alarms[0].hour, alarms[0].minute);
    return String(buf);
}

bool TimeManager::checkAlarmTrigger() {
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 1000) return false;
    lastCheck = millis();

    DateTime now = getLocalTime();
    if (!now.valid) return false;

    if (now.minute != _lastCheckedMinute) {
        _alarmTriggeredToday = false; 
        _lastCheckedMinute = now.minute;
    }

    if (_alarmTriggeredToday) return false; 
    if (!_alarmsEnabled) return false;

    // Iterate Alarms
    struct tm tinfo;
    ::getLocalTime(&tinfo); // need wday
    int todayIndex = (tinfo.tm_wday == 0) ? 6 : (tinfo.tm_wday - 1); // Mon=0 .. Sun=6 needed?
    // struct tm wday: 0=Sun, 1=Mon ... 6=Sat
    // Our array: 0=Sun, 1=Mon ... 6=Sat (Standard comp)
    int day = tinfo.tm_wday; 

    for (int i = 0; i < alarms.size(); i++) {
        if (alarms[i].active && alarms[i].days[day]) {
            if (now.hour == alarms[i].hour && now.minute == alarms[i].minute) {
                // Trigger!
                if (_skipNextAlarm) {
                    _skipNextAlarm = false;
                    _alarmTriggeredToday = true; // Consume minute
                    return false;
                }
                
                _alarmTriggeredToday = true;
                // If one-shot (all days full? No, we don't have one-shot flag in struct, assuming repeating for now unless we implement logic)
                return true;
            }
        }
    }
    
    return false;
}


void TimeManager::setAlarmsEnabled(bool enabled) {
    _alarmsEnabled = enabled;
}

bool TimeManager::areAlarmsEnabled() {
    return _alarmsEnabled;
}

void TimeManager::setSkipNextAlarm(bool skip) {
    _skipNextAlarm = skip;
}

bool TimeManager::isSkipNextAlarmSet() {
    return _skipNextAlarm;
}

// --- Timer ---

void TimeManager::setTimer(int minutes) {
    if (_timerRunning) {
        Serial.println("TimeManager: Overwriting existing timer");
    }
    _timerStart = millis();
    _timerDuration = minutes * 60000UL;
    _timerRunning = true; 
}

void TimeManager::cancelTimer() {
    _timerRunning = false;
}

bool TimeManager::isTimerRunning() {
    return _timerRunning;
}

unsigned long TimeManager::getTimerRemainingMs() {
    if (!_timerRunning) return 0;
    unsigned long elapsed = millis() - _timerStart;
    if (elapsed >= _timerDuration) return 0;
    return _timerDuration - elapsed;
}

bool TimeManager::checkTimerTrigger() {
    if (_timerRunning) {
        if ((millis() - _timerStart) >= _timerDuration) {
            _timerRunning = false; // Auto stop
            return true;
        }
    }
    return false;
}

// --- Snooze ---

void TimeManager::startSnooze() {
    int mins = settings.getSnoozeMinutes();
    _snoozeEndTime = millis() + (mins * 60000UL); 
    _snoozeActive = true;
}

void TimeManager::cancelSnooze() {
    _snoozeActive = false;
}

bool TimeManager::isSnoozeActive() {
    // If expired, we are conceptually not active, but we need checkSnoozeExpired to trigger
    return _snoozeActive;
}

bool TimeManager::checkSnoozeExpired() {
    if (_snoozeActive && millis() > _snoozeEndTime) {
        _snoozeActive = false;
        return true;
    }
    return false;
}


// Helper to implement Smart Wakeup Calculation
long TimeManager::getSecondsToNextAlarm() {
    // Return -1 if no alarms are pending in the near future (e.g. infinite sleep until Hook)
    // If not enabled globally, no alarms at all.
    if (!_alarmsEnabled) return -1;

    DateTime now = getLocalTime();
    if (!now.valid) return -1; // Without time, we cannot calculate diff

    long minSecondsDiff = -1;

    // 1. Check Alarms
    if (isAlarmSet()) {
         long currentTotalSeconds = (now.hour * 3600) + (now.minute * 60) + now.second;
         
         for (const auto& alarm : alarms) {
             if (!alarm.active) continue;
             long alarmTotalSeconds = (alarm.hour * 3600) + (alarm.minute * 60);
             long diff = alarmTotalSeconds - currentTotalSeconds;
             if (diff < 0) diff += 86400; // Next day
             
             if (minSecondsDiff == -1 || diff < minSecondsDiff) {
                 minSecondsDiff = diff;
             }
         }
         // Note: Days of week check omitted for simple calc, assuming daily for wakeup hint
    }
    
    return minSecondsDiff;
} 


