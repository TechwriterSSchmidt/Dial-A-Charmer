#include <Arduino.h>
#include <Audio.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <LittleFS.h>
#include <esp_task_wdt.h> // Watchdog
#include <vector>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define WDT_TIMEOUT 30 // 30 Seconds Watchdog Limit (needed for WebServer HTML generation)

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

#if HAS_ES8388_CODEC
#include <es8388.h>
es8388 codec;
// Default Output: Headphone (L+R)
es_dac_output_t currentCodecOutput = (es_dac_output_t)(DAC_OUTPUT_LOUT1 | DAC_OUTPUT_ROUT1); 
#endif

// --- Objects ---
Audio *audio = nullptr; // Pointer managed object (initialized in Task)
volatile bool isAudioBusy = false; // Thread-safe flag

RotaryDial dial(CONF_PIN_DIAL_PULSE, CONF_PIN_HOOK, CONF_PIN_EXTRA_BTN, CONF_PIN_DIAL_MODE);
LedManager ledManager(CONF_PIN_LED);

// --- AUDIO MULTITHREADING SETUP ---
QueueHandle_t audioQueue;
SemaphoreHandle_t sdMutex = NULL; // Mutex for SD card access
TaskHandle_t webServerTaskHandle = NULL; // Web Server Task

enum AudioCmdType { CMD_PLAY, CMD_STOP, CMD_VOL, CMD_OUT, CMD_CONNECT_SPEECH };
struct AudioCmd {
    AudioCmdType type;
    char path[128]; // Filename or URL
    int value;      // Volume or Output ID
};

// Forward Declaration needed for task
void audioTaskCode(void * parameter);
void webServerTaskCode(void * parameter);
void playSequence(std::vector<String> files, bool useSpeaker);
void clearSpeechQueue();

// Global State
std::vector<String> globalSpeechQueue;
bool globalSpeechQueueUseSpeaker = false;
bool sdAvailable = false;
bool isDialTonePlaying = false; // New Dial Tone State

// --- Timeout / Busy Logic ---
bool isLineBusy = false;       
unsigned long offHookTime = 0; 
#define OFF_HOOK_TIMEOUT 5000  

// Note: PCM5100A is a "dumb" DAC and doesn't report I2C status. We assume it's working.

// --- Background Scan Globals ---
int bgScanPersonaIndex = 1;
static File bgScanDir;
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

void ensurePlaylistDir();

int loadProgressFromSD(int category, size_t maxSize) {
    if (!sdAvailable || maxSize == 0) return 0;
    String idxPath = getStoredIndexPath(category);
    if (!SD.exists(idxPath)) return 0;
    File fIdx = SD.open(idxPath);
    if (!fIdx) return 0;
    String idxStr = fIdx.readStringUntil('\n');
    fIdx.close();
    int idx = idxStr.toInt();
    if (idx < 0 || (size_t)idx >= maxSize) return 0;
    return idx;
}

void saveVirtualPlaylistToSD(Playlist &pl) {
    if (!sdAvailable) return;
    ensurePlaylistDir();
    String path = getStoredPlaylistPath(0);
    SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) return;
    for (const auto &ref : pl.virtualTracks) {
        f.print(ref.cat);
        f.print(',');
        f.println(ref.idx);
    }
    f.close();
}

bool loadVirtualPlaylistFromSD(Playlist &pl) {
    if (!sdAvailable) return false;
    String path = getStoredPlaylistPath(0);
    if (!SD.exists(path)) return false;
    File f = SD.open(path);
    if (!f) return false;

    pl.virtualTracks.clear();
    bool invalid = false;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        int comma = line.indexOf(',');
        if (comma < 0) { invalid = true; break; }
        int cat = line.substring(0, comma).toInt();
        int idx = line.substring(comma + 1).toInt();
        if (cat < 1 || cat > 5 || categories.find(cat) == categories.end()) { invalid = true; break; }
        if (idx < 0 || (size_t)idx >= categories[cat].tracks.size()) { invalid = true; break; }
        pl.virtualTracks.push_back({(uint8_t)cat, (uint16_t)idx});
    }
    f.close();

    if (invalid || pl.virtualTracks.empty()) {
        pl.virtualTracks.clear();
        return false;
    }
    return true;
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
        esp_task_wdt_reset(); // Prevent WDT Reset during long directory scans
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
    
    // --- Instant "Fortune" Detection ---
    if (SD.exists("/persona_05/fortune.txt")) {
        Serial.println("[BG] Detected Fortune Cookie Mode!");
        // Force Switch to Category Mode
        phonebook.addEntry("5", "Fortune Cookie", "FUNCTION", "COMPLIMENT_CAT", "5");
    } 
    // Removed duplicate fallback logic for Mix on 5. Mix is now on 6.
    
    bgScanPersonaIndex = 1;
    bgScanActive = true;
    bgScanPhonebookChanged = false;
    // ensure bgScanDir is closed if it was left open
    if(bgScanDir) bgScanDir.close();
}


void handlePersonaScan() {
    if (!bgScanActive || !sdAvailable) return;

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(20)) != pdTRUE) return; // try later if busy

    // Safety: Reset WDT during SD operations
    esp_task_wdt_reset();

    // Process a chunk of work
    // State 0: Open Dir if needed
    if (!bgScanDir) {
        if (bgScanPersonaIndex > 5) {
            // FINISHED
            bgScanActive = false;
            if (bgScanPhonebookChanged) {
                Serial.println("[BG] Saving updated Phonebook names...");
                phonebook.saveChanges();
            } else {
                Serial.println("[BG] Scan finished. No changes.");
            }
            xSemaphoreGive(sdMutex);
            return;
        }
        
        String path = "/persona_0" + String(bgScanPersonaIndex);
        bgScanDir = SD.open(path);
        if (!bgScanDir || !bgScanDir.isDirectory()) {
            if(bgScanDir) bgScanDir.close();
            bgScanPersonaIndex++;
            xSemaphoreGive(sdMutex);
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
            
            // Special handling for fortune.txt to capitalize it
            if (name.equalsIgnoreCase("fortune")) {
                name = "Fortune";
            }

            Serial.printf("[BG] Found Name: %s\n", name.c_str());
            
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
    } else {
        // End of Directory
        bgScanDir.close();
        bgScanPersonaIndex++;
    }

    xSemaphoreGive(sdMutex);
}

void buildPlaylist() {

    categories.clear();
    mainPlaylist.tracks.clear();
    mainPlaylist.index = 0;
    
    Serial.println("Initializing Playlists...");
    
    // Categories 1-5 (5 is Fortune/Offline)
    for (int i = 1; i <= 5; i++) {
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

    bool loadedVirtual = loadVirtualPlaylistFromSD(mainPlaylist);
    if (!loadedVirtual) {
        // Aggregate References
        for (int i = 1; i <= 5; i++) {
            for (size_t idx = 0; idx < categories[i].tracks.size(); idx++) {
                mainPlaylist.virtualTracks.push_back({(uint8_t)i, (uint16_t)idx});
            }
        }

        if (mainPlaylist.virtualTracks.empty()) {
            Serial.println("Warning: No tracks found in any category!");
        } else {
            auto rng = std::default_random_engine(esp_random());
            std::shuffle(mainPlaylist.virtualTracks.begin(), mainPlaylist.virtualTracks.end(), rng);
            saveVirtualPlaylistToSD(mainPlaylist);
            Serial.printf("Built Main Playlist (Virtual Items: %d)\n", mainPlaylist.virtualTracks.size());
        }
    } else {
        Serial.printf("Loaded Main Playlist (Virtual Items: %d)\n", mainPlaylist.virtualTracks.size());
    }

    if (!mainPlaylist.virtualTracks.empty()) {
        mainPlaylist.index = loadProgressFromSD(0, mainPlaylist.virtualTracks.size());
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
            saveVirtualPlaylistToSD(*target);
            saveProgressToSD(0, 0);
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


// --- Audio Task & Helpers ---

void audioTaskCode(void * parameter) {
    Serial.print("[AudioTask] Started on Core "); Serial.println(xPortGetCoreID());
    
    // Create Audio Object Here (Local to Core 0)
    audio = new Audio();
    // audio->setPinout(CONF_I2S_BCLK, CONF_I2S_LRC, CONF_I2S_DOUT); // Disable manual pinout for Audio Kit, let library handle defaults or autodetect
    #ifdef BOARD_AI_THINKER_AUDIO_KIT
        // Audio Kit uses ES8388 which is managed by I2C, but data still needs I2S pins.
        // However, the library ESP32-audioI2S might have default mappings or conflicts.
        // Try setting explicit master clock or different config if needed.
        // For now, let's keep it but ensure pins are correct: 27, 25, 26.
        audio->setPinout(CONF_I2S_BCLK, CONF_I2S_LRC, CONF_I2S_DOUT);
        // audio->setI2SCommFMT_LSB(true); // DISABLED: Often causes static noise if Codec expects I2S Standard
        
        // ENABLE MCLK on GPIO 0
        // Allows ES8388 to work in Master Mode or Sync correctly
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
        WRITE_PERI_REG(PIN_CTRL, READ_PERI_REG(PIN_CTRL) & 0xFFFFFFE0);
        Serial.println("MCLK Output Enabled on GPIO 0");

    #else
        audio->setPinout(CONF_I2S_BCLK, CONF_I2S_LRC, CONF_I2S_DOUT);
    #endif
    audio->forceMono(false); // Stereo required for Audio Kit to work properly? 
    
    for(;;) {
        audio->loop();
        isAudioBusy = audio->isRunning();
        
        AudioCmd cmd;
        if(xQueueReceive(audioQueue, &cmd, 0) == pdTRUE) {
            switch(cmd.type) {
                case CMD_PLAY:
                    // Take SD mutex (wait up to 10 seconds for playback)
                    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(10000)) == pdTRUE) {
                        if (SD.exists(cmd.path)) {
                            audio->connecttoFS(SD, cmd.path);
                        } else {
                            Serial.printf("[Audio] File missing: %s\n", cmd.path);
                            if (SD.exists("/system/fallback_alarm.wav")) {
                                 audio->connecttoFS(SD, "/system/fallback_alarm.wav");
                            }
                        }
                        xSemaphoreGive(sdMutex); // Release after file is opened
                    } else {
                        Serial.println("[Audio] SD Mutex timeout, skipping playback");
                    }
                    break;
                case CMD_CONNECT_SPEECH:
                    audio->connecttospeech(cmd.path, "de");
                    break;
                case CMD_STOP:
                    audio->stopSong();
                    break;
                case CMD_VOL:
                    audio->setVolume(cmd.value);
                    #if HAS_ES8388_CODEC
                        // Update Codec Volume (Map 0-21 internal gain to 0-100 hardware gain)
                        {
                            int hwVol = ::map(cmd.value, 0, 21, 0, 100);
                            codec.config(16, currentCodecOutput, ADC_INPUT_LINPUT2_RINPUT2, hwVol);
                        }
                    #endif
                    break;

                case CMD_OUT:
                    #if HAS_ES8388_CODEC
                        // ES8388 Hardware Routing
                        if (cmd.value == 2) { 
                            // Speaker Mode
                            currentCodecOutput = (es_dac_output_t)(DAC_OUTPUT_LOUT2 | DAC_OUTPUT_ROUT2);
                            int vol = ::map(settings.getBaseVolume(), 0, 42, 0, 100);
                            codec.config(16, currentCodecOutput, ADC_INPUT_LINPUT2_RINPUT2, vol);
                            // codec.volume(vol); // Removed invalid call
                        } else {
                            // Handset Mode (Headset)
                            currentCodecOutput = (es_dac_output_t)(DAC_OUTPUT_LOUT1 | DAC_OUTPUT_ROUT1);
                            int vol = ::map(settings.getVolume(), 0, 42, 0, 100);
                            codec.config(16, currentCodecOutput, ADC_INPUT_LINPUT2_RINPUT2, vol);
                            // codec.volume(vol); // Removed invalid call
                        }
                    #endif

                    // Software Balance / Gain (Legacy support & Fine tuning)
                    if (cmd.value == 2) { 
                        audio->setBalance(16); // Right (Speaker)
                        int vol = ::map(settings.getBaseVolume(), 0, 42, 0, 21);
                        audio->setVolume(vol);
                    } else {
                         audio->setBalance(-16); // Left (Handset)
                         int vol = ::map(settings.getVolume(), 0, 42, 0, 21);
                         audio->setVolume(vol);
                    }
                    break;
            }
        }
        vTaskDelay(2 / portTICK_PERIOD_MS);
    }
}

void sendAudioCmd(AudioCmdType type, const char* path, int val) {
    AudioCmd cmd;
    cmd.type = type;
    if (path) strncpy(cmd.path, path, 127);
    else cmd.path[0] = '\0';
    cmd.value = val;
    xQueueSend(audioQueue, &cmd, 0);
}

// Web Server Task - runs on Core 1 with its own WDT resets
void webServerTaskCode(void * parameter) {
    Serial.print("[WebServerTask] Started on Core "); Serial.println(xPortGetCoreID());
    esp_task_wdt_add(NULL); // register this task
    for(;;) {
        esp_task_wdt_reset();
        webManager.loop();
        vTaskDelay(5 / portTICK_PERIOD_MS); // yield CPU
    }
}

// --- Helper Functions ---
enum AudioOutput { OUT_NONE, OUT_HANDSET, OUT_SPEAKER };
AudioOutput currentOutput = OUT_NONE; 

void setAudioOutput(AudioOutput target) {
    if (currentOutput == target) return; 
    currentOutput = target;
    sendAudioCmd(CMD_OUT, NULL, (int)target);
}

void playSound(String filename, bool useSpeaker = false) {
    if (!sdAvailable) {
        Serial.println("SD Not Available, cannot play: " + filename);
        return;
    }
    setAudioOutput(useSpeaker ? OUT_SPEAKER : OUT_HANDSET);
    sendAudioCmd(CMD_PLAY, filename.c_str(), 0);
}

#include <driver/adc.h> // Include Legacy ADC driver

// --- Time Speaking Logic ---
enum TimeSpeakState { 
    TIME_IDLE, 
    TIME_INTRO, 
    TIME_HOUR, 
    TIME_UHR, 
    TIME_MINUTE, 
    TIME_DATE_INTRO,
    TIME_WEEKDAY,
    TIME_DAY,
    TIME_MONTH,
    TIME_YEAR,
    TIME_DST,
    TIME_DONE 
};
TimeSpeakState timeState = TIME_IDLE;
int currentHour = 0;
int currentMinute = 0;
// Date Globals
int currentDay = 0;
int currentMonth = 0; 
int currentYear = 0;
int currentWeekday = 0;
bool currentIsDst = false;

void speakTime() {
    Serial.println("Speaking Time...");
    
    TimeManager::DateTime dt = timeManager.getLocalTime();
    
    if (dt.valid) {
        currentHour = dt.hour;
        currentMinute = dt.minute;
        
        // Use rawTime to get full calendar details via standard library
        struct tm * tInfo;
        tInfo = localtime(&dt.rawTime);
        currentDay = tInfo->tm_mday;     // 1-31
        currentMonth = tInfo->tm_mon;    // 0-11
        currentYear = tInfo->tm_year + 1900;
        currentWeekday = tInfo->tm_wday; // 0-6 (Sun-Sat)
        currentIsDst = (tInfo->tm_isdst > 0);
        
    } else {
        Serial.println("Time Invalid (No Sync), using 12:00 default");
        currentHour = 12;
        currentMinute = 0;
        // Defaults if needed (1970)
        currentDay = 1; currentMonth = 0; currentYear = 1970; currentWeekday = 4;
    }

    // Smart Output: Handset if off-hook, Speaker if on-hook
    if (dial.isOffHook()) {
        setAudioOutput(OUT_HANDSET);
    } else {
        setAudioOutput(OUT_SPEAKER);
    }

    timeState = TIME_INTRO;
    String lang = settings.getLanguage();
    String introPath = "/time/" + lang + "/intro.mp3";
    
    // Debug
    Serial.printf("Queueing Time Intro: %s\n", introPath.c_str());
    
    playSound(introPath, (currentOutput == OUT_SPEAKER));
}

void processTimeQueue() {
    if (timeState == TIME_IDLE || timeState == TIME_DONE) return;
    
    Serial.printf("processTimeQueue: State %d\n", timeState);

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
                // If minute is 0, skip to Date
                timeState = TIME_DATE_INTRO;
                nextFile = basePath + "date_intro.mp3";
            } else {
                timeState = TIME_MINUTE;
                if (currentMinute < 10) {
                     nextFile = basePath + "m_0" + String(currentMinute) + ".mp3";
                } else {
                     nextFile = basePath + "m_" + String(currentMinute) + ".mp3";
                }
            }
            break;
            
        case TIME_MINUTE: // Minute finished
            timeState = TIME_DATE_INTRO;
            nextFile = basePath + "date_intro.mp3";
            break;

        // --- NEW DATE LOGIC ---
        case TIME_DATE_INTRO:
            timeState = TIME_WEEKDAY;
            nextFile = basePath + "wday_" + String(currentWeekday) + ".mp3";
            break;

        case TIME_WEEKDAY:
            timeState = TIME_DAY;
            nextFile = basePath + "day_" + String(currentDay) + ".mp3";
            break;

        case TIME_DAY:
            timeState = TIME_MONTH;
            nextFile = basePath + "month_" + String(currentMonth) + ".mp3";
            break;

        case TIME_MONTH:
            timeState = TIME_YEAR;
            nextFile = basePath + "year_" + String(currentYear) + ".mp3";
            break;

        case TIME_YEAR:
            timeState = TIME_DST;
            if (currentIsDst) nextFile = basePath + "dst_summer.mp3";
            else nextFile = basePath + "dst_winter.mp3";
            break;

        case TIME_DST:
            timeState = TIME_DONE;
            Serial.println("Time & Date Speak Done");
            break;
            
        default: break;
    }
    
    if (nextFile.length() > 0) {
        Serial.print("Next Time File: "); Serial.println(nextFile);
        playSound(nextFile, false);
    }
}

void speakCompliment(int number) {
    // Check if AI is configured (Skip for 0 = Random Mix from SD, Skip 5 = Fortune Offline)
    String key = settings.getGeminiKey();
    
    if (number != 0 && number != 5 && key.length() > 5) { // Assuming a valid key
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
            delay(10);
        }
        sendAudioCmd(CMD_STOP, NULL, 0); // Clean break
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
    // 0 = Mix, 1..5 = Specific Categories (5 is Fortune)
    // If number > 5, fallback to Mix (0)
    int category = (number <= 5) ? number : 0;
    
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
    
    // Clear Queue to prevent "ghost words" after alarm stops
    clearSpeechQueue();

    if (audio->isRunning()) audio->stopSong();
    
    timeManager.cancelTimer(); 
    
    setAudioOutput(OUT_HANDSET); 
    
    // BUG FIX: Volume Reset
    // Reset volume to standard level immediately, otherwise next call is loud!
    int defVol = settings.getBaseVolume(); 
    if(audio) {
        audio->setVolume(defVol);
        sendAudioCmd(CMD_VOL, NULL, defVol);
    }

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
    // Vibration Pattern Removed
    
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
    
    // System Codes (Fixed)
    if (numberStr == "90") {
         executePhonebookFunction("TOGGLE_ALARMS", "");
         return;
    }
    if (numberStr == "91") {
         executePhonebookFunction("SKIP_NEXT_ALARM", "");
         return;
    }

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

// Helper to speak Timer/Alarm confirmation
void speakTimerConfirm(int minutes) {
    String lang = settings.getLanguage(); 
    String confirmFile = (lang == "de") ? "/system/timer_confirm_de.mp3" : "/system/timer_confirm_en.mp3";
    String numFile = "/time/" + lang + "/" + String(minutes) + ".mp3";
    
    std::vector<String> seq;
    if(SD.exists(confirmFile)) seq.push_back(confirmFile);
    if(SD.exists(numFile)) seq.push_back(numFile);
    
    playSequence(seq, true); // true = Speaker
}

void speakAlarmConfirm(int h, int m) {
    String lang = settings.getLanguage();
    String confirmFile = (lang == "de") ? "/system/alarm_confirm_de.mp3" : "/system/alarm_confirm_en.mp3";
    
    std::vector<String> seq;
    if(SD.exists(confirmFile)) seq.push_back(confirmFile);
    
    // Hour
    String hFile = "/time/" + lang + "/" + String(h) + ".mp3";
    if (SD.exists(hFile)) seq.push_back(hFile);
    
    // Minute
    // Simple logic: If minute > 0, speak it.
    if (m > 0 || m == 0) { // Always speak minute for alarm? "14 00"? 
        // File "0.mp3" exists? Yes.
        String mFile = "/time/" + lang + "/" + String(m) + ".mp3";
        if (SD.exists(mFile)) seq.push_back(mFile);
    }
    
    playSequence(seq, true); // Speaker
}

void processBufNumber(String numberStr) {
    // Serial.printf("Processing Buffered Input: %s\n", numberStr.c_str());

    // 1. Logic: Setting Alarm Clock (Button Held + Dialing 4 digits)
    if (dial.isButtonDown()) {
        if (numberStr.length() == 4) {
            int h = numberStr.substring(0, 2).toInt();
            int m = numberStr.substring(2, 4).toInt();
            
            if (h >= 0 && h < 24 && m >= 0 && m < 60) {
                timeManager.setAlarm(h, m);
                Serial.printf("ALARM SET TO: %02d:%02d\n", h, m);
                
                // Audio Feedback
                speakAlarmConfirm(h, m);
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
            
            // Confirm via Speaker
            speakTimerConfirm(num);
         } else {
            // Unknown
            Serial.println("Invalid Timer Value");
            playSound("/system/error_tone.wav", false);
         }
    }
}

void onDial(int number) {
    if (isLineBusy) {
        // Serial.println("Ignored Digit (Line Busy)");
        return;
    }

    if (number == 10) number = 0; // Fix 0
    // Serial.printf("Digit Received: %d\n", number);
    
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
    // Serial.printf("Hook State: %s\n", offHook ? "OFF HOOK (Picked Up)" : "ON HOOK (Hung Up)");
    
    // 2. Logic: Delete Alarm (Lift Handset + Hold Button)
    if (offHook && dial.isButtonDown()) {
         if (timeManager.isAlarmSet()) {
             timeManager.deleteAlarm(0); 
             Serial.println("Manual Alarm Deleted (Button + Lift)");
             
             // Feedback on Speaker (Consistent with Timer Cancel)
             String lang = settings.getLanguage();
             playSound("/system/alarm_deleted_" + lang + ".mp3", true); 
         } else {
             // Nothing to delete
             playSound("/system/error_tone.wav", false);
         }
         return;
    }

    if (offHook) {
        // Reset Timeout State
        offHookTime = millis();
        isLineBusy = false;

        // statusLed.setIdle();
        
        // Stop Alarm if ringing
        if (isAlarmRinging) {
            startSnooze(); // Lift = Snooze Start (Put Aside)
            return;
        }
        
        // Check running Timer -> Cancel and Speak on Base
        if (timeManager.isTimerRunning()) {
             timeManager.cancelTimer();
             stopAlarm(); 
             Serial.println("Timer Stopped by Pickup");
             
             setAudioOutput(OUT_SPEAKER);
             String lang = settings.getLanguage();
             playSound("/system/timer_deleted_" + lang + ".mp3", true);
             return; 
        }
        
        // NEW Standard Behavior: Play Dial Tone
        Serial.println("OFF HOOK -> Playing Dial Tone");
        playDialTone();

    } else {
        // ON HOOK (Hung Up)
        
        // BUG FIX: Stop any pending sentences
        clearSpeechQueue();
        
        // Reset Busy State and Stop Loop
        isLineBusy = false;

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
    if (audio->isRunning() || !globalSpeechQueue.empty()) {
        Serial.println("Interruption: Button Pressed -> Stopping Audio & Queue");
        clearSpeechQueue(); 
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



unsigned long lastActivityTime = 0;

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
    if (isDialTonePlaying) return;
    
    // Stop any speech holding the audio
    if (audio->isRunning()) audio->stopSong();

    int toneIndex = settings.getDialTone();
    String dt = getSystemFileByIndex(toneIndex);
    
    // SAFETY CHECK: Dial Tone must not be a system announcement!
    if (dt.indexOf("alarm_active") >= 0 || dt.indexOf("timer_") >= 0 || dt.indexOf("menu_") >= 0) {
        Serial.println("Warning: Invalid DialTone config detected (" + dt + "). Reverting to default.");
        dt = "/system/dialtone_1.wav";
        // Auto-fix settings (Don't reset index blindly, just use safe file temporarily)
        // settings.setDialTone(2); 
    }

    if (dt == "" || !SD.exists(dt)) {
        dt = "/system/dialtone_1.wav";
    }

    // Workaround: Valid file check for current specific SD corruption/format issue
    // Since dialtone_1.wav is reported invalid, we map it to beep.wav (which is known good)
    if (dt == "/system/dialtone_1.wav") {
         Serial.println("Workaround: Remapping potentially broken dialtone_1.wav to beep.wav");
         dt = "/system/beep.wav";
    }

    Serial.println("Starting Dial Tone: " + dt);
    
    // Smart Output: If we are "Off Hook", it's Handset.
    setAudioOutput(OUT_HANDSET);
    playSound(dt, false); // false = Handset
    isDialTonePlaying = true;
    
    // Reset Timeout Timer for Busy Logic
    offHookTime = millis();
}

void stopDialTone() {
    if (isDialTonePlaying) {
        sendAudioCmd(CMD_STOP, NULL, 0);
        isDialTonePlaying = false;
        Serial.println("Dial Tone Stopped.");
    }
}
// --- END HELPER ---

#include <nvs_flash.h> // Ensure we can init NVS

void setup() {
    Serial.begin(CONF_SERIAL_BAUD);
    
    // --- WATCHDOG INIT ---
    // Initialize WDT with timeout and panic (reset) enabled
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT * 1000,
        .idle_core_mask = (1 << 0) | (1 << 1), // Optional: Monitor Idle tasks
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL); // Add current thread (Loop) to WDT
#else
    esp_task_wdt_init(WDT_TIMEOUT, true); // 20s timeout, panic enabling
    esp_task_wdt_add(NULL); // Add current thread (Loop) to WDT
#endif
    
    // Check Wakeup Cause
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("Woke up from Deep Sleep (Hook Lift)!");
    } else {
        Serial.println("Power On / Reset");
    }

    // Init NVS Partition (Vital for Preferences)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("NVS Erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    settings.begin();
    ledManager.begin();
    ledManager.setMode(LedManager::CONNECTING); // Start sequence
    
    // Init LittleFS for Phonebook
    if(!LittleFS.begin(true)){
        Serial.println("LittleFS Mount Failed");
    }

    phonebook.begin(); // Load Phonebook from LittleFS
    timeManager.begin(); // Added TimeManager
    
    // --- Hardware Init ---
    
    // 0. Create SD Mutex (before any SD access)
    sdMutex = xSemaphoreCreateMutex();
    
    // 1. Init Audio Task FIRST (Secure DMA RAM!)
    audioQueue = xQueueCreate(20, sizeof(AudioCmd));
    xTaskCreatePinnedToCore(
        audioTaskCode,   /* Function */
        "AudioTask",     /* Name */
        8192,            /* Stack size (High for I2S/MP3) */
        NULL,            /* Param */
        2,               /* Priority (Higher than Main) */
        NULL,            /* Handle */
        0                /* Core 0 */
    );
    
    // BUG FIX: Wait for Audio Object Creation before consuming more RAM
    Serial.print("Waiting for Audio Engine...");
    unsigned long startWait = millis();
    while (audio == nullptr) {
        delay(10);
        if (millis() - startWait > 3000) {
            Serial.println("ERROR: Audio Task failed to start!");
            break;
        }
    }
    Serial.println("Ready.");

    #if HAS_ES8388_CODEC
        // Enable Power Amplifier (GPIO 21 on Audio Kit v2.2)
        #ifdef CONF_PIN_PA_ENABLE
            pinMode(CONF_PIN_PA_ENABLE, OUTPUT);
            digitalWrite(CONF_PIN_PA_ENABLE, HIGH);
            Serial.printf("PA Enable Pin %d set HIGH\n", CONF_PIN_PA_ENABLE);
        #endif

        Serial.println("Initializing ES8388 Codec...");
        Wire.begin(CONF_I2C_CODEC_SDA, CONF_I2C_CODEC_SCL);
        if(!codec.begin(&Wire)){
            Serial.println("ES8388 Init Failed!");
        } else {
            Serial.println("ES8388 Init Success");
            codec.config(16, currentCodecOutput, ADC_INPUT_LINPUT2_RINPUT2, 50);
        }
    #endif

    // 1. Init SD 
    #ifdef BOARD_AI_THINKER_AUDIO_KIT
        // Correct SPI setup for Audio Kit v2.2 (HSPI)
        // SCK=14, MISO=2, MOSI=15, CS=13
        SPI.begin(14, 2, 15, 13); 
        // Reduced speed (1MHz) for stability
        if(!SD.begin(13, SPI, 1000000)){
    #else
        // Standard VSPI (Lolin D32 Pro default)
        if(!SD.begin(CONF_PIN_SD_CS)){
    #endif
        Serial.println("SD Card Mount Failed (No Card?)");
        ledManager.setMode(LedManager::SOS);
        sdAvailable = false;
    } else {
        Serial.println("SD Card Mounted");
        sdAvailable = true;
        // buildPlaylist(); // MOVED DOWN
    }

    // Init WiFi (after Audio secured DMA)
    webManager.begin();
    
    // NOW Build Playlists (uses remaining RAM/PSRAM)
    if (sdAvailable) {
        buildPlaylist();
    }
    
    // 2. Init Audio Task (Multithreading) - Moved to TOP
    
    // Start with Handset
    setAudioOutput(OUT_HANDSET);

    dial.onDialComplete(onDial);
    dial.onHookChange(onHook);
    dial.onButtonPress(onButton);
    dial.begin();
    
    // Start Web Server Task on Core 1 (separate from loop WDT)
    xTaskCreatePinnedToCore(
        webServerTaskCode,  /* Function */
        "WebServerTask",    /* Name */
        8192,               /* Stack size */
        NULL,               /* Param */
        1,                  /* Priority */
        &webServerTaskHandle, /* Handle */
        1                   /* Core 1 */
    );
    
    Serial.println("Dial-A-Charmer Started (PCM5100A + MAX9814)");
    
    // Initial Beep - Test Speaker Output
    Serial.println("Wait for Hook State to settle...");
    delay(500); // Wait for hook debounce
    Serial.printf("Current Hook State: %s\n", dial.isOffHook() ? "OFF HOOK" : "ON HOOK");

    setAudioOutput(OUT_SPEAKER);
    playSound("/system/beep.wav", true); // Speaker
    Serial.println("Initial Beep played on SPEAKER (Output 2)");

    // Start Background Scan for updated Persona Names
    startPersonaScan();
}

void loop() {
    // Reset Watchdog
    esp_task_wdt_reset();

    // Verify Hook State Log
    static bool lastHookState = false; 
    bool actualHook = dial.isOffHook();
    if(actualHook != lastHookState) {
        lastHookState = actualHook;
        Serial.printf("[DEBUG] Hook State Changed: %s\n", actualHook ? "OFF HOOK (Handset Lifted)" : "ON HOOK (Hung Up)");
    }

    dial.loop();
    
    // --- TIMEOUT / BUSY LOGIC ---
    if (dial.isOffHook()) {
        if (!isLineBusy) {
             // Reset Timer Conditions
             bool active = false;
             // 1. Dialing buffer not empty (User started dialing)
             if (dialBuffer.length() > 0) active = true;
             // 2. Audio playing (EXCEPT Dial Tone) - e.g. Compliment, Menu, Alarm
             if (audio && audio->isRunning() && !isDialTonePlaying) active = true;
             // 3. Snooze Exception (Phone silent while snoozing)
             if (timeManager.isSnoozeActive()) active = true;

             if (active) {
                 offHookTime = millis();
             } else {
                 // Idle (or Dial Tone playing)
                 if (millis() - offHookTime > OFF_HOOK_TIMEOUT) {
                     Serial.println("[Timeout] Off-Hook Limit Reached -> Busy Line");
                     isLineBusy = true;
                     stopDialTone(); 
                     // Play Busy Signal Loop
                     setAudioOutput(OUT_HANDSET);
                     playSound("/system/busy_tone.wav", false);
                 }
             }
        }
    }

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
    handlePersonaScan(); // Run BG Task (now mutex-guarded)
    
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
    
    // --- AP Mode Long Press (10s) ---
    static unsigned long btnPressStart = 0;
    static bool buttonActionTriggered = false;

    if (dial.isButtonDown()) {
        if (btnPressStart == 0) { 
            btnPressStart = millis(); 
            buttonActionTriggered = false; 
        }
        
        unsigned long dur = millis() - btnPressStart;
        
        // 10s Action: AP Mode
        if (dur > 10000 && !buttonActionTriggered) {
             Serial.println("Long Press Detected: Starting AP Mode");
             playSound("/system/beep.wav", false);
             webManager.startAp();
             buttonActionTriggered = true; 
             // Wait for release
             while(dial.isButtonDown()) { dial.loop(); delay(10); }
        }
    } else {
        btnPressStart = 0;
        buttonActionTriggered = false;
    }
    
    // --- SMART DEEP SLEEP LOGIC ---
    // Prevent sleep if: Off Hook (Active), Audio Playing, Timer Running, or USB Power Detected
    bool systemBusy = dial.isOffHook() || audio->isRunning() || timeManager.isTimerRunning() || isAlarmRinging || timeManager.isSnoozeActive() || isUsbPowerConnected();
    
    if (systemBusy) {
        lastActivityTime = millis();
    } else {
        if (millis() - lastActivityTime > CONF_SLEEP_TIMEOUT_MS) {
            Serial.println("zzZ Preparing for Deep Sleep zzZ");
            
            // 1. Calculate Wakeup Triggers
            
            // A) WAKE ON HOOK (User Action)
            // Check current state to wake on change/toggle
            int currentHookState = digitalRead(CONF_PIN_HOOK);
            esp_sleep_enable_ext0_wakeup((gpio_num_t)CONF_PIN_HOOK, !currentHookState);
            Serial.printf(" - Wakeup configured on PIN %d (Log: %d)\n", CONF_PIN_HOOK, !currentHookState);

            // B) WAKE ON NEXT ALARM (Timer)
            long secondsToAlarm = timeManager.getSecondsToNextAlarm();
            if (secondsToAlarm > 0) {
                // Wakeup exactly at alarm time (conversion to microseconds)
                uint64_t sleepUs = (uint64_t)secondsToAlarm * 1000000ULL;
                esp_sleep_enable_timer_wakeup(sleepUs);
                
                // Info logging
                int hours = secondsToAlarm / 3600;
                int mins = (secondsToAlarm % 3600) / 60;
                Serial.printf(" - Wakeup Timer set for %ld s (~%dh %dm until Alarm)\n", secondsToAlarm, hours, mins);
            } else {
                Serial.println(" - No upcoming alarms set. Sleeping indefinitely until Hook Lift.");
            }
            
            // 2. Status LED Off
            ledManager.setMode(LedManager::OFF);
            ledManager.update(); // Flush change
            delay(100); // Give Serial time to flush
            
            // 3. Goodnight
            esp_deep_sleep_start();
        }
    }
}

// --- Audio Queue System (Non-Blocking) ---

void clearSpeechQueue() {
    if (!globalSpeechQueue.empty()) {
        Serial.println("Clearing Speech Queue...");
        globalSpeechQueue.clear();
    }
}

void playSequence(std::vector<String> files, bool useSpeaker) {
    if (files.empty()) return;
    
    globalSpeechQueue = files;
    globalSpeechQueueUseSpeaker = useSpeaker;
    
    // Play First Item
    String first = globalSpeechQueue[0];
    globalSpeechQueue.erase(globalSpeechQueue.begin());
    
    playSound(first, useSpeaker);
}

// Audio Events
void audio_eof_mp3(const char *info){
    // Serial.print("EOF: "); Serial.println(info);
    
    // 1. Handle Speech Queue (Priority)
    if (!globalSpeechQueue.empty()) {
        String next = globalSpeechQueue[0];
        globalSpeechQueue.erase(globalSpeechQueue.begin());
        // Small delay to separate words naturally? 
        // We can't delay here in callback (might block decoder task if callback is from there? No, usually separate).
        // If PlaySound sends queue message, it's instant.
        playSound(next, globalSpeechQueueUseSpeaker);
        return;
    }
    
    // 2. Handle Time Speaking Queue (Legacy State Machine)
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
