#include "PhonebookManager.h"
#include "esp_log.h"
#include <stdio.h>
#include <sys/unistd.h>
#include <sys/stat.h>

static const char *TAG = "APP_PHONEBOOK";

PhonebookManager phonebook;

PhonebookManager::PhonebookManager() {}

void PhonebookManager::begin() {
    struct stat st;
    if (stat(_filename, &st) == 0) {
        ESP_LOGI(TAG, "Loading Phonebook from %s...", _filename);
        load();
    } else {
        ESP_LOGI(TAG, "Phonebook not found at %s, initializing empty...", _filename);
    }

    // Default Entries Logic
    bool changed = false;
    auto ensure = [&](std::string num, std::string name, std::string type, std::string val, std::string param="") {
        if (!hasEntry(num)) {
            ESP_LOGI(TAG, "Adding default: %s", name.c_str());
            _entries[num] = {name, type, val, param};
            changed = true;
        }
    };

    ensure("110", "Zeitauskunft", "FUNCTION", "ANNOUNCE_TIME");
    ensure("0", "Gemini AI", "FUNCTION", "GEMINI_CHAT"); 
    
    // Personas
    ensure("1", "Persona 1 (Default)", "FUNCTION", "COMPLIMENT_CAT", "1");
    ensure("2", "Persona 2 (Joke)", "FUNCTION", "COMPLIMENT_CAT", "2");
    ensure("3", "Persona 3 (SciFi)", "FUNCTION", "COMPLIMENT_CAT", "3");
    ensure("4", "Persona 4 (Captain)", "FUNCTION", "COMPLIMENT_CAT", "4");
    ensure("5", "Persona 5", "FUNCTION", "COMPLIMENT_CAT", "5");
    ensure("6", "Random Mix (Surprise)", "FUNCTION", "COMPLIMENT_MIX", "0");
    
    // Admin
    ensure("9", "Voice Admin Menu", "FUNCTION", "VOICE_MENU");
    ensure("90", "Toggle Alarms", "FUNCTION", "TOGGLE_ALARMS");
    ensure("91", "Skip Next Alarm", "FUNCTION", "SKIP_NEXT_ALARM");
    ensure("095", "System Reboot", "FUNCTION", "REBOOT");

    if (changed) {
        save();
        ESP_LOGI(TAG, "Phonebook updated with defaults.");
    }
}

void PhonebookManager::load() {
    FILE *f = fopen(_filename, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open phonebook file");
        return;
    }

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (length <= 0) {
        fclose(f);
        return;
    }

    char *buffer = (char *)malloc(length + 1);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for phonebook");
        fclose(f);
        return;
    }
    
    fread(buffer, 1, length, f);
    buffer[length] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buffer);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        free(buffer);
        return;
    }

    _entries.clear();
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (item->string) {
            std::string number = item->string;
            PhonebookEntry entry;
            
            cJSON *j_name = cJSON_GetObjectItem(item, "name");
            cJSON *j_type = cJSON_GetObjectItem(item, "type");
            cJSON *j_value = cJSON_GetObjectItem(item, "value");
            cJSON *j_param = cJSON_GetObjectItem(item, "parameter");

            entry.name = j_name ? j_name->valuestring : "Unknown";
            entry.type = j_type ? j_type->valuestring : "TTS";
            entry.value = j_value ? j_value->valuestring : "";
            entry.parameter = j_param ? j_param->valuestring : "";

            _entries[number] = entry;
        }
    }
    
    cJSON_Delete(root);
    free(buffer);
    ESP_LOGI(TAG, "Phonebook loaded. %d entries.", _entries.size());
}

void PhonebookManager::save() {
    cJSON *root = cJSON_CreateObject();
    
    for (auto const& [number, entry] : _entries) {
        cJSON *entryObj = cJSON_CreateObject();
        cJSON_AddStringToObject(entryObj, "name", entry.name.c_str());
        cJSON_AddStringToObject(entryObj, "type", entry.type.c_str());
        cJSON_AddStringToObject(entryObj, "value", entry.value.c_str());
        cJSON_AddStringToObject(entryObj, "parameter", entry.parameter.c_str());
        
        cJSON_AddItemToObject(root, number.c_str(), entryObj);
    }
    
    char *string = cJSON_Print(root);
    if (string == NULL) {
        cJSON_Delete(root);
        return;
    }

    FILE *f = fopen(_filename, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open phonebook file for writing");
    } else {
        fprintf(f, "%s", string);
        fclose(f);
    }
    
    cJSON_free(string);
    cJSON_Delete(root);
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
