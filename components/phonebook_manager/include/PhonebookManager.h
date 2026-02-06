#pragma once

#include <string>
#include <map>
#include "cJSON.h"

struct PhonebookEntry {
    std::string name;        // Display Name
    std::string type;        // "TTS", "AUDIO", "FUNCTION"
    std::string value;       // Text, Path, or ID
    std::string parameter;   // Optional param
};

class PhonebookManager {
public:
    PhonebookManager();
    void begin();
    
    // Core Functionality
    bool hasEntry(std::string number);
    PhonebookEntry getEntry(std::string number);
    
    // Management
    void addEntry(std::string number, std::string name, std::string type, std::string value, std::string parameter = "");
    void removeEntry(std::string number);
    std::string getJson(); 
    void saveFromJson(std::string jsonString);
    void saveChanges(); 

    // Search
    std::string findKeyByValueAndParam(std::string value, std::string parameter);

private:
    const char* _filename = "/sdcard/phonebook.json"; // Moved to SD Card for IDF (for now)
    std::map<std::string, PhonebookEntry> _entries;
    
    void load();
    void save();
};

extern PhonebookManager phonebook;
