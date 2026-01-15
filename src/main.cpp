#include <Arduino.h>
#include <TinyGPS++.h>
#include <Audio.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <vector>
#include <algorithm>
#include <random>
#include <map>

#include "config.h"
#include "Settings.h"
#include "WebManager.h"
#include "RotaryDial.h"
#include "LedStatus.h"
#include "Es8311Driver.h" // New Audio Codec Driver
#include "AiManager.h"    // New AI Manager

// --- Objects ---
TinyGPSPlus gps;
Audio audio;
RotaryDial dial(CONF_PIN_DIAL_PULSE, CONF_PIN_HOOK, CONF_PIN_EXTRA_BTN);

// --- Globals ---
unsigned long alarmEndTime = 0;
bool alarmActive = false;
bool timerRunning = false;
bool timerSoundPlaying = false;

// --- Playlist Management ---
struct Playlist {
    std::vector<String> tracks;
    int index = 0;
};

std::map<int, Playlist> categories; // 1=Trump, 2=Badran, 3=Yoda, 4=Neutral
Playlist mainPlaylist; // For Dial 0

void scanDirectoryToPlaylist(String path, int categoryId) {
    File dir = SD.open(path);
    if(!dir || !dir.isDirectory()) {
         Serial.print("Dir missing: "); Serial.println(path);
         return;
    }
    
    File file = dir.openNextFile();
    while(file) {
        if(!file.isDirectory()) {
            String fname = String(file.name());
            if(fname.endsWith(".mp3") && fname.indexOf("._") == -1) {
                // Construct full path. Depending on SD lib version, file.name() might be full path or distinct.
                // We assume we need to prepend path if it's just the name.
                String fullPath = path;
                if(!fullPath.endsWith("/")) fullPath += "/";
                if(fname.startsWith("/")) fname = fname.substring(1); // avoid double slash
                fullPath += fname;
                
                // Add to specific category
                categories[categoryId].tracks.push_back(fullPath);
                
                // Add to main playlist
                mainPlaylist.tracks.push_back(fullPath);
            }
        }
        file = dir.openNextFile();
    }
}

void buildPlaylist() {
    categories.clear();
    mainPlaylist.tracks.clear();
    mainPlaylist.index = 0;
    
    Serial.println("Building Playlists...");
    
    // 1: Trump
    scanDirectoryToPlaylist("/compliments/trump", 1);
    // 2: Badran
    scanDirectoryToPlaylist("/compliments/badran", 2);
    // 3: Yoda
    scanDirectoryToPlaylist("/compliments/yoda", 3);
    // 4: Neutral
    scanDirectoryToPlaylist("/compliments/neutral", 4);
    // Scan root compliments for uncategorized/mixtures if needed, or just these subfolders
    // scanDirectoryToPlaylist("/compliments", 5); 

    // Shuffle all
    auto rng = std::default_random_engine(esp_random());
    
    for(auto const& [key, val] : categories) {
         if (!categories[key].tracks.empty()) {
             std::shuffle(categories[key].tracks.begin(), categories[key].tracks.end(), rng);
             categories[key].index = 0;
             Serial.printf("Category %d: %d tracks\n", key, categories[key].tracks.size());
         }
    }
    
    if (!mainPlaylist.tracks.empty()) {
        std::shuffle(mainPlaylist.tracks.begin(), mainPlaylist.tracks.end(), rng);
        Serial.printf("Main Playlist (All): %d tracks\n", mainPlaylist.tracks.size());
    }
}

String getNextTrack(int category) {
    Playlist* target = nullptr;
    
    if (category == 0) {
        target = &mainPlaylist;
    } else {
        if (categories.find(category) != categories.end()) {
            target = &categories[category];
        }
    }
    
    if (target == nullptr || target->tracks.empty()) {
        // Fallback or empty
        if (category != 0 && !mainPlaylist.tracks.empty()) {
             // If specific category is empty, try main
             target = &mainPlaylist; 
        } else {
             return "";
        }
    }
    
    // Check index
    if (target->index >= target->tracks.size()) {
        Serial.printf("Playlist %d finished! Reshuffling...\n", category);
        std::shuffle(target->tracks.begin(), target->tracks.end(), std::default_random_engine(esp_random()));
        target->index = 0;
    }
    
    return target->tracks[target->index++];
}


// --- Helper Functions ---
void playSound(String filename) {
    if (SD.exists(filename)) {
        statusLed.setTalking();
        audio.connecttofs(SD, filename.c_str());
    } else {
        Serial.print("File missing: ");
        Serial.println(filename);
        statusLed.setWarning();
    }
}

void speakTime() {
    Serial.print("Speaking Time...");
    playSound("/time_intro.mp3"); 
    // TODO: proper timestamp logic
}

void speakCompliment(int number) {
    // Check if AI is configured
    String key = settings.getGeminiKey();
    
    if (key.length() > 5) { // Assuming a valid key
        statusLed.setWifiConnecting(); // Yellow = Thinking
        Serial.println("AI Mode Active");
        
        // 1. Get Text from Gemini
        String text = ai.getCompliment(number);
        
        if (text.length() > 0) {
            // 2. Get TTS URL
            String ttsUrl = ai.getTTSUrl(text);
            
            // 3. Play
            Serial.println("Streaming TTS...");
            statusLed.setTalking();
            audio.connecttohost(ttsUrl.c_str());
            return;
        } else {
            Serial.println("AI Failed, falling back to SD");
            statusLed.setWarning();
        }
    }
    
    // Fallback if no Key or AI failed
    // New Logic: Play from randomized playlist based on number
    // 0 = Mix, 1..4 = Specific Categories
    // If number > 4, fallback to Mix (0)
    int category = (number <= 4) ? number : 0;
    
    String path = getNextTrack(category);
    
    if (path.length() == 0) {
         Serial.println("Playlist Empty! Check SD Card.");
         path = "/system/error.mp3"; 
    }
    
    Serial.printf("Playing compliment (Cat: %d): %s\n", category, path.c_str());
    playSound(path);
}

// --- Callbacks ---
void onDial(int number) {
    Serial.printf("Dialed: %d\n", number);
    
    // Check for Ringtone Setting Mode (Button Held)
    if (dial.isButtonDown()) {
        if (number >= 1 && number <= 5) {
            Serial.printf("Setting Ringtone to %d\n", number);
            settings.setRingtone(number);
            statusLed.setWifiConnected(); // Green blink
            playSound("/ringtones/" + String(number) + ".mp3");
            return;
        }
    }

    if (dial.isOffHook()) {
        if (number == 0) {
            speakTime();
        } else {
            speakCompliment(number);
        }
    } else {
        Serial.printf("Setting Timer for %d minutes\n", number);
        alarmEndTime = millis() + (number * 60000);
        timerRunning = true;
        statusLed.setWifiConnecting(); 
        playSound("/timer_set.mp3");
    }
}

void onHook(bool offHook) {
    Serial.printf("Hook State: %s\n", offHook ? "OFF HOOK (Picked Up)" : "ON HOOK (Hung Up)");
    
    if (offHook) {
        statusLed.setIdle();
        if (timerSoundPlaying || alarmActive) {
            if (audio.isRunning()) audio.stopSong();
            timerSoundPlaying = false;
            alarmActive = false;
            timerRunning = false;
            Serial.println("Alarm/Timer Stopped by Pickup");
        } else {
            playSound("/dial_tone.mp3"); 
        }
    } else {
        if (audio.isRunning()) {
            audio.stopSong();
            statusLed.setIdle();
        }
    }
}

void onButton() {
    Serial.println("Extra Button Pressed");
    playSound("/ip_announce_start.mp3");
}

void setup() {
    Serial.begin(CONF_SERIAL_BAUD);
    settings.begin();
    statusLed.begin();
    
    // Init Audio Codec (ES8311)
    if (!audioCodec.begin()) {
        Serial.println("ES8311 Init Failed!");
        statusLed.setWarning();
    } else {
        Serial.println("ES8311 Initialized");
        audioCodec.setVolume(50); // Set Hardware Volume to 50%
        // Adjust software volume in Audio lib if needed
    }
    
    // Init SD 
    if(!SD.begin(CONF_PIN_SD_CS)){
        Serial.println("SD Card Mount Failed");
        statusLed.setWarning();
    } else {
        Serial.println("SD Card Mounted");
        buildPlaylist();
    }
    
    // Init Audio Lib
    audio.setPinout(CONF_I2S_BCLK, CONF_I2S_LRC, CONF_I2S_DOUT);
    
    // Map 0-42 (UI) to 0-21 (Audio Lib)
    int volConf = settings.getVolume();
    int volLib = map(volConf, 0, 42, 0, 21);
    audio.setVolume(volLib); 
    
    dial.onDialComplete(onDial);
    dial.onHookChange(onHook);
    dial.onButtonPress(onButton);
    dial.begin();
    
    webManager.begin();
    
    Serial2.begin(CONF_GPS_BAUD, SERIAL_8N1, CONF_GPS_RX, CONF_GPS_TX);
    
    Serial.println("Atomic Charmer Started");
    playSound("/startup.mp3");
}

void loop() {
    audio.loop();
    webManager.loop();
    dial.loop();
    statusLed.loop();
    
    while (Serial2.available() > 0) {
        gps.encode(Serial2.read());
    }
    
    if (timerRunning && millis() > alarmEndTime) {
        timerRunning = false;
        timerSoundPlaying = true;
        Serial.println("Timer Finished! Ringing...");
        playSound("/ringtones/" + String(settings.getRingtone()) + ".mp3");
    }
}

// Audio Events
void audio_eof_mp3(const char *info){
    Serial.print("EOF: "); Serial.println(info);
    statusLed.setIdle();
    if (timerSoundPlaying) {
        audio.connecttofs(SD, ("/ringtones/" + String(settings.getRingtone()) + ".mp3").c_str());
    }
}

// Called when stream (TTS) finishes
void audio_eof_stream(const char *info){
    Serial.print("EOF Stream: "); Serial.println(info);
    statusLed.setIdle();
}
