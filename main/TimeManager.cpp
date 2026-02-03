#include "TimeManager.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "TIME_MANAGER";
static bool _alarm_ringing = false;
static int _last_triggered_day = -1; // To ensure we trigger only once per day

void TimeManager::init() {
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    
    // Set Timezone to CET/CEST (Berlin)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

void TimeManager::setAlarm(int dayIndex, int hour, int minute, bool active, bool volumeRamp, const char* ringtone) {
    if (dayIndex < 0 || dayIndex > 6) return;

    nvs_handle_t my_handle;
    if (nvs_open("dialcharm", NVS_READWRITE, &my_handle) == ESP_OK) {
        char key_h[16], key_m[16], key_en[16], key_rmp[16], key_snd[16];
        snprintf(key_h, sizeof(key_h), "alm_%d_h", dayIndex);
        snprintf(key_m, sizeof(key_m), "alm_%d_m", dayIndex);
        snprintf(key_en, sizeof(key_en), "alm_%d_en", dayIndex);
        snprintf(key_rmp, sizeof(key_rmp), "alm_%d_rmp", dayIndex);
        snprintf(key_snd, sizeof(key_snd), "alm_%d_snd", dayIndex);

        nvs_set_i32(my_handle, key_h, hour);
        nvs_set_i32(my_handle, key_m, minute);
        nvs_set_u8(my_handle, key_en, active ? 1 : 0);
        nvs_set_u8(my_handle, key_rmp, volumeRamp ? 1 : 0);
        
        if (ringtone && strlen(ringtone) > 0) {
            nvs_set_str(my_handle, key_snd, ringtone);
        } else {
            nvs_set_str(my_handle, key_snd, "digital_alarm.wav");
        }
        
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Saved Alarm Day %d: %02d:%02d (Act:%d Rmp:%d) Tone: %s", dayIndex, hour, minute, active, volumeRamp, ringtone);
    }
}

DayAlarm TimeManager::getAlarm(int dayIndex) {
    DayAlarm alarm = {7, 0, false, false, "digital_alarm.wav"}; // Default
    if (dayIndex < 0 || dayIndex > 6) return alarm;

    nvs_handle_t my_handle;
    if (nvs_open("dialcharm", NVS_READONLY, &my_handle) == ESP_OK) {
        char key_h[16], key_m[16], key_en[16], key_rmp[16], key_snd[16];
        snprintf(key_h, sizeof(key_h), "alm_%d_h", dayIndex);
        snprintf(key_m, sizeof(key_m), "alm_%d_m", dayIndex);
        snprintf(key_en, sizeof(key_en), "alm_%d_en", dayIndex);
        snprintf(key_rmp, sizeof(key_rmp), "alm_%d_rmp", dayIndex);
        snprintf(key_snd, sizeof(key_snd), "alm_%d_snd", dayIndex);

        int32_t h = 7, m = 0;
        uint8_t en = 0;
        uint8_t rmp = 0;
        char snd[64] = {0};
        size_t len = sizeof(snd);

        nvs_get_i32(my_handle, key_h, &h);
        nvs_get_i32(my_handle, key_m, &m);
        nvs_get_u8(my_handle, key_en, &en);
        nvs_get_u8(my_handle, key_rmp, &rmp);
        if (nvs_get_str(my_handle, key_snd, snd, &len) == ESP_OK) {
            alarm.ringtone = std::string(snd);
        }
        
        alarm.hour = (int)h;
        alarm.minute = (int)m;
        alarm.active = (en == 1);
        alarm.volumeRamp = (rmp == 1);

        nvs_close(my_handle);
    }
    return alarm;
}

struct tm TimeManager::getCurrentTime() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    return timeinfo;
}

bool TimeManager::checkAlarm() {
    if (_alarm_ringing) return false; // Already ringing

    struct tm now = getCurrentTime();
    if (now.tm_year < 120) return false; // Time not set yet (before 2020)

    // Check against today's alarm
    int today = now.tm_wday; // 0=Sun
    DayAlarm alm = getAlarm(today);

    if (!alm.active) return false;

    // Check matching time
    if (now.tm_hour == alm.hour && now.tm_min == alm.minute) {
        // Debounce: ensure we trigger only once per specific day/minute instance
        // Best way: Use tm_yday + hour + minute combined, or just ensure sec is small
        // A simple check is: don't retrigger same day if we stopped it.
        // But if we use snoozing later, this needs logic. 
        // For now: Trigger if seconds < 5 (poll interval usually shorter) AND last_triggered_day != today
        // Actually, better: 
        if (_last_triggered_day != now.tm_yday) {
             ESP_LOGI(TAG, "ALARM TRIGGERED for Day %d at %02d:%02d", today, now.tm_hour, now.tm_min);
             _alarm_ringing = true;
             _last_triggered_day = now.tm_yday;
             return true;
        }
    } else {
        // Reset trigger flag if we are past the alarm time? 
        // Not strictly necessary if using yday, but useful for testing same day multiple times if needed.
        // For production, yday is robust.
    }
    return false;
}

bool TimeManager::isAlarmRinging() { 
    return _alarm_ringing; 
}

void TimeManager::stopAlarm() { 
    ESP_LOGI(TAG, "Stopping Alarm");
    _alarm_ringing = false; 
}
