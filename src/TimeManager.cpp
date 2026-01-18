#include "TimeManager.h"
#include <WiFi.h>

TimeManager timeManager;

// NTP Servers
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0; // We handle timezone via offset addition manually or set it here? 
// Better set it here so struct tm is correct local time
// But Settings has an int offset (hours). 
// configTime takes seconds offset.

void TimeManager::begin() {
    // NTP Init (Config but don't force sync yet)
    // We use settings for timezone.
    configTime(settings.getTimezoneOffset() * 3600, 0, ntpServer);
}

void TimeManager::loop() {
    // 1. NTP Sync Check (If WiFi is there)
    if (WiFi.status() == WL_CONNECTED) {
        // configTime keeps syncing in background, we just check if we have time
        // Use non-blocking overload (tm struct, ms timeout)
        struct tm timeinfo;
        if (::getLocalTime(&timeinfo, 0)) {
            _currentSource = NTP;
        }
    } 

    // 2. Logic Update (Reset Alarm Trigger at midnight, etc)
    // handled in checkAlarmTrigger
}

TimeManager::DateTime TimeManager::getLocalTime() {
    DateTime dt = {0,0,0,0,0, 0, 0, false};
    
    // Strategy: Prefer System Time (NTP/RTC) if set, else GPS
    struct tm timeinfo;
    // local time (ESP32 RTC)
    if (::getLocalTime(&timeinfo, 50)) { // 50ms timeout
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
    _alarmHour = hour;
    _alarmMinute = minute;
    _alarmTriggeredToday = false;
}

void TimeManager::deleteAlarm() {
    _alarmHour = -1;
    _alarmMinute = -1;
}

bool TimeManager::isAlarmSet() {
    return (_alarmHour != -1 && _alarmMinute != -1);
}

String TimeManager::getAlarmString() {
    if (!isAlarmSet()) return "--:--";
    char buf[10];
    sprintf(buf, "%02d:%02d", _alarmHour, _alarmMinute);
    return String(buf);
}

bool TimeManager::checkAlarmTrigger() {
    // Limit checks to prevent NVS saturation (though now Cached)
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 1000) return false;
    lastCheck = millis();

    DateTime now = getLocalTime();
    if (!now.valid) return false;

    // Reset trigger if minute changed
    if (now.minute != _lastCheckedMinute) {
        _alarmTriggeredToday = false; 
        _lastCheckedMinute = now.minute;
    }

    if (_alarmTriggeredToday) return false; // Already handled this minute

    if (!_alarmsEnabled) return false;

    // 1. Check Single/Manual Alarm (High Priority, never skipped by "Skip Next")
    if (isAlarmSet()) {
        if (now.hour == _alarmHour && now.minute == _alarmMinute) {
            _alarmTriggeredToday = true;
            deleteAlarm(); // Single alarm is one-shot
            return true;
        }
    }

    // 2. Check Periodic Alarm (From Settings)
    struct tm tinfo;
    if (::getLocalTime(&tinfo)) {
         // tm_wday: 0=Sun, 1=Mon... 6=Sat
         // Our Settings: 0=Mon, 1=Tue... 6=Sun
         int dayIndex = (tinfo.tm_wday == 0) ? 6 : (tinfo.tm_wday - 1);
         
         if (settings.isAlarmEnabled(dayIndex)) {
             int pHour = settings.getAlarmHour(dayIndex);
             int pMin = settings.getAlarmMinute(dayIndex);
             
             if (now.hour == pHour && now.minute == pMin) {
                 _alarmTriggeredToday = true;
                     
                     // Check Skip Logic
                     if (_skipNextAlarm) {
                         _skipNextAlarm = false; // Consumed
                         return false; 
                     }
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
    _timerEndTime = millis() + (minutes * 60000UL);
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
    if (millis() > _timerEndTime) return 0;
    return _timerEndTime - millis();
}

bool TimeManager::checkTimerTrigger() {
    if (_timerRunning && millis() > _timerEndTime) {
        _timerRunning = false; // Auto stop
        return true;
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

// Helper to check if snooze JUST finished
// This logic is tricky to separate purely. 
// Let's add specific trigger check for snooze end.
// For now, main loop checks `isSnoozeActive()`? 
// No, main loop needs to know when to start ringing.

