#pragma once
#include <time.h>
#include "esp_err.h"

#include <string>

struct DayAlarm {
    int hour;
    int minute;
    bool active;
    bool volumeRamp;
    std::string ringtone;
};

class TimeManager {
public:
    static void init();
    
    // dayIndex: 0=Sunday, 1=Monday, ..., 6=Saturday
    static void setAlarm(int dayIndex, int hour, int minute, bool active, bool volumeRamp, const char* ringtone); 
    static DayAlarm getAlarm(int dayIndex);
    
    static bool checkAlarm(); // Returns true if alarm trigger condition is met ONE TIME
    static bool isAlarmRinging();
    static void stopAlarm();
    
    static struct tm getCurrentTime();

    // Timezone Handling
    static void setTimezone(const char* tz);
    static std::string getTimezone();
};
