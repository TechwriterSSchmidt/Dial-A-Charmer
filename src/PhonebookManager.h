#ifndef PHONEBOOKMANAGER_H
#define PHONEBOOKMANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <map>

struct PhonebookEntry {
    String name;        // Anzeigename (z.B. "Persona 01")
    String type;        // "TTS", "AUDIO", "FUNCTION"
    String value;       // Text, Dateipfad oder Funktions-ID
    String parameter;   // Z.B. Kategorie-ID für FUNCTION
};

class PhonebookManager {
public:
    PhonebookManager();
    void begin();
    
    // Core Functionality
    bool hasEntry(String number);
    PhonebookEntry getEntry(String number);
    
    // Management
    void addEntry(String number, String name, String type, String value, String parameter = "");
    void removeEntry(String number);
    String getJson(); 
    void saveFromJson(String jsonString);
    void saveAll(JsonObject json); // New method
    void saveChanges(); // Expose save() functionality publicly
    
    // Search
    String findKeyByValueAndParam(String value, String parameter);

private:
    const char* _filename = "/phonebook.json";
    std::map<String, PhonebookEntry> _entries;
    
    void load();
    void save(); // Internal implementation
};

extern PhonebookManager phonebook;

#endif
