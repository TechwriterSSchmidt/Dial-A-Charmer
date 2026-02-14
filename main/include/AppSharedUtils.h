#pragma once

#include <dirent.h>
#include <nvs.h>
#include <cstring>
#include <string>
#include <strings.h>
#include "app_config.h"

struct AppLedSettings {
    uint8_t enabled;
    uint8_t day_pct;
    uint8_t night_pct;
    uint8_t day_start;
    uint8_t night_start;
};

static inline AppLedSettings app_default_led_settings() {
    AppLedSettings settings = {
        .enabled = (APP_LED_DEFAULT_ENABLED ? 1 : 0),
        .day_pct = APP_LED_DAY_PERCENT,
        .night_pct = APP_LED_NIGHT_PERCENT,
        .day_start = APP_LED_DAY_START_HOUR,
        .night_start = APP_LED_NIGHT_START_HOUR,
    };
    return settings;
}

static inline void app_clamp_led_settings(AppLedSettings *settings) {
    if (!settings) {
        return;
    }
    settings->enabled = settings->enabled ? 1 : 0;
    if (settings->day_pct > 100) settings->day_pct = 100;
    if (settings->night_pct > 100) settings->night_pct = 100;
    if (settings->day_start > 23) settings->day_start = 23;
    if (settings->night_start > 23) settings->night_start = 23;
}

static inline bool app_led_settings_equal(const AppLedSettings &a, const AppLedSettings &b) {
    return a.enabled == b.enabled &&
           a.day_pct == b.day_pct &&
           a.night_pct == b.night_pct &&
           a.day_start == b.day_start &&
           a.night_start == b.night_start;
}

static inline void app_load_led_settings_from_handle(nvs_handle_t handle, AppLedSettings *out_settings) {
    if (!out_settings) {
        return;
    }

    AppLedSettings settings = app_default_led_settings();
    nvs_get_u8(handle, "led_enabled", &settings.enabled);
    nvs_get_u8(handle, "led_day_pct", &settings.day_pct);
    nvs_get_u8(handle, "led_night_pct", &settings.night_pct);
    nvs_get_u8(handle, "led_day_start", &settings.day_start);
    nvs_get_u8(handle, "led_night_start", &settings.night_start);
    app_clamp_led_settings(&settings);
    *out_settings = settings;
}

static inline void app_store_led_settings_to_handle(nvs_handle_t handle, const AppLedSettings *settings_in) {
    if (!settings_in) {
        return;
    }

    AppLedSettings settings = *settings_in;
    app_clamp_led_settings(&settings);
    nvs_set_u8(handle, "led_enabled", settings.enabled);
    nvs_set_u8(handle, "led_day_pct", settings.day_pct);
    nvs_set_u8(handle, "led_night_pct", settings.night_pct);
    nvs_set_u8(handle, "led_day_start", settings.day_start);
    nvs_set_u8(handle, "led_night_start", settings.night_start);
}

static inline bool app_is_lang_en() {
    nvs_handle_t my_handle;
    char val[8] = {0};
    size_t len = sizeof(val);
    bool is_en = false;

    if (nvs_open("dialcharm", NVS_READONLY, &my_handle) == ESP_OK) {
        if (nvs_get_str(my_handle, "src_lang", val, &len) == ESP_OK) {
            is_en = (strncmp(val, "en", 2) == 0);
        }
        nvs_close(my_handle);
    }
    return is_en;
}

static inline const char *app_lang_code() {
    return app_is_lang_en() ? "en" : "de";
}

static inline std::string app_get_first_wav_name(const char *dir_path) {
    if (!dir_path) {
        return "";
    }

    DIR *dir = opendir(dir_path);
    if (!dir) {
        return "";
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        if (!name || name[0] == '.') {
            continue;
        }
        const char *dot = strrchr(name, '.');
        if (dot && strcasecmp(dot, ".wav") == 0) {
            std::string result = name;
            closedir(dir);
            return result;
        }
    }

    closedir(dir);
    return "";
}

static inline std::string app_get_first_wav_path(const char *dir_path) {
    std::string name = app_get_first_wav_name(dir_path);
    if (name.empty()) {
        return "";
    }
    return std::string(dir_path) + "/" + name;
}
