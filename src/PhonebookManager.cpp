#include "PhonebookManager.h"

PhonebookManager phonebook;

PhonebookManager::PhonebookManager() {}

void PhonebookManager::begin() {
    if (!SPIFFS.exists(_filename)) {
        Serial.println("Phonebook not found, creating default...");
        
        // --- Core Functions ---
        addEntry("0", "Time Announce", "FUNCTION", "ANNOUNCE_TIME");
        
        // --- Personas / Characters (1-6) ---
        addEntry("1", "Persona 01 (Donald)", "FUNCTION", "COMPLIMENT_CAT", "1");
        addEntry("2", "Persona 02 (Jaqueline)", "FUNCTION", "COMPLIMENT_CAT", "2");
        addEntry("3", "Persona 03 (Yoda)", "FUNCTION", "COMPLIMENT_CAT", "3");
        addEntry("4", "Persona 04 (Neutral)", "FUNCTION", "COMPLIMENT_CAT", "4");
        addEntry("5", "Nerd Joke", "TTS", "Erzähle mir einen kurzen Programmierer-Witz.");
        addEntry("50", "Life Coach", "TTS", "Sag mir etwas inspirierendes wie ein Guru.");
        
        // --- Admin / System (8-9) ---
        addEntry("8", "System Status", "FUNCTION", "SYSTEM_STATUS");
        
        // Admin Menu: Voice Menu
        String menuText = "Willkommen im System-Menü. ";
        menuText += "Wähle Neun-Null, um alle Alarme umzuschalten. ";
        menuText += "Wähle Neun-Eins, um den nächsten Alarm zu überspringen. ";
        menuText += "Wähle Acht, für den Systemstatus.";
        addEntry("9", "Admin Menu", "TTS", menuText);
        
        // --- Control Commands ---
        addEntry("90", "Toggle Alarms", "FUNCTION", "TOGGLE_ALARMS");
        addEntry("91", "Skip Next Alarm", "FUNCTION", "SKIP_NEXT_ALARM");

    } else {
        load();
    }
}

void PhonebookManager::load() {
    File file = SPIFFS.open(_filename, "r");
    if (!file) {
        Serial.println("Failed to open phonebook file");
        return;
    }

    JsonDocument doc; // ArduinoJson v7 verwaltet Speicher dynamisch
    DeserializationError error = deserializeJson(doc, file);

    if (error) {
        Serial.print("Phonebook JSON deserialize failed: ");
        Serial.println(error.c_str());
        return;
    }

    _entries.clear();
    JsonObject root = doc.as<JsonObject>();
    for (JsonPair kv : root) {
        String number = kv.key().c_str();
        JsonObject val = kv.value().as<JsonObject>();
        
        PhonebookEntry entry;
        entry.name = val["name"] | "Unknown";
        entry.type = val["type"] | "TTS";
        entry.value = val["value"] | "";
        entry.parameter = val["parameter"] | "";
        
        _entries[number] = entry;
    }
    file.close();
    Serial.printf("Phonebook loaded. %d entries.\n", _entries.size());
}

void PhonebookManager::saveAll(JsonObject json) {
    _entries.clear();
    for (JsonPair kv : json) {
        String number = kv.key().c_str();
        JsonObject val = kv.value().as<JsonObject>();
        // Add safety check
        if (val.isNull()) continue;
        
        PhonebookEntry entry;
        entry.name = val["name"] | "Unknown";
        entry.type = val["type"] | "TTS";
        entry.value = val["value"] | "";
        entry.parameter = val["parameter"] | "";
        _entries[number] = entry;
    }
    save();
}

void PhonebookManager::save() {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();

    for (auto const& [number, entry] : _entries) {
        // Fix deprecated createNestedObject
        JsonObject obj = root[number].to<JsonObject>();
        obj["name"] = entry.name;
        obj["type"] = entry.type;
        obj["value"] = entry.value;
        obj["parameter"] = entry.parameter;
    }

    File file = SPIFFS.open(_filename, "w");
    if (!file) {
        Serial.println("Failed to open phonebook file for writing");
        return;
    }

    serializeJson(doc, file);
    file.close();
    Serial.println("Phonebook saved.");
}

bool PhonebookManager::hasEntry(String number) {
    return _entries.find(number) != _entries.end();
}

PhonebookEntry PhonebookManager::getEntry(String number) {
    if (hasEntry(number)) {
        return _entries[number];
    }
    return {"", "", "", ""};
}

void PhonebookManager::addEntry(String number, String name, String type, String value, String parameter) {
    PhonebookEntry entry;
    entry.name = name;
    entry.type = type;
    entry.value = value;
    entry.parameter = parameter;
    
    _entries[number] = entry;
    save();
}

void PhonebookManager::removeEntry(String number) {
    _entries.erase(number);
    save();
}

String PhonebookManager::getJson() {
    File file = SPIFFS.open(_filename, "r");
    if (!file) return "{}";
    String json = file.readString();
    file.close();
    return json;
}

void PhonebookManager::saveFromJson(String jsonString) {
    File file = SPIFFS.open(_filename, "w");
    if (file) {
        file.print(jsonString);
        file.close();
        load(); 
    }
}
