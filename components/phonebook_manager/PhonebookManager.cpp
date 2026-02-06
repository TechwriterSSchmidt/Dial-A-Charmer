#include "PhonebookManager.h"
#include "app_config.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "APP_PHONEBOOK";

PhonebookManager phonebook;

PhonebookManager::PhonebookManager() {}

void PhonebookManager::begin() {
    // Default Entries Logic (in-memory only)
    auto ensure = [&](std::string num, std::string name, std::string type, std::string val, std::string param="") {
        if (!hasEntry(num)) {
            ESP_LOGI(TAG, "Adding default: %s", name.c_str());
            _entries[num] = {name, type, val, param};
        }
    };

    ensure(APP_PB_NUM_PERSONA_1, "Persona 1 (Default)", "FUNCTION", "COMPLIMENT_CAT", "1");
    ensure(APP_PB_NUM_PERSONA_2, "Persona 2 (Joke)", "FUNCTION", "COMPLIMENT_CAT", "2");
    ensure(APP_PB_NUM_PERSONA_3, "Persona 3 (SciFi)", "FUNCTION", "COMPLIMENT_CAT", "3");
    ensure(APP_PB_NUM_PERSONA_4, "Persona 4 (Captain)", "FUNCTION", "COMPLIMENT_CAT", "4");
    ensure(APP_PB_NUM_PERSONA_5, "Persona 5", "FUNCTION", "COMPLIMENT_CAT", "5");
    ensure(APP_PB_NUM_RANDOM_MIX, "Random Mix (Surprise)", "FUNCTION", "COMPLIMENT_MIX", "0");
    ensure(APP_PB_NUM_TIME, "Zeitauskunft", "FUNCTION", "ANNOUNCE_TIME");
    ensure(APP_PB_NUM_GEMINI, "Gemini AI", "FUNCTION", "GEMINI_CHAT");
    
    // Admin
    ensure(APP_PB_NUM_VOICE_MENU, "Voice Admin Menu", "FUNCTION", "VOICE_MENU");
    ensure(APP_PB_NUM_TOGGLE_ALARMS, "Toggle Alarms", "FUNCTION", "TOGGLE_ALARMS");
    ensure(APP_PB_NUM_SKIP_ALARM, "Skip Next Alarm", "FUNCTION", "SKIP_NEXT_ALARM");
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
    // Re-use logic from save() but return string
    // Implementation omitted for brevity, usually for WebUI
    return "{}"; 
}

void PhonebookManager::saveFromJson(std::string jsonString) {
    // Implementation omitted
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
