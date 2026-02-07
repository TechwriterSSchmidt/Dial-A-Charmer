#include "TimeManager.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "app_config.h"

static const char *TAG = "TIME_MANAGER";
static bool _alarm_ringing = false;
static int _last_triggered_day = -1; // Ensure trigger only once per day
static TaskHandle_t s_sntp_task = NULL;
static bool s_sntp_synced = false;
static time_t s_last_ntp_sync = 0;

// RTC DS3231 Implementation
#define I2C_PORT_NUM I2C_NUM_1
#define DS3231_ADDR 0x68

static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t rtc_dev_handle = NULL;

static uint8_t bcd2dec(uint8_t val) { return ((val / 16 * 10) + (val % 16)); }
static uint8_t dec2bcd(uint8_t val) { return ((val / 10 * 16) + (val % 10)); }

static bool rtc_read_time_raw(struct tm *out_tm);
static bool rtc_read_status(uint8_t *status);
static bool rtc_clear_osf();

static time_t timegm_utc(struct tm *timeinfo) {
    if (!timeinfo) return (time_t)-1;

    char tz_backup[64] = {0};
    const char *old_tz = getenv("TZ");
    if (old_tz) {
        strncpy(tz_backup, old_tz, sizeof(tz_backup) - 1);
    }

    setenv("TZ", "UTC0", 1);
    tzset();
    time_t ts = mktime(timeinfo);

    if (old_tz) {
        setenv("TZ", tz_backup, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();

    return ts;
}

static void sntp_monitor_task(void *pvParameters) {
    int backoff_sec = 5;
    const int max_backoff_sec = 300;
    sntp_sync_status_t last_status = SNTP_SYNC_STATUS_RESET;

    while (true) {
        sntp_sync_status_t status = esp_sntp_get_sync_status();
        if (status != last_status) {
            if (status == SNTP_SYNC_STATUS_COMPLETED) {
                ESP_LOGI(TAG, "SNTP sync status: completed");
            } else if (status == SNTP_SYNC_STATUS_IN_PROGRESS) {
                ESP_LOGW(TAG, "SNTP sync status: in progress");
            } else {
                ESP_LOGW(TAG, "SNTP sync status: reset");
            }
            last_status = status;
        }

        if (status == SNTP_SYNC_STATUS_COMPLETED || s_sntp_synced) {
            s_sntp_synced = true;
            vTaskDelay(pdMS_TO_TICKS(600000));
            continue;
        }

        ESP_LOGW(TAG, "SNTP not synced, retry in %d seconds", backoff_sec);
        vTaskDelay(pdMS_TO_TICKS(backoff_sec * 1000));

        if (!s_sntp_synced) {
            esp_sntp_stop();
            esp_sntp_init();
        }

        if (backoff_sec < max_backoff_sec) {
            backoff_sec *= 2;
            if (backoff_sec > max_backoff_sec) backoff_sec = max_backoff_sec;
        }
    }
}

static void i2c_init_rtc() {
#if defined(APP_PIN_RTC_SDA) && defined(APP_PIN_RTC_SCL)
    i2c_master_bus_config_t i2c_mst_config = {};
    i2c_mst_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_mst_config.i2c_port = I2C_PORT_NUM;
    i2c_mst_config.scl_io_num = (gpio_num_t)APP_PIN_RTC_SCL;
    i2c_mst_config.sda_io_num = (gpio_num_t)APP_PIN_RTC_SDA;
    i2c_mst_config.glitch_ignore_cnt = 7;
    i2c_mst_config.flags.enable_internal_pullup = true; // Use internal pullup just in case

    ESP_LOGI(TAG, "Initializing I2C Master (New Driver) on SDA:%d SCL:%d", APP_PIN_RTC_SDA, APP_PIN_RTC_SCL);
    
    esp_err_t err = i2c_new_master_bus(&i2c_mst_config, &bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C New Master Bus Failed: %s", esp_err_to_name(err));
        return;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = DS3231_ADDR;
    dev_cfg.scl_speed_hz = 100000;

    err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &rtc_dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C Add Device Failed: %s", esp_err_to_name(err));
    }
#else
    ESP_LOGE(TAG, "RTC Pins not defined!");
#endif
}

static void rtc_write_time(struct tm *timeinfo) {
    if (!timeinfo || !rtc_dev_handle) return;
    
    uint8_t data[8];
    data[0] = 0x00; // Start Register
    data[1] = dec2bcd(timeinfo->tm_sec);
    data[2] = dec2bcd(timeinfo->tm_min);
    data[3] = dec2bcd(timeinfo->tm_hour);
    data[4] = dec2bcd(timeinfo->tm_wday + 1); // DS3231 1-7
    data[5] = dec2bcd(timeinfo->tm_mday);
    data[6] = dec2bcd(timeinfo->tm_mon + 1);
    data[7] = dec2bcd(timeinfo->tm_year - 100);

    esp_err_t ret = i2c_master_transmit(rtc_dev_handle, data, 8, -1);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "DS3231 Updated Successfully");
    } else {
        ESP_LOGE(TAG, "DS3231 Write Failed: %s", esp_err_to_name(ret));
    }
}

static void rtc_read_time() {
    struct tm t;
    if (!rtc_read_time_raw(&t)) {
        ESP_LOGE(TAG, "DS3231 Read Failed (or no RTC present)");
        return;
    }

    uint8_t status = 0;
    if (rtc_read_status(&status) && (status & 0x80)) {
        ESP_LOGW(TAG, "DS3231 OSF flag set, ignoring RTC time until sync");
        return;
    }

    // Set System Time
    time_t timestamp = timegm_utc(&t);
    struct timeval tv = { .tv_sec = timestamp, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    ESP_LOGI(TAG, "System Time set from DS3231: %s", asctime(&t));
}

static bool rtc_read_time_raw(struct tm *out_tm) {
    if (!rtc_dev_handle || !out_tm) return false;

    uint8_t reg = 0x00;
    uint8_t data[7];

    esp_err_t ret = i2c_master_transmit_receive(rtc_dev_handle, &reg, 1, data, 7, -1);
    if (ret != ESP_OK) return false;

    memset(out_tm, 0, sizeof(struct tm));
    out_tm->tm_sec  = bcd2dec(data[0]);
    out_tm->tm_min  = bcd2dec(data[1]);
    out_tm->tm_hour = bcd2dec(data[2]);
    out_tm->tm_wday = bcd2dec(data[3]) - 1;
    out_tm->tm_mday = bcd2dec(data[4]);
    out_tm->tm_mon  = bcd2dec(data[5] & 0x7F) - 1;
    out_tm->tm_year = bcd2dec(data[6]) + 100;
    out_tm->tm_isdst = -1;

    return true;
}

static bool rtc_read_status(uint8_t *status) {
    if (!rtc_dev_handle || !status) return false;

    uint8_t reg = 0x0F; // Status register
    uint8_t data = 0;
    esp_err_t ret = i2c_master_transmit_receive(rtc_dev_handle, &reg, 1, &data, 1, -1);
    if (ret != ESP_OK) return false;
    *status = data;
    return true;
}

static bool rtc_clear_osf() {
    uint8_t status = 0;
    if (!rtc_read_status(&status)) return false;
    status &= (uint8_t)~0x80;

    uint8_t data[2] = {0x0F, status};
    esp_err_t ret = i2c_master_transmit(rtc_dev_handle, data, sizeof(data), -1);
    return ret == ESP_OK;
}

// Callback for SNTP
void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "SNTP Synchronized. Updating RTC...");
    time_t now;
    struct tm timeinfo;
    time(&now);
    gmtime_r(&now, &timeinfo);
    rtc_write_time(&timeinfo);
    rtc_clear_osf();
    s_sntp_synced = true;
    s_last_ntp_sync = now;
}

void TimeManager::init() {
    ESP_LOGI(TAG, "Initializing TimeManager (RTC + SNTP)...");

    // 1. Init I2C & Try to load time from RTC
    i2c_init_rtc();
    rtc_read_time(); 

    // 2. Start SNTP
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    if (!s_sntp_task) {
        xTaskCreate(sntp_monitor_task, "sntp_monitor", 3072, NULL, 4, &s_sntp_task);
    }
    
    // Set Timezone
    std::string tz = getTimezone();
    if (tz.empty()) tz = "CET-1CEST,M3.5.0,M10.5.0/3"; // Default Berlin
    setenv("TZ", tz.c_str(), 1);
    tzset();
}

void TimeManager::setTimezone(const char* tz) {
    if (!tz) return;
    
    nvs_handle_t my_handle;
    if (nvs_open("dialcharm", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_str(my_handle, "time_zone", tz);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        
        setenv("TZ", tz, 1);
        tzset();
        ESP_LOGI(TAG, "Timezone set to: %s", tz);
        
        // Update RTC time just in case (optional, but recalculates local time)
        // struct tm now = getCurrentTime();
        // rtc_write_time(&now); 
    }
}

time_t TimeManager::getLastNtpSync() {
    return s_last_ntp_sync;
}

std::string TimeManager::getTimezone() {
    std::string tz = "";
    nvs_handle_t my_handle;
    if (nvs_open("dialcharm", NVS_READONLY, &my_handle) == ESP_OK) {
        char val[64];
        size_t len = sizeof(val);
        if (nvs_get_str(my_handle, "time_zone", val, &len) == ESP_OK) {
            tz = std::string(val);
        }
        nvs_close(my_handle);
    }
    return tz;
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
            nvs_set_str(my_handle, key_snd, APP_DEFAULT_TIMER_RINGTONE);
        }
        
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Saved Alarm Day %d: %02d:%02d (Act:%d Rmp:%d) Tone: %s", dayIndex, hour, minute, active, volumeRamp, ringtone);
    }
}

DayAlarm TimeManager::getAlarm(int dayIndex) {
    DayAlarm alarm = {7, 0, false, false, APP_DEFAULT_TIMER_RINGTONE}; // Default
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

struct tm TimeManager::getCurrentTimeRtc() {
    struct tm rtc_tm;
    if (rtc_read_time_raw(&rtc_tm)) {
        time_t ts = timegm_utc(&rtc_tm);
        struct tm local_tm;
        localtime_r(&ts, &local_tm);
        return local_tm;
    }
    return getCurrentTime();
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
