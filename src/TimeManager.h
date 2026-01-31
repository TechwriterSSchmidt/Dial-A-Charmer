#ifndef TIMEMANAGER_H
#define TIMEMANAGER_H

#include <Arduino.h>
#include <time.h>
#include <vector>
#include <RTClib.h>
#include <Preferences.h>
#include "config.h"
#include "Settings.h"

class TimeManager {
public:
    enum TimeSource {
        NONE,
        NTP,
        RTC
    };
    
    struct Alarm {
        int hour;
        int minute;
        bool active;
        bool days[7]; // Sun=0, Sat=6
    };

    struct DateTime {
        int year;
        int month;
        int day;
        int hour;
        int minute;
        int second;
        time_t rawTime; // Added for easy tm conversion
        bool valid;
    };

    void begin();
    void loop();
    
    // Time Access
    DateTime getLocalTime();
    bool isTimeSet();
    TimeSource getSource();

    // Alarm Management
    void setAlarm(int hour, int minute); // Simple wrapper
    void addAlarm(int h, int m, bool active = true, uint8_t daysBitmap = 0b01111111);
    void deleteAlarm(int index = 0);
    bool isAlarmSet();
    String getAlarmString(); // HH:MM
    std::vector<Alarm> getAlarms();

    // Persistence
    void loadAlarms();
    void saveAlarms();
    
    // Global Alarm Control
    void setAlarmsEnabled(bool enabled);
    bool areAlarmsEnabled();
    void setSkipNextAlarm(bool skip);
    bool isSkipNextAlarmSet(); // Added getter

    // Timer Management
    void setTimer(int minutes);
    void cancelTimer();
    bool isTimerRunning();
    unsigned long getTimerRemainingMs();

    // Flags for Main Loop to trigger ringing
    bool checkAlarmTrigger(); 
    bool checkTimerTrigger();
    
    // Snooze
    void startSnooze();
    void cancelSnooze();
    bool isSnoozeActive(); 
    bool checkSnoozeExpired(); // Added

    // Deep Sleep Helper
    long getSecondsToNextAlarm();

private:
    TimeSource _currentSource = NONE;
    RTC_DS3231 rtc;
    Preferences prefs;
    
    // Alarms
    std::vector<Alarm> alarms;
    bool _alarmTriggeredToday = false;
    int _lastCheckedMinute = -1;
    bool _alarmsEnabled = true;
    bool _skipNextAlarm = false;

    // Timer
    unsigned long _timerStart = 0;
    unsigned long _timerDuration = 0;
    bool _timerRunning = false;

    // Snooze
    unsigned long _snoozeEndTime = 0;
    bool _snoozeActive = false;

    // NTP Checks
    unsigned long _lastNtpSync = 0;
    void syncNtp(); // Internal helper
};

extern TimeManager timeManager;

#endif
