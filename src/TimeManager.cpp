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


// Helper to implement Smart Wakeup Calculation
long TimeManager::getSecondsToNextAlarm() {
    // Return -1 if no alarms are pending in the near future (e.g. infinite sleep until Hook)
    // If not enabled globally, no alarms at all.
    if (!_alarmsEnabled) return -1;

    DateTime now = getLocalTime();
    if (!now.valid) return -1; // Without time, we cannot calculate diff

    long minSecondsDiff = -1;

    // 1. Check Single/One-Shot Alarm
    // This is priority.
    if (isAlarmSet()) {
         long currentTotalSeconds = (now.hour * 3600) + (now.minute * 60) + now.second;
         long alarmTotalSeconds = (_alarmHour * 3600) + (_alarmMinute * 60);

         // Single Alarm is implicitly "Today" unless time passed? 
         // Logic in checkAlarmTrigger handles exact minute. 
         // Let's assume Single Alarm is "Next Occurrence Today".
         // If passed, we assume setup clears it or user clears it. 
         // If user set alarm 08:00 at 09:00, is it tomorrow? 
         // Implementation of setAlarm does not store Date. 
         // Assumption: If AlarmTime > CurrentTime, it's today. Else Tomorrow.
         
         long diff = 0;
         if (alarmTotalSeconds > currentTotalSeconds) {
             diff = alarmTotalSeconds - currentTotalSeconds;
         } else {
             // Tomorrow
             diff = (24 * 3600 - currentTotalSeconds) + alarmTotalSeconds;
         }
         
         minSecondsDiff = diff;
    }

    // 2. Check Repeating Alarms (Next 7 days)
    for (int dayOffset = 0; dayOffset < 8; dayOffset++) {
        // tm_wday: 0=Sun..6=Sat.
        // We need 0..6 (Sun..Sat) for calculation math, but need to map to our Settings Index (Mon=0..Sun=6).
        int currentDayWday = (int)((now.year - 1900) * 365.25); // Rough... no, use tm struct info implied in now variable? 
        // We lack DayOfWeek in our custom DateTime struct.
        // Let's re-get it properly.
        struct tm tinfo;
        if (! ::getLocalTime(&tinfo)) break; 

        // Current check day
        int checkWday = (tinfo.tm_wday + dayOffset) % 7; 
        
        // Convert to Settings Index: Mon(1)->0, ..., Sat(6)->5, Sun(0)->6
        int settingsIdx = (checkWday == 0) ? 6 : (checkWday - 1);

        if (settings.isAlarmEnabled(settingsIdx)) {
             int aH = settings.getAlarmHour(settingsIdx);
             int aM = settings.getAlarmMinute(settingsIdx);

             long currentTotalSeconds = (tinfo.tm_hour * 3600) + (tinfo.tm_min * 60) + tinfo.tm_sec;
             long alarmTotalSeconds = (aH * 3600) + (aM * 60);

             // If checking today (dayOffset==0)
             if (dayOffset == 0) {
                 if (currentTotalSeconds >= alarmTotalSeconds) continue; // Already passed today
                 
                 // If Skip Next is set, and this is the VERY FIRST alarm we find?
                 // The logic says "Next recurring alarm".
                 // If we find one today, and skip is active -> SKIP IT.
                 if (_skipNextAlarm) {
                     // We found the next alarm, but we skip it.
                     // IMPORTANT: We do NOT consume the flag here (const method), 
                     // but we behave as if it's not the target.
                     // But wait, if we skip THIS one, we need the NEXT one.
                     // So we continue the loop?
                     // Yes. But we must ensure we don't apply skip logic to the NEXT one found.
                     // This simple logic is complex for "peek". 
                     // Let's simplify: If skipNext is TRUE, we just ignore the FIRST valid match we find.
                     // But since we iterate day 0..7, the first match IS the next alarm.
                     // So we just iterate.
                     // Actually, we can't modify state. 
                     // Complex. Let's assume for Deep Sleep optimization, waking up and instantly skipping 
                     // (then sleeping again) is acceptable overhead compared to coding complex date math here.
                     // Plan: Wake up for the skipped alarm, checkAlarmTrigger will return false (and consume flag),
                     // device will sleep again. Safe and Robust.
                 }
                 
                 long diff = alarmTotalSeconds - currentTotalSeconds;
                 if (minSecondsDiff == -1 || diff < minSecondsDiff) minSecondsDiff = diff;
             } else {
                 // Future Day
                 long secUntilMidnight = (24 * 3600) - currentTotalSeconds; // From NOW
                 // Wait, this math is for dayOffset relative to NOW.
                 // Correct logic:
                 // Diff = (Seconds until End of Today) + ((Offset-1) * Full Days) + (Alarm Time on Target Day)
                 long diff = (24 * 3600 - ((tinfo.tm_hour * 3600) + (tinfo.tm_min * 60) + tinfo.tm_sec))
                             + ((dayOffset - 1) * 24 * 3600)
                             + alarmTotalSeconds;
                 
                 if (minSecondsDiff == -1 || diff < minSecondsDiff) minSecondsDiff = diff;
             }
        }
    }
    
    return minSecondsDiff;
}

