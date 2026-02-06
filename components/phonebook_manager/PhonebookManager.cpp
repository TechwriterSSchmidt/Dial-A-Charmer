#include "PhonebookManager.h"
#include "app_config.h"
#include "esp_log.h"
#include "cJSON.h"
#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

static const char *TAG = "APP_PHONEBOOK";

PhonebookManager phonebook;

PhonebookManager::PhonebookManager() {}

static bool is_wav_name(const char *name) {
    if (!name) return false;
    size_t len = strlen(name);
    if (len < 4) return false;
    return strcasecmp(name + len - 4, ".wav") == 0;
}

static bool read_first_wav(const std::string &dir_path, std::string &out_name) {
    DIR *dir = opendir(dir_path.c_str());
    if (!dir) return false;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (is_wav_name(ent->d_name)) {
            out_name = ent->d_name;
            closedir(dir);
            return true;
        }
    }
    closedir(dir);
    return false;
}

static std::string extract_category_title(const std::string &filename) {
    std::string stem = filename;
    size_t dot = stem.rfind('.');
    if (dot != std::string::npos) {
        stem = stem.substr(0, dot);
    }

    size_t last = stem.rfind('_');
    if (last == std::string::npos || last == 0) return stem;
    size_t prev = stem.rfind('_', last - 1);
    if (prev == std::string::npos) return stem;

    std::string title = stem.substr(0, prev);
    for (char &c : title) {
        if (c == '_') c = ' ';
    }
    return title;
}

static std::string get_persona_title(int idx) {
    char path[64];
    std::string wav;

    snprintf(path, sizeof(path), "/sdcard/persona_%02d/de", idx);
    if (!read_first_wav(path, wav)) {
        snprintf(path, sizeof(path), "/sdcard/persona_%02d/en", idx);
        if (!read_first_wav(path, wav)) {
            snprintf(path, sizeof(path), "/sdcard/persona_%02d", idx);
            if (!read_first_wav(path, wav)) {
                return "";
            }
        }
    }

    return extract_category_title(wav);
}

void PhonebookManager::begin() {
    // Default Entries Logic (in-memory only)
    auto ensure = [&](std::string num, std::string name, std::string type, std::string val, std::string param="") {
        if (!hasEntry(num)) {
            ESP_LOGI(TAG, "Adding default: %s", name.c_str());
            _entries[num] = {name, type, val, param};
        }
    };

    std::string p1 = get_persona_title(1);
    std::string p2 = get_persona_title(2);
    std::string p3 = get_persona_title(3);
    std::string p4 = get_persona_title(4);
    std::string p5 = get_persona_title(5);

    ensure(APP_PB_NUM_PERSONA_1, p1.empty() ? "Persona 1" : p1, "FUNCTION", "COMPLIMENT_CAT", "1");
    ensure(APP_PB_NUM_PERSONA_2, p2.empty() ? "Persona 2" : p2, "FUNCTION", "COMPLIMENT_CAT", "2");
    ensure(APP_PB_NUM_PERSONA_3, p3.empty() ? "Persona 3" : p3, "FUNCTION", "COMPLIMENT_CAT", "3");
    ensure(APP_PB_NUM_PERSONA_4, p4.empty() ? "Persona 4" : p4, "FUNCTION", "COMPLIMENT_CAT", "4");
    ensure(APP_PB_NUM_PERSONA_5, p5.empty() ? "Persona 5" : p5, "FUNCTION", "COMPLIMENT_CAT", "5");
    ensure(APP_PB_NUM_RANDOM_MIX, "Random Mix (Surprise)", "FUNCTION", "COMPLIMENT_MIX", "0");
    ensure(APP_PB_NUM_TIME, "Zeitauskunft", "FUNCTION", "ANNOUNCE_TIME");
    
    // Admin
    ensure(APP_PB_NUM_VOICE_MENU, "Voice Admin Menu", "FUNCTION", "VOICE_MENU");
    ensure(APP_PB_NUM_REBOOT, "System Reboot", "FUNCTION", "REBOOT");
}

void PhonebookManager::load() {
    // Phonebook persistence disabled; defaults only.
    _entries.clear();
}

void PhonebookManager::save() {
    // Phonebook persistence disabled; keep in memory only.
}

bool PhonebookManager::hasEntry(std::string number) {
    return _entries.find(number) != _entries.end();
}

PhonebookEntry PhonebookManager::getEntry(std::string number) {
    if (hasEntry(number)) {
        return _entries[number];
    }
    return {"Unknown", "NONE", "", ""};
}

void PhonebookManager::addEntry(std::string number, std::string name, std::string type, std::string value, std::string parameter) {
    _entries[number] = {name, type, value, parameter};
    save();
}

void PhonebookManager::removeEntry(std::string number) {
    if (hasEntry(number)) {
        _entries.erase(number);
        save();
    }
}

std::string PhonebookManager::getJson() {
    cJSON *root = cJSON_CreateObject();
    for (const auto &kv : _entries) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "name", kv.second.name.c_str());
        cJSON_AddStringToObject(entry, "type", kv.second.type.c_str());
        cJSON_AddStringToObject(entry, "value", kv.second.value.c_str());
        cJSON_AddStringToObject(entry, "parameter", kv.second.parameter.c_str());
        cJSON_AddItemToObject(root, kv.first.c_str(), entry);
    }

    const char *json_str = cJSON_PrintUnformatted(root);
    std::string out = json_str ? json_str : "{}";
    free((void *)json_str);
    cJSON_Delete(root);
    return out;
}

void PhonebookManager::saveFromJson(std::string jsonString) {
    cJSON *root = cJSON_Parse(jsonString.c_str());
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return;
    }

    _entries.clear();

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (!item->string || !cJSON_IsObject(item)) {
            continue;
        }

        cJSON *name = cJSON_GetObjectItem(item, "name");
        cJSON *type = cJSON_GetObjectItem(item, "type");
        cJSON *value = cJSON_GetObjectItem(item, "value");
        cJSON *param = cJSON_GetObjectItem(item, "parameter");

        if (!cJSON_IsString(name) || !cJSON_IsString(type) || !cJSON_IsString(value)) {
            continue;
        }

        std::string parameter = cJSON_IsString(param) ? param->valuestring : "";
        _entries[item->string] = {name->valuestring, type->valuestring, value->valuestring, parameter};
    }

    cJSON_Delete(root);
}

void PhonebookManager::saveChanges() {
    save();
}

std::string PhonebookManager::findKeyByValueAndParam(std::string value, std::string parameter) {
    for (auto const& [key, val] : _entries) {
        if (val.value == value && val.parameter == parameter) {
            return key;
        }
    }
    return "";
}
