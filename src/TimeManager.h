#ifndef TIMEMANAGER_H
#define TIMEMANAGER_H

#include <Arduino.h>
#include <time.h>
#include "config.h"
#include "Settings.h"

class TimeManager {
public:
    enum TimeSource {
        NONE,
        NTP
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
    void setAlarm(int hour, int minute);
    void deleteAlarm();
    bool isAlarmSet();
    String getAlarmString(); // HH:MM
    
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

private:
    TimeSource _currentSource = NONE;
    
    // Alarm
    int _alarmHour = -1;
    int _alarmMinute = -1;
    bool _alarmTriggeredToday = false;
    int _lastCheckedMinute = -1;
    bool _alarmsEnabled = true;
    bool _skipNextAlarm = false;

    // Timer
    unsigned long _timerEndTime = 0;
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
