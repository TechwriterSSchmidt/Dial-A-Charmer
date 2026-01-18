#include <Arduino.h>
#include <Audio.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <SPIFFS.h>
#include <vector>
#include <algorithm>
#include <random>
#include <map>
#include <driver/i2s.h> // For ADC Mic

#include "config.h"
#include "Settings.h"
#include "WebManager.h"
#include "TimeManager.h" 
#include "RotaryDial.h"
#include "LedManager.h"
#include "AiManager.h"   
#include "PhonebookManager.h"

// --- Objects ---
Audio *audio = nullptr; // Pointer managed object
RotaryDial dial(CONF_PIN_DIAL_PULSE, CONF_PIN_HOOK, CONF_PIN_EXTRA_BTN, CONF_PIN_DIAL_MODE);
LedManager ledManager(CONF_PIN_LED);

// Global State
bool sdAvailable = false;
bool isDialTonePlaying = false; // New Dial Tone State

// Note: PCM5100A is a "dumb" DAC and doesn't report I2C status. We assume it's working.

// --- Background Scan Globals ---
int bgScanPersonaIndex = 1;
File bgScanDir;
bool bgScanActive = false;
bool bgScanPhonebookChanged = false;

// --- Ringing State (UI) ---
bool isAlarmRinging = false;
bool isTimerRinging = false; 
unsigned long ringingStartTime = 0;
int currentAlarmVol = 0;

// Dialing State
String dialBuffer = "";
unsigned long lastDialTime = 0;
#define DIAL_CMD_TIMEOUT 3000

// --- Playlist Management ---
struct Playlist {
    std::vector<String> tracks;
    
    // Virtual Playlist Support (Reference to other playlists)
    struct TrackRef { uint8_t cat; uint16_t idx; };
    std::vector<TrackRef> virtualTracks; 
    bool isVirtual = false;

    int index = 0;
};

std::map<int, Playlist> categories; // 1=Trump, 2=Badran, 3=Yoda, 4=Neutral
Playlist mainPlaylist; // For Dial 0

// --- Persistence Helpers ---
// CHANGED: Use v3 filenames to force cache invalidation (fixes mp3_group vs persona path mismatch)
String getStoredPlaylistPath(int category) {
    return "/playlists/cat_" + String(category) + "_v3.m3u";
}

String getStoredIndexPath(int category) {
    return "/playlists/cat_" + String(category) + "_v3.idx";
}

void ensurePlaylistDir() {
    if (!sdAvailable) return;
    if (!SD.exists("/playlists")) SD.mkdir("/playlists");
}

void savePlaylistToSD(int category, Playlist &pl) {
    if (!sdAvailable) return;
    ensurePlaylistDir();
    String path = getStoredPlaylistPath(category);
    SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) return;
    for (const auto &track : pl.tracks) {
        f.println(track);
    }
    f.close();
}

void saveProgressToSD(int category, int index) {
    if (!sdAvailable) return;
    ensurePlaylistDir();
    String path = getStoredIndexPath(category);
    SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (f) {
        f.println(index);
        f.close();
    }
}

bool loadPlaylistFromSD(int category, Playlist &pl) {
    if (!sdAvailable) return false;
    String path = getStoredPlaylistPath(category);
    if (!SD.exists(path)) return false;
    
    File f = SD.open(path);
    if (!f) return false;
    
    pl.tracks.clear();
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) pl.tracks.push_back(line);
    }
    f.close();
    
    // Load Index
    String idxPath = getStoredIndexPath(category);
    if (SD.exists(idxPath)) {
        File fIdx = SD.open(idxPath);
        if (fIdx) {
            String idxStr = fIdx.readStringUntil('\n'); 
            pl.index = idxStr.toInt();
            // Sanity check
            if (pl.index < 0 || (size_t)pl.index >= pl.tracks.size()) pl.index = 0;
            fIdx.close();
        }
    } else {
        pl.index = 0;
    }
    return true;
}

void scanDirectoryToPlaylist(String path, int categoryId) {
    if (!sdAvailable) return;
    File dir = SD.open(path);
    if(!dir || !dir.isDirectory()) {
         Serial.print("Dir missing: "); Serial.println(path);
         return;
    }
    
    Serial.printf("Scanning dir: %s for Cat %d\n", path.c_str(), categoryId);

    File file = dir.openNextFile();
    while(file) {
        if(!file.isDirectory()) {
            String fname = String(file.name());
            if(fname.endsWith(".mp3") && fname.indexOf("._") == -1) {
                // Construct full path
                String fullPath = path;
                if(!fullPath.endsWith("/")) fullPath += "/";
                if(fname.startsWith("/")) fname = fname.substring(1); 
                fullPath += fname;
                
                // Serial.printf("  Found: %s\n", fullPath.c_str()); // Debug verbose

                // Add to specific category ONLY
                categories[categoryId].tracks.push_back(fullPath);
            }
        }
        file = dir.openNextFile();
    }
}

void startPersonaScan() {
    if (!sdAvailable) return;
    Serial.println("[BG] Starting Background Persona Scan...");
    bgScanPersonaIndex = 1;
    bgScanActive = true;
    bgScanPhonebookChanged = false;
    // ensure bgScanDir is closed if it was left open
    if(bgScanDir) bgScanDir.close();
}

void handlePersonaScan() {
    // TEMPORARY DEBUG ENABLED AGAIN
    // return; 

    if (!bgScanActive || !sdAvailable) return;

    // Process a chunk of work
    // State 0: Open Dir if needed
    if (!bgScanDir) {
            if (bgScanPersonaIndex > 4) {
                // FINISHED
                bgScanActive = false;
                if (bgScanPhonebookChanged) {
                    Serial.println("[BG] Saving updated Phonebook names...");
                    phonebook.saveChanges();
                } else {
                    Serial.println("[BG] Scan finished. No changes.");
                }
                return;
            }
            
            String path = "/persona_0" + String(bgScanPersonaIndex);
            bgScanDir = SD.open(path);
            if (!bgScanDir || !bgScanDir.isDirectory()) {
                // Serial.printf("[BG] Skipping missing/file %s\n", path.c_str());
                if(bgScanDir) bgScanDir.close();
                bgScanPersonaIndex++;
                return; // Next loop will handle next index
            }
            Serial.printf("[BG] Scanning %s...\n", path.c_str());
    }

    // State 1: Read ONE file
    File file = bgScanDir.openNextFile();
    if (file) {
        String fname = String(file.name());
        
        // Clean filename (some FS return full path)
        int lastSlash = fname.lastIndexOf('/');
        if (lastSlash >= 0) fname = fname.substring(lastSlash + 1);

        if (!file.isDirectory() && fname.endsWith(".txt") && !fname.startsWith(".") && fname.indexOf("._") == -1) {
            // Found TXT! Update and Move on
            String name = fname.substring(0, fname.length() - 4);
            Serial.printf("[BG] Found Name: %s\n", name.c_str());
            
            // Note: findKeyByValueAndParam might be slow if phonebook is huge but it's usually small
            String key = phonebook.findKeyByValueAndParam("COMPLIMENT_CAT", String(bgScanPersonaIndex));
            
            if (key != "") {
                PhonebookEntry e = phonebook.getEntry(key);
                if (e.name != name) {
                    phonebook.addEntry(key, name, "FUNCTION", "COMPLIMENT_CAT", String(bgScanPersonaIndex));
                    bgScanPhonebookChanged = true;
                }
            } else {
                phonebook.addEntry(String(bgScanPersonaIndex), name, "FUNCTION", "COMPLIMENT_CAT", String(bgScanPersonaIndex));
                bgScanPhonebookChanged = true;
            }
            
            // Optimization: Found the name, stop scanning this folder
            bgScanDir.close(); 
            bgScanPersonaIndex++;
        }
        // file object is destroyed here, handle is closed? 
        // SD lib usually closes when object is destroyed.
    } else {
        // End of Directory
        bgScanDir.close();
        bgScanPersonaIndex++;
    }
}

void buildPlaylist() {
    // Initial Scan for Names - REMOVED (Moved to Background Task)
    // updatePersonaNamesFromSD();

    categories.clear();
    mainPlaylist.tracks.clear();
    mainPlaylist.index = 0;
    
    Serial.println("Initializing Playlists...");
    
    // Categories 1-4
    for (int i = 1; i <= 4; i++) {
        String subfolder = "persona_0" + String(i);
        
        bool loaded = loadPlaylistFromSD(i, categories[i]);
        if (!loaded || categories[i].tracks.empty()) {
            Serial.printf("Building Cat %d (scanning)...\n", i);
            categories[i].tracks.clear();
            categories[i].index = 0;
            // Removed /compliments prefix to match SD card structure where groups are at root
            scanDirectoryToPlaylist("/" + subfolder, i);
            
            // Shuffle
            auto rng = std::default_random_engine(esp_random());
            std::shuffle(categories[i].tracks.begin(), categories[i].tracks.end(), rng);
            
            savePlaylistToSD(i, categories[i]);
            saveProgressToSD(i, 0);
        } else {
            Serial.printf("Loaded Cat %d from SD (Tracks: %d, Idx: %d)\n", i, categories[i].tracks.size(), categories[i].index);
        }
    }
    
    // Main Playlist (0) - Virtual to save RAM
    mainPlaylist.isVirtual = true;
    mainPlaylist.tracks.clear(); 
    mainPlaylist.virtualTracks.clear();
    mainPlaylist.index = 0;
    
    Serial.println("Building Main Playlist (Virtual)...");
    
    // Aggregate References
    for (int i = 1; i <= 4; i++) {
        for (size_t idx = 0; idx < categories[i].tracks.size(); idx++) {
            mainPlaylist.virtualTracks.push_back({(uint8_t)i, (uint16_t)idx});
        }
    }
    
    if (mainPlaylist.virtualTracks.empty()) {
        Serial.println("Warning: No tracks found in any category!");
    } else {
        auto rng = std::default_random_engine(esp_random());
        std::shuffle(mainPlaylist.virtualTracks.begin(), mainPlaylist.virtualTracks.end(), rng);
        Serial.printf("Built Main Playlist (Virtual Items: %d)\n", mainPlaylist.virtualTracks.size());
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
    
    if (target == nullptr || (target->isVirtual ? target->virtualTracks.empty() : target->tracks.empty())) {
        // Fallback to Main if specific empty
        if (category != 0 && !mainPlaylist.virtualTracks.empty()) { // Main is always virtual now
             target = &mainPlaylist; 
             category = 0; 
        } else {
             return "";
        }
    }
    
    String track = "";

    if (target->isVirtual) {
        // --- Virtual Playlist Logic ---
        if ((size_t)target->index >= target->virtualTracks.size()) {
            Serial.printf("Playlist %d (Virtual) finished! Reshuffling...\n", category);
            // Shuffle virtual tracks
            auto rng = std::default_random_engine(esp_random());
            std::shuffle(target->virtualTracks.begin(), target->virtualTracks.end(), rng);
            target->index = 0;
            // No SD save for virtual playlist shuffle state currently supported/needed
        }
        
        Playlist::TrackRef ref = target->virtualTracks[target->index];
        // Validate Ref
        if (categories.find(ref.cat) != categories.end() && ref.idx < categories[ref.cat].tracks.size()) {
            track = categories[ref.cat].tracks[ref.idx];
        } else {
            Serial.println("Invalid Virtual Track Reference!");
            track = "/system/error_tone.wav"; // Fallback
        }
    } else {
        // --- Standard Playlist Logic ---
        if ((size_t)target->index >= target->tracks.size()) {
            Serial.printf("Playlist %d finished! Reshuffling...\n", category);
            auto rng = std::default_random_engine(esp_random());
            std::shuffle(target->tracks.begin(), target->tracks.end(), rng);
            target->index = 0;
            
            savePlaylistToSD(category, *target);
        }
        track = target->tracks[target->index];
    }
    
    // Increment and Save Progress
    target->index++; 
    saveProgressToSD(category, target->index);
    
    return track;
}


// --- Helper Functions ---
enum AudioOutput { OUT_NONE, OUT_HANDSET, OUT_SPEAKER };
AudioOutput currentOutput = OUT_NONE; 

void setAudioOutput(AudioOutput target) {
    if (audio == nullptr) return;
    if (currentOutput == target) return; 

    // PCM5100A Shared Bus Logic (Stereo DAC)
    // Left Channel = Handset
    // Right Channel = Base Speaker
    
    // Force Mono Mix so the audio content plays on the selected speaker
    audio->forceMono(true);

    if (target == OUT_HANDSET) {
        audio->setBalance(-16); // Left Only
        int vol = ::map(settings.getVolume(), 0, 42, 0, 21); // Fixed ambiguous map
        audio->setVolume(vol);
        Serial.printf("Output Switched: HANDSET (Vol: %d)\n", vol);
    } else {
        audio->setBalance(16); // Right Only
        int vol = ::map(settings.getBaseVolume(), 0, 42, 0, 21); // Fixed ambiguous map
        audio->setVolume(vol);
        Serial.printf("Output Switched: SPEAKER (Vol: %d)\n", vol);
    }
    currentOutput = target;
    delay(20); 
}

void playSound(String filename, bool useSpeaker = false) {
    if (!sdAvailable) {
        Serial.println("SD Not Available, cannot play: " + filename);
        return;
    }
    setAudioOutput(useSpeaker ? OUT_SPEAKER : OUT_HANDSET);

    if (SD.exists(filename)) {
        // statusLed handled by loop/state
        audio->connecttoFS(SD, filename.c_str());
    } else {
        Serial.print("File missing: ");
        Serial.println(filename);
        // statusLed handled by loop/state
    }
}

#include <driver/adc.h> // Include Legacy ADC driver

// Check Battery Voltage (Lolin D32 Pro: Divider 100k/100k on IO35)
bool checkBattery() {
    return true; // DISABLED due to I2S ADC Conflict
}

// --- Time Speaking Logic ---
enum TimeSpeakState { TIME_IDLE, TIME_INTRO, TIME_HOUR, TIME_UHR, TIME_MINUTE, TIME_DONE };
TimeSpeakState timeState = TIME_IDLE;
int currentHour = 0;
int currentMinute = 0;

void speakTime() {
    Serial.println("Speaking Time...");
    
    TimeManager::DateTime dt = timeManager.getLocalTime();
    
    if (dt.valid) {
        currentHour = dt.hour;
        currentMinute = dt.minute;
    } else {
        Serial.println("Time Invalid (No Sync), using 12:00");
        currentHour = 12;
        currentMinute = 0;
    }

    setAudioOutput(OUT_HANDSET);
    timeState = TIME_INTRO;
    String lang = settings.getLanguage();
    playSound("/time/" + lang + "/intro.mp3", false);
}

void processTimeQueue() {
    if (timeState == TIME_IDLE || timeState == TIME_DONE) return;

    // This function is called from audio_eof_mp3 to trigger next file
    String nextFile = "";
    String lang = settings.getLanguage();
    String basePath = "/time/" + lang + "/";

    switch (timeState) {
        case TIME_INTRO: // Intro finished, play Hour
            timeState = TIME_HOUR;
            nextFile = basePath + "h_" + String(currentHour) + ".mp3";
            break;
            
        case TIME_HOUR: // Hour finished, play "Uhr"
            timeState = TIME_UHR;
            nextFile = basePath + "uhr.mp3";
            break;
            
        case TIME_UHR: // "Uhr" finished, play Minute (if not 0)
            if (currentMinute == 0) {
                timeState = TIME_DONE;
                Serial.println("Time Speak Done (Exact Hour)");
            } else {
                timeState = TIME_MINUTE;
                if (currentMinute < 10) {
                     // Play "null five" format? 
                     nextFile = basePath + "m_0" + String(currentMinute) + ".mp3";
                } else {
                     nextFile = basePath + "m_" + String(currentMinute) + ".mp3";
                }
            }
            break;
            
        case TIME_MINUTE: // Minute finished
            timeState = TIME_DONE;
            Serial.println("Time Speak Done");
            break;
            
        default: break;
    }
    
    if (nextFile.length() > 0) {
        Serial.print("Next Time File: "); Serial.println(nextFile);
        playSound(nextFile, false);
    }
}

void speakCompliment(int number) {
    // Check if AI is configured (Skip for 0 = Random Mix from SD)
    String key = settings.getGeminiKey();
    
    if (number != 0 && key.length() > 5) { // Assuming a valid key
        // statusLed handled by loop/state
        Serial.println("AI Mode Active");
        
        // --- Retro Thinking Sound ---
        // Play sound BEFORE request to bridge the gap.
        // Needs a loop to play out, as getCompliment blocks.
        Serial.println("Playing Thinking Sound...");
        playSound("/system/computing.wav", false); // Assuming file exists
        unsigned long startThink = millis();
        // Play for max 3 seconds or until file ends
        while (audio->isRunning() && (millis() - startThink < 3000)) {
            audio->loop(); 
            delay(1);
        }
        audio->stopSong(); // Clean break
        // ----------------------------
        
        // 1. Get Text from Gemini
        String text = ai.getCompliment(number);
        
        if (text.length() > 0) {
            // 2. Get TTS URL
            String ttsUrl = ai.getTTSUrl(text);
            
            // 3. Play
            Serial.println("Streaming TTS...");
            setAudioOutput(OUT_HANDSET);
            // statusLed handled by loop/state
            audio->connecttohost(ttsUrl.c_str());
            return;
        } else {
            Serial.println("AI Failed, falling back to SD");
            // statusLed handled by loop/state
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
         path = "/system/error_tone.wav"; 
    }
    
    Serial.printf("Playing compliment (Cat: %d): %s\n", category, path.c_str());
    playSound(path, false);
}

void playPreviewSound(String type, int index) {
    if (audio->isRunning()) audio->stopSong();
    
    // Play on Speaker if On-Hook, Handset if Off-Hook
    bool offHook = dial.isOffHook();
    // setAudioOutput(offHook ? OUT_HANDSET : OUT_SPEAKER); // playSound handles this
    
    String path = "";
    if (type == "ring") {
        path = "/ringtones/" + String(index) + ".wav";
    } else if (type == "dt") {
        path = "/system/dialtone_" + String(index) + ".wav";
    }
    
    if (path.length() > 0) {
        Serial.printf("Preview Sound: %s\n", path.c_str());
        playSound(path, !offHook); // Use Speaker if NOT off-hook
    }
}

void startAlarm(bool isTimer) {
    isAlarmRinging = true;
    isTimerRinging = isTimer; 
    ringingStartTime = millis();
    currentAlarmVol = 5; 
    Serial.println(isTimer ? "TIMER ALERT STARTED" : "ALARM CLOCK RINGING");
    
    setAudioOutput(OUT_SPEAKER);
    audio->setVolume(::map(currentAlarmVol, 0, 42, 0, 21)); 
    
    playSound("/ringtones/" + String(settings.getRingtone()) + ".wav", true);
}

void stopAlarm() {
    isAlarmRinging = false;
    isTimerRinging = false;
    digitalWrite(CONF_PIN_VIB_MOTOR, LOW); 
    if (audio->isRunning()) audio->stopSong();
    
    timeManager.cancelTimer(); 
    
    setAudioOutput(OUT_HANDSET); 
    Serial.println("Alarm Stopped");
}

void startSnooze() {
    stopAlarm();
    timeManager.startSnooze();
    Serial.println("Snooze Started.");
}

void cancelSnooze() {
    timeManager.cancelSnooze();
    Serial.println("Snooze Cancelled.");
}

void handleAlarmLogic() {
    if (!isAlarmRinging) return;
    
    unsigned long duration = millis() - ringingStartTime;
    // Vibration Pattern
    bool vibOn = (duration % 1000) < 500;
    digitalWrite(CONF_PIN_VIB_MOTOR, vibOn ? HIGH : LOW);
    
    // Volume Ramp
    int maxVol = settings.getBaseVolume();
    int rampStep = duration / 3000; 
    int newVol = 5 + rampStep;
    
    if (newVol > maxVol) newVol = maxVol;
    
    if (newVol != currentAlarmVol) {
        currentAlarmVol = newVol;
        audio->setVolume(::map(currentAlarmVol, 0, 42, 0, 21));
    }
}

// --- Callbacks ---

// Helper to format IP for speech
String formatIpForSpeech(IPAddress ip) {
    String s = ip.toString();
    String dot = (settings.getLanguage() == "de") ? " punkt " : " dot ";
    s.replace(".", dot);
    return s;
}

void executePhonebookFunction(String func, String param) {
    Serial.printf("Executing Function: %s [%s]\n", func.c_str(), param.c_str());
    String lang = settings.getLanguage();

    if (func == "ANNOUNCE_TIME" || func == "SPEAK_TIME") {
        speakTime();
    }
    else if (func == "COMPLIMENT_MIX") {
        speakCompliment(0);
    }
    else if (func == "COMPLIMENT_CAT") {
        int cat = param.toInt();
        speakCompliment(cat > 0 ? cat : 0);
    }
    else if (func == "VOICE_MENU") {
        // Plays /system/menu_de.mp3 or /system/menu_en.mp3
        String path = "/system/menu_" + lang + ".mp3";
        if (SD.exists(path)) {
            playSound(path, false);
        } else {
            // Fallback TTS if file missing
            String text = (lang == "de") ? 
                "System Menü. Wähle 9 0 um alle Alarme ein- oder auszuschalten. 9 1 um den nächsten Routine-Wecker zu überspringen. 8 für Status." :
                "System Menu. Dial 9 0 to toggle all alarms. 9 1 to skip the next routine alarm. 8 for status.";
            if (ai.hasApiKey()) {
                audio->connecttohost(ai.getTTSUrl(text).c_str());
            }
        }
    }
    else if (func == "SYSTEM_STATUS") {
        String statusText = "";
        if (lang == "de") {
            statusText += "System Status. ";
            statusText += "WLAN Signal " + String(WiFi.RSSI()) + " dB. ";
            statusText += "IP Adresse " + formatIpForSpeech(WiFi.localIP()) + ". ";
        } else {
            statusText += "System Status. ";
            statusText += "WiFi Signal " + String(WiFi.RSSI()) + " dB. ";
            statusText += "IP Address " + formatIpForSpeech(WiFi.localIP()) + ". ";
        }
        
        if (ai.hasApiKey()) {
             setAudioOutput(OUT_HANDSET);
             audio->connecttohost(ai.getTTSUrl(statusText).c_str());
        } else {
             Serial.println(statusText);
             playSound("/system/beep.wav", false);
        }
    }
    else if (func == "TOGGLE_ALARMS") {
        timeManager.setAlarmsEnabled(!timeManager.areAlarmsEnabled());
        bool alarmsEnabled = timeManager.areAlarmsEnabled();
        
        // Try playing file first
        String fName = alarmsEnabled ? "alarms_on" : "alarms_off";
        String path = "/system/" + fName + "_" + lang + ".mp3";
        
        if (SD.exists(path)) {
            playSound(path, false);
        } else {
            String msg = "";
            if (lang == "de") {
                msg = alarmsEnabled ? "Alarme aktiviert." : "Alarme deaktiviert.";
            } else {
                msg = alarmsEnabled ? "Alarms enabled." : "Alarms disabled.";
            }
            if (ai.hasApiKey()) {
                audio->connecttohost(ai.getTTSUrl(msg).c_str());
            }
        }
    }
    else if (func == "SKIP_NEXT_ALARM") {
        bool newState = !timeManager.isSkipNextAlarmSet();
        timeManager.setSkipNextAlarm(newState);
        
        // File paths: alarm_skipped (Active Skip) vs alarm_active (Normal)
        String fName = newState ? "alarm_skipped" : "alarm_active";
        String path = "/system/" + fName + "_" + lang + ".mp3";
        
        if (SD.exists(path)) {
            playSound(path, false);
        } else {
            String msg = "";
            if (lang == "de") {
                msg = newState ? "Nächster wiederkehrender Alarm übersprungen." : "Wiederkehrender Alarm wieder aktiv.";
            } else {
                msg = newState ? "Next recurring alarm skipped." : "Recurring alarm reactivated.";
            }
            if (ai.hasApiKey()) {
                audio->connecttohost(ai.getTTSUrl(msg).c_str());
            }
        }
    }
    else {
        Serial.println("Unknown Function type");
    }
}

// Central Logic for Dialed Numbers (Phonebook + Features)
void handleDialedNumber(String numberStr) {
    
    // Check Phonebook First
    PhonebookEntry entry = phonebook.getEntry(numberStr);
    
    if (entry.type.length() > 0) {
        Serial.printf("Phonebook Match: %s (%s)\n", entry.name.c_str(), entry.type.c_str());
        
        if (entry.type == "FUNCTION") {
             executePhonebookFunction(entry.value, entry.parameter);
        }
        else if (entry.type == "AUDIO") {
            // Play specific file
             Serial.printf("Playing Audio: %s\n", entry.value.c_str());
             playSound(entry.value, false);
        }
        else if (entry.type == "TTS") {
             Serial.printf("TTS Req: %s\n", entry.value.c_str());
             if (ai.hasApiKey()) {
                 // Add parameter hint (e.g. style) if needed
                 String text = entry.value; 
                 // If value is a prompt (not direct text), we might need to ask Gemini first?
                 // But Current Phonebook Spec says "value" is the prompt / text.
                 // Let's assume for "TTS" type we feed it to Gemini to generate the audio, 
                 // OR if it's static text, we just read it (via Google TTS).
                 // For "Admin Menu" text, we want direct TTS.
                 // For "Make a joke", we want Gemini Generation.
                 
                 // Distinction: Is it a Prompt or Text to Read?
                 // If Type is TTS, we assume it is TEXT TO READ directly (like Menu).
                 // If we want generation, we should use a FUNCTION "GEMINI_PROMPT" or similar?
                 // Or we detect based on content?
                 // Current implementation of 'getCompliment' generates.
                 // Let's keep it simple: TTS type = Direct Speech. 
                 // If phonebook has "Erzähle einen Witz", that's a prompt for generation.
                 // Let's change those defaults to FUNCTION "AI_PROMPT" or handle TTS as prompt?
                 
                 // Refined Logic based on user request:
                 // The "Nerd Joke" entry 5 has value "Erzähle einen Witz". This is a PROMPT.
                 // The "Admin Menu" entry 9 has "Willkommen...". This is TEXT.
                 
                 // HEURISTIC: If value starts with "Erzähle", "Sag", "Generiere", "Gib" -> Prompt.
                 // Otherwise -> Read.
                 String v = entry.value;
                 if (v.startsWith("Erzähle") || v.startsWith("Sag") || v.startsWith("Generiere") || v.startsWith("Gib")) {
                     // Generate
                      String generated = ai.callGemini(v); // We need to expose callGemini publicly or add method
                      if(generated.length() > 0) {
                          audio->connecttohost(ai.getTTSUrl(generated).c_str());
                      }
                 } else {
                     // Read directly
                     audio->connecttohost(ai.getTTSUrl(v).c_str());
                 }
             }
        }
    } else {
        // Fallback for Timer setting (1-60)
         int num = numberStr.toInt();
         if (num > 0 && num <= 60 && numberStr.length() <= 2) {
            Serial.printf("Setting Timer for %d minutes\n", num);
            timeManager.setTimer(num);
            playSound("/system/beep.wav", true); 
         } else {
            // Unknown
            Serial.println("Unknown Number Dialed");
            playSound("/system/error_tone.wav", false);
         }
    }
}

void processBufNumber(String numberStr) {
    Serial.printf("Processing Buffered Input: %s\n", numberStr.c_str());

    // 1. Logic: Setting Alarm Clock (Button Held + Dialing 4 digits)
    if (dial.isButtonDown()) {
        if (numberStr.length() == 4) {
            int h = numberStr.substring(0, 2).toInt();
            int m = numberStr.substring(2, 4).toInt();
            
            if (h >= 0 && h < 24 && m >= 0 && m < 60) {
                timeManager.setAlarm(h, m);
                Serial.printf("ALARM SET TO: %02d:%02d\n", h, m);
                
                // Audio Feedback
                setAudioOutput(OUT_HANDSET);
                playSound("/system/timer_set.mp3", false); 
            } else {
                Serial.println("Invalid Time Format");
                playSound("/system/error_tone.wav", false);
            }
        } else {
            // Wrong length for Alarm Setting
             playSound("/system/error_tone.wav", false);
        }
        return;
    }

    // 2. Normal Dialing (Off Hook)
    if (dial.isOffHook()) {
        handleDialedNumber(numberStr);
    } 
    // 3. Timer Setting (On Hook)
    else {
        // Interpret number as Minutes
         int num = numberStr.toInt();
         if (num > 0 && num <= 120) { // Allow up to 120 minutes
            Serial.printf("Setting Timer for %d minutes\n", num);
            timeManager.setTimer(num);
            playSound("/system/beep.wav", true); 
         } else {
            // Unknown
            Serial.println("Invalid Timer Value");
            playSound("/system/error_tone.wav", false);
         }
    }
}

void onDial(int number) {
    if (number == 10) number = 0; // Fix 0
    Serial.printf("Digit Received: %d\n", number);
    
    // Reset Buffer if it was stale (Safety net, though loop handles timeout)
    // Actually loop handles dispatch, clearing buffer provided we call it.
    
    // Append Digit
    dialBuffer += String(number);
    lastDialTime = millis();
    
    // Immediate Feedback (Click/Beep)
    // Don't play loud beep if handset is on hook? 
    // Maybe play quietly on speaker if on hook?
    // Let's use Handset if OffHook, Speaker if OnHook?
    // For now, simple beep via current output.
    // playSound can be disruptive to currently playing audio?
    // Only beep if silence?
    // Ideally: Short mechanical click simulation or just silence (rotary is physical).
    // Let's play a very short beep if nothing is playing?
    // if (!audio->isRunning()) playSound("/system/click.wav", false);
    
    // Special Case: ALARM SETTING (Button Held)
    // If we have 4 digits, we can execute immediately without waiting for timeout
    // to give "snappy" feel.
    if (dial.isButtonDown() && dialBuffer.length() == 4) {
        processBufNumber(dialBuffer);
        dialBuffer = ""; // Clear immediately
    }
}

// Forward Declarations
String getSystemFileByIndex(int index);
void playDialTone();
void stopDialTone();

void onHook(bool offHook) {
    Serial.printf("Hook State: %s\n", offHook ? "OFF HOOK (Picked Up)" : "ON HOOK (Hung Up)");
    
    // 2. Logic: Delete Alarm (Button + Lift)
    if (offHook && dial.isButtonDown()) {
        timeManager.deleteAlarm();
        Serial.println("Alarm Deleted/Disabled");
        // Audio Feedback
        setAudioOutput(OUT_HANDSET);
        playSound("/system/error_tone.wav", false); // "Deleted" tone
        return;
    }

    if (offHook) {
        // statusLed.setIdle();
        
        // Stop Alarm if ringing
        if (isAlarmRinging) {
            startSnooze(); // Lift = Snooze Start (Put Aside)
            return;
        }
        
        if (timeManager.isTimerRunning()) {
             timeManager.cancelTimer();
             stopAlarm(); // Should stop timer ringing too
             Serial.println("Timer Stopped by Pickup");
             return; // Don't play compliment logic
        }
        
        // --- Battery Check ---
        if (!checkBattery()) {
            Serial.println("Warn: Battery Low!");
            playSound("/system/battery_crit.mp3", false);
            return; 
        }
        
        // NEW Standard Behavior: Play Dial Tone
        Serial.println("OFF HOOK -> Playing Dial Tone");
        playDialTone();

        // Automatic Surprise Mix on Pickup - REMOVED for Realism
        // speakCompliment(0); 
    } else {
        // ON HOOK (Hung Up)
        // Ensure Tone is stopped
        stopDialTone();
        
        if (timeManager.isSnoozeActive()) {
            // User hung up during snooze -> Cancel Snooze (I am awake)
            cancelSnooze();
        }
        
        if (audio->isRunning()) {
            audio->stopSong();
            // statusLed.setIdle();
        }
    }
}

void onButton() {
    // Interruption Logic: Stop Speaking if Button Pressed
    if (audio->isRunning()) {
        Serial.println("Interruption: Button Pressed -> Stopping Audio");
        audio->stopSong();
        return; 
    }

    // If not holding for combo, speak time?
    // Let's keep speakTime behavior if released quickly?
    // But `isButtonDown` is updated in `RotaryDial` on press/release.
    // This callback `onButton` (Press) is ok.
    Serial.println("Extra Button Pressed: Speak Time");
    speakTime();
}

// --- Deep Sleep & Power Management ---
// Configured in config.h: CONF_SLEEP_TIMEOUT_MS

RTC_DATA_ATTR int bootCount = 0; // Store in RTC memory

bool isUsbPowerConnected() {
    // DISABLED for now due to I2S ADC Conflict (Both use ADC1)
    return true; // Assume USB Power -> No Deep Sleep
}

void setGpsStandby() {
    Serial.println("GPS: Entering Standby (UBX-RXM-PMREQ)");
    // UBX-RXM-PMREQ (Backup Mode, Infinite Duration)
    uint8_t packet[] = {
        0xB5, 0x62, 0x02, 0x41, 0x08, 0x00, 
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x4D, 0x3B
    };
    Serial2.write(packet, sizeof(packet));
    delay(100); // Give it time to sleep
}

void wakeGps() {
    Serial.println("GPS: Waking Up...");
    // Send dummy bytes to wake UART
    Serial2.write(0xFF);
    delay(100);
}

unsigned long lastActivityTime = 0;

/*
 * SETUP I2S ADC (Input from MAX9814)
 * Using I2S_NUM_0 in ADC Built-In Mode (REQUIRED for ADC)
 * GPIO 36 is ADC1_CHANNEL_0
 */
void setupI2S_ADC() {
    // 16-bit sampling, only need mono
    // Note: Built-in ADC mode requires higher sample rates to satisfy clock dividers (approx > 22kHz)
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
        .sample_rate = 44100, 
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, 
        .communication_format = I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 128,
        .use_apll = false
    };

    // Install Driver on I2S_NUM_0
    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("Failed installing I2S ADC driver: %d\n", err);
        return;
    }

    // Configure ADC mode
    // GPIO 36 is ADC1_CHANNEL_0
    err = i2s_set_adc_mode(ADC_UNIT_1, ADC1_CHANNEL_0);
    if (err != ESP_OK) {
        Serial.printf("Failed setting ADC mode: %d\n", err);
    } else {
        i2s_adc_enable(I2S_NUM_0);
        Serial.println("I2S ADC (Mic) Initialized on I2S_NUM_0");
    }
}

// --- HELPER DIAL TONE ---
String getSystemFileByIndex(int index) {
    File dir = SD.open("/system");
    if(!dir || !dir.isDirectory()) return "";
    
    std::vector<String> files;
    File file = dir.openNextFile();
    while(file){
        if(!file.isDirectory()) {
            String name = file.name();
            if (!name.startsWith(".")) files.push_back(name);
        }
        file = dir.openNextFile();
    }
    std::sort(files.begin(), files.end());
    
    // Index is 1-based (from settings dropdown)
    if (index > 0 && index <= files.size()) {
        return "/system/" + files[index - 1];
    }
    return "";
}

void playDialTone() {
    if (!sdAvailable) return;
    int toneIndex = settings.getDialTone();
    String freqFile = getSystemFileByIndex(toneIndex);
    
    if (freqFile != "") {
        Serial.print("Starting Dial Tone: "); Serial.println(freqFile);
        audio->setFileLoop(true); // Loop!
        audio->connecttoFS(SD, freqFile.c_str());
        isDialTonePlaying = true;
    }
}

void stopDialTone() {
    if (isDialTonePlaying && audio && audio->isRunning()) {
        audio->stopSong();
        audio->setFileLoop(false);
        isDialTonePlaying = false;
        Serial.println("Dial Tone Stopped.");
    }
}
// --- END HELPER ---

void setup() {
    Serial.begin(CONF_SERIAL_BAUD);
    
    // Check Wakeup Cause
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("Woke up from Deep Sleep (Hook Lift)!");
    } else {
        Serial.println("Power On / Reset");
    }

    settings.begin();
    ledManager.begin();
    ledManager.setMode(LedManager::CONNECTING); // Start sequence
    
    // Init SPIFFS for Phonebook
    if(!SPIFFS.begin(true)){
        Serial.println("SPIFFS Mount Failed");
    }

    phonebook.begin(); // Load Phonebook from SPIFFS
    timeManager.begin(); // Added TimeManager
    
    // --- Hardware Init ---
    
    // 1. Init SD 
    if(!SD.begin(CONF_PIN_SD_CS)){
        Serial.println("SD Card Mount Failed (No Card?)");
        ledManager.setMode(LedManager::SOS);
        sdAvailable = false;
    } else {
        Serial.println("SD Card Mounted");
        sdAvailable = true;
        buildPlaylist();
    }
    
    // 2. Init Audio (PCM5100A)
    // Audio(internalDAC, channelEnabled, i2sPort)
    // We use I2S_NUM_1 for DAC to leave I2S_NUM_0 for the internal ADC (Mic)
    audio = new Audio(false, 3, I2S_NUM_1);
    audio->setPinout(CONF_I2S_BCLK, CONF_I2S_LRC, CONF_I2S_DOUT);
    audio->forceMono(true); // Critical for separation via Balance
    
    // Start with Handset
    setAudioOutput(OUT_HANDSET);

    // 3. Init Mic (ADC)
    setupI2S_ADC();
    
    dial.onDialComplete(onDial);
    dial.onHookChange(onHook);
    dial.onButtonPress(onButton);
    dial.begin();
    
    webManager.begin();
    
    // If we woke from Deep Sleep, the GPS is likely in Backup Mode. Wake it up!
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        wakeGps();
    }
    
    Serial.println("Dial-A-Charmer Started (PCM5100A + MAX9814)");
    
    // Init Motor
    pinMode(CONF_PIN_VIB_MOTOR, OUTPUT);
    digitalWrite(CONF_PIN_VIB_MOTOR, LOW);
    
    // Initial Beep
    setAudioOutput(OUT_HANDSET);
    playSound("/system/beep.wav", false); // Speaker

    // Start Background Scan for updated Persona Names
    startPersonaScan();
}

void loop() {
    static unsigned long lastLoopDebug = 0;
    if(millis() - lastLoopDebug > 2000) {
        lastLoopDebug = millis();
        Serial.printf("[LOOP] running... AudioRun: %d, WebClient: ?, FreeHeap: %u\n", (audio?audio->isRunning():0), ESP.getFreeHeap());
    }

    if(audio) audio->loop();
    webManager.loop();
    dial.loop();
    
    // --- PULSE FEEDBACK (Mechanical Click) ---
    if (dial.hasNewPulse()) {
        // 1. Stop Dial Tone if active (First pulse logic)
        if (isDialTonePlaying) {
             stopDialTone();
        }
        
        // 2. Play Click if the channel is free
        // Note: rapid re-triggering might cause stutter if file is too large. 
        // Use a very short <50ms file: /system/click.wav
        // if (!audio->isRunning()) {
        //    audio->connecttoFS(SD, "/system/click.wav");
        // }
        // Actually, re-triggering connecttoFS is heavy.
        // Better: Just mute/unmute or toggle a pin if we had a buzzer.
        // Since we only have I2S, and it's single stream, we skipping audio feedback for now if song is playing.
        // But for DialTone (which we just stopped), we are free.
        
        if (!audio->isRunning()) {
             // For now, we attempt to play. Ensure file is TINY.
             // If loop latency is high, this will slow down dialing detection!
             // Proceed with caution.
             if (SD.exists("/system/click.wav")) {
                audio->connecttoFS(SD, "/system/click.wav");
             }
        }
    }

    // --- DIAL TONE LOGIC ---
    if (isDialTonePlaying && dial.isDialing()) {
        stopDialTone();
        // audio->setVolume(0); // Optional: Mute click noise?
    }

    timeManager.loop();
    handlePersonaScan(); // Run BG Task
    
    // --- Buffered Dialing Logic ---
    if (dialBuffer.length() > 0) {
        #define DIAL_CMD_TIMEOUT 3000

        if (millis() - lastDialTime > DIAL_CMD_TIMEOUT) {
            Serial.printf("Dialing Finished (Timeout). Buffer: %s\n", dialBuffer.c_str());
            processBufNumber(dialBuffer);
            dialBuffer = "";
        }
    }

    // --- Triggers ---
    if (timeManager.checkTimerTrigger()) {
        startAlarm(true);
    }
    if (timeManager.checkAlarmTrigger()) {
        startAlarm(false);
    }
    if (timeManager.checkSnoozeExpired()) {
         Serial.println("Snooze Expired -> Ringing!");
         startAlarm(false);
    }

    // --- LED Updates ---
    // Throttle LED updates to prevent starving the Audio loop
    static unsigned long lastLedUpdate = 0;
    
    // STRICT MODE: DISABLE LEDS WHEN AUDIO IS RUNNING
    // This prevents RMT interrupts from crashing I2S
    bool isAudioRunning = audio->isRunning();
    unsigned long ledInterval = (isAudioRunning) ? 999999 : 50; 

    if (millis() - lastLedUpdate > ledInterval && !isAudioRunning) {
        lastLedUpdate = millis();
        TimeManager::DateTime dt = timeManager.getLocalTime();
        int h = dt.valid ? dt.hour : -1;
        ledManager.update();
    }
    
    // LED State Logic
    if (isAlarmRinging) {
        if (isTimerRinging) {
             ledManager.setMode(LedManager::TIMER_ALERT); // Red Fast
        } else {
             ledManager.setMode(LedManager::ALARM_CLOCK); // Warm White Pulse
        }
    } 
    else if (timeManager.isSnoozeActive()) {
        ledManager.setMode(LedManager::SNOOZE_MODE); // Warm White Solid
    }
    else if (!sdAvailable) { // Removed audioCodecAvailable check
         ledManager.setMode(LedManager::SOS);
    }
    else if (WiFi.status() == WL_CONNECTED) {
        ledManager.setMode(LedManager::IDLE_GLOW);
    }
    else {
         // Not Connected -> Search Mode
         ledManager.setMode(LedManager::CONNECTING);
    }
    
    if (isAlarmRinging) {
        handleAlarmLogic();
    }
    
    // --- HALF-DUPLEX AEC ---
    // DISABLED TEMPORARILY due to I2C Errors causing potential instability
    /*
    static bool wasAudioRunning = false;
    bool isAudioRunning = audio->isRunning();
    
    if (settings.getHalfDuplex()) {
        if (isAudioRunning && !wasAudioRunning) {
            Serial.println("AEC: Audio Start -> Muting Mic");
            audioCodec.muteMic(true);
        } 
        else if (!isAudioRunning && wasAudioRunning) {
            Serial.println("AEC: Audio Stop -> Unmuting Mic");
            audioCodec.muteMic(false);
        }
    } else if (wasAudioRunning && !isAudioRunning) {
        // Ensure unmuted if we toggled setting off during playback
        audioCodec.muteMic(false);
    }
    wasAudioRunning = isAudioRunning;
    */

    // --- AP Mode Long Press ---
    static unsigned long btnPressStart = 0;
    if (dial.isButtonDown()) {
        if (btnPressStart == 0) btnPressStart = millis();
        if (millis() - btnPressStart > 10000) { // 10 Seconds
             Serial.println("Long Press Detected: Starting AP Mode");
             playSound("/system/beep.wav", false);
             webManager.startAp();
             btnPressStart = 0; // Trigger once
             // Wait for release
             while(dial.isButtonDown()) { dial.loop(); delay(10); }
        }
    } else {
        btnPressStart = 0;
    }
    
    // --- SMART DEEP SLEEP LOGIC ---
    // Prevent sleep if: Off Hook (Active), Audio Playing, Timer Running, or USB Power Detected
    bool systemBusy = dial.isOffHook() || audio->isRunning() || timeManager.isTimerRunning() || isAlarmRinging || timeManager.isSnoozeActive() || isUsbPowerConnected();
    
    if (systemBusy) {
        lastActivityTime = millis();
    } else {
        if (millis() - lastActivityTime > CONF_SLEEP_TIMEOUT_MS) {
            Serial.println("zzZ Entering Smart Deep Sleep (Preserving GPS) zzZ");
            
            // 1. Send GPS to Sleep (Backup Mode)
            setGpsStandby();
            
            // 2. Configure Wakeup
            // We wake when the hook is lifted.
            // Check current state to wake on change/toggle
            int currentHookState = digitalRead(CONF_PIN_HOOK);
            esp_sleep_enable_ext0_wakeup((gpio_num_t)CONF_PIN_HOOK, !currentHookState);
            
            // 3. Status LED Off
            ledManager.setMode(LedManager::OFF);
            ledManager.update(); // Flush change
            
            // 4. Goodnight
            esp_deep_sleep_start();
        }
    }
}

// Audio Events
void audio_eof_mp3(const char *info){
    Serial.print("EOF: "); Serial.println(info);
    
    // Handle Time Speaking Queue
    if (timeState != TIME_IDLE && timeState != TIME_DONE) {
        processTimeQueue();
        return; // Don't idle LED yet
    }

    // statusLed.setIdle();
    
    if (isAlarmRinging) {
         // Loop Alarm Sound
         playSound("/ringtones/" + String(settings.getRingtone()) + ".wav", true);
         
         // Note: setVolume might be reset by playSound internal logic if we called setAudioOutput again?
         // Our modified playSound calls setAudioOutput. 
         // setAudioOutput resets volume to settings.getBaseVolume().
         // FIXME: We need to force current ramp volume back.
         audio->setVolume(::map(currentAlarmVol, 0, 42, 0, 21));
    }
}

// Called when stream (TTS) finishes
void audio_eof_stream(const char *info){
    Serial.print("EOF Stream: "); Serial.println(info);
    // statusLed.setIdle();
}
