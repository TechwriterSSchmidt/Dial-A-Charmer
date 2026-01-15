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
bool alarmActive = false; // Used for external alarms?
bool timerRunning = false;
bool isAlarmRinging = false;
unsigned long alarmRingingStartTime = 0;
int currentAlarmVol = 0;

// --- Playlist Management ---
struct Playlist {
    std::vector<String> tracks;
    int index = 0;
};

std::map<int, Playlist> categories; // 1=Trump, 2=Badran, 3=Yoda, 4=Neutral
Playlist mainPlaylist; // For Dial 0

// --- Persistence Helpers ---
String getStoredPlaylistPath(int category) {
    return "/playlists/cat_" + String(category) + ".m3u";
}

String getStoredIndexPath(int category) {
    return "/playlists/cat_" + String(category) + ".idx";
}

void ensurePlaylistDir() {
    if (!SD.exists("/playlists")) SD.mkdir("/playlists");
}

void savePlaylistToSD(int category, Playlist &pl) {
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
                // Construct full path
                String fullPath = path;
                if(!fullPath.endsWith("/")) fullPath += "/";
                if(fname.startsWith("/")) fname = fname.substring(1); 
                fullPath += fname;
                
                // Add to specific category ONLY
                categories[categoryId].tracks.push_back(fullPath);
            }
        }
        file = dir.openNextFile();
    }
}

void buildPlaylist() {
    categories.clear();
    mainPlaylist.tracks.clear();
    mainPlaylist.index = 0;
    
    Serial.println("Initializing Playlists...");
    
    // Categories 1-4
    for (int i = 1; i <= 4; i++) {
        String subfolder = "mp3_group_0" + String(i);
        
        bool loaded = loadPlaylistFromSD(i, categories[i]);
        if (!loaded || categories[i].tracks.empty()) {
            Serial.printf("Building Cat %d (scanning)...\n", i);
            categories[i].tracks.clear();
            categories[i].index = 0;
            scanDirectoryToPlaylist("/compliments/" + subfolder, i);
            
            // Shuffle
            auto rng = std::default_random_engine(esp_random());
            std::shuffle(categories[i].tracks.begin(), categories[i].tracks.end(), rng);
            
            savePlaylistToSD(i, categories[i]);
            saveProgressToSD(i, 0);
        } else {
            Serial.printf("Loaded Cat %d from SD (Tracks: %d, Idx: %d)\n", i, categories[i].tracks.size(), categories[i].index);
        }
    }
    
    // Main Playlist (0)
    bool loadedMain = loadPlaylistFromSD(0, mainPlaylist);
    if (!loadedMain || mainPlaylist.tracks.empty()) {
        Serial.println("Building Main Playlist...");
        mainPlaylist.tracks.clear();
        mainPlaylist.index = 0;
        
        // Aggregate
        for (int i = 1; i <= 4; i++) {
            for (const auto &t : categories[i].tracks) mainPlaylist.tracks.push_back(t);
        }
        
        auto rng = std::default_random_engine(esp_random());
        std::shuffle(mainPlaylist.tracks.begin(), mainPlaylist.tracks.end(), rng);
        
        savePlaylistToSD(0, mainPlaylist);
        saveProgressToSD(0, 0);
    } else {
        Serial.printf("Loaded Main Playlist from SD (Tracks: %d, Idx: %d)\n", mainPlaylist.tracks.size(), mainPlaylist.index);
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
        // Fallback to Main if specific empty
        if (category != 0 && !mainPlaylist.tracks.empty()) {
             target = &mainPlaylist; 
             category = 0; 
        } else {
             return "";
        }
    }
    
    // Check index
    if ((size_t)target->index >= target->tracks.size()) {
        Serial.printf("Playlist %d finished! Reshuffling...\n", category);
        std::shuffle(target->tracks.begin(), target->tracks.end(), std::default_random_engine(esp_random()));
        target->index = 0;
        
        savePlaylistToSD(category, *target);
        // Progress saved below after increment
    }
    
    String track = target->tracks[target->index];
    
    // Increment and Save Progress
    target->index++; 
    saveProgressToSD(category, target->index);
    
    return track;
}


// --- Helper Functions ---
enum AudioOutput { OUT_HANDSET, OUT_SPEAKER };
AudioOutput currentOutput = OUT_HANDSET;

void setAudioOutput(AudioOutput target) {
    // Check if we need to switch
    if (currentOutput == target) return; 
    
    // Stop ensuring clean switch
    if(audio.isRunning()) audio.stopSong();
    
    if (target == OUT_HANDSET) {
        audio.setPinout(CONF_I2S_BCLK, CONF_I2S_LRC, CONF_I2S_DOUT);
        int vol = map(settings.getVolume(), 0, 42, 0, 21);
        audio.setVolume(vol);
        Serial.println("Output: Handset");
    } else {
        audio.setPinout(CONF_I2S_SPK_BCLK, CONF_I2S_SPK_LRC, CONF_I2S_SPK_DOUT);
        int vol = map(settings.getBaseVolume(), 0, 42, 0, 21);
        audio.setVolume(vol);
        Serial.println("Output: Speaker");
    }
    currentOutput = target;
}

void playSound(String filename, bool useSpeaker = false) {
    setAudioOutput(useSpeaker ? OUT_SPEAKER : OUT_HANDSET);

    if (SD.exists(filename)) {
        statusLed.setTalking();
        audio.connecttofs(SD, filename.c_str());
    } else {
        Serial.print("File missing: ");
        Serial.println(filename);
        statusLed.setWarning();
    }
}

// --- Time Speaking Logic ---
enum TimeSpeakState { TIME_IDLE, TIME_INTRO, TIME_HOUR, TIME_UHR, TIME_MINUTE, TIME_DONE };
TimeSpeakState timeState = TIME_IDLE;
int currentHour = 0;
int currentMinute = 0;

void speakTime() {
    Serial.println("Speaking Time...");
    
    // Get time from GPS or fallback
    // Note: TinyGPS++ gps variable is available
    if (gps.time.isValid()) {
        currentHour = gps.time.hour() + settings.getTimezoneOffset(); // Basic offset logic
        if (currentHour >= 24) currentHour -= 24;
        currentMinute = gps.time.minute();
    } else {
        // Mock time if no GPS for testing? Or maybe fail silently?
        // Let's assume 12:00 for demo if valid fails
        Serial.println("GPS Time Invalid, using mock 12:00");
        currentHour = 12;
        currentMinute = 0;
    }

    setAudioOutput(OUT_HANDSET);
    timeState = TIME_INTRO;
    playSound("/time/intro.wav", false);
}

void processTimeQueue() {
    if (timeState == TIME_IDLE || timeState == TIME_DONE) return;

    // This function is called from audio_eof_mp3 to trigger next file
    String nextFile = "";

    switch (timeState) {
        case TIME_INTRO: // Intro finished, play Hour
            timeState = TIME_HOUR;
            nextFile = "/time/h_" + String(currentHour) + ".wav";
            break;
            
        case TIME_HOUR: // Hour finished, play "Uhr"
            timeState = TIME_UHR;
            nextFile = "/time/uhr.wav";
            break;
            
        case TIME_UHR: // "Uhr" finished, play Minute (if not 0)
            if (currentMinute == 0) {
                timeState = TIME_DONE;
                Serial.println("Time Speak Done (Exact Hour)");
            } else {
                timeState = TIME_MINUTE;
                if (currentMinute < 10) {
                     // Play "null five" format? 
                     nextFile = "/time/m_0" + String(currentMinute) + ".wav";
                } else {
                     nextFile = "/time/m_" + String(currentMinute) + ".wav";
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
        statusLed.setWifiConnecting(); // Yellow = Thinking
        Serial.println("AI Mode Active");
        
        // 1. Get Text from Gemini
        String text = ai.getCompliment(number);
        
        if (text.length() > 0) {
            // 2. Get TTS URL
            String ttsUrl = ai.getTTSUrl(text);
            
            // 3. Play
            Serial.println("Streaming TTS...");
            setAudioOutput(OUT_HANDSET);
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
         path = "/system/error_tone.wav"; 
    }
    
    Serial.printf("Playing compliment (Cat: %d): %s\n", category, path.c_str());
    playSound(path, false);
}

void startAlarm() {
    isAlarmRinging = true;
    alarmRingingStartTime = millis();
    currentAlarmVol = 5; // Start quiet
    Serial.println("Starting Alarm Sequence (Ascending + Vibe)");
    
    setAudioOutput(OUT_SPEAKER);
    // Explicitly set low volume initially (bypass stored setting for now)
    audio.setVolume(map(currentAlarmVol, 0, 42, 0, 21)); 
    
    // Start Sound
    playSound("/ringtones/" + String(settings.getRingtone()) + ".wav", true);
}

void stopAlarm() {
    if (!isAlarmRinging) return;
    
    isAlarmRinging = false;
    digitalWrite(CONF_PIN_VIB_MOTOR, LOW); // Ensure Motor Off
    if (audio.isRunning()) audio.stopSong();
    
    // Restore Volumes to settings
    setAudioOutput(OUT_HANDSET); // Reset default state
    // Vol will be restored by next setAudioOutput call or manual
    Serial.println("Alarm Stopped");
}

void handleAlarmLogic() {
    if (!isAlarmRinging) return;
    
    unsigned long duration = millis() - alarmRingingStartTime;
    
    // 1. Vibration Pattern (Pulse: 500ms ON, 500ms OFF)
    bool vibOn = (duration % 1000) < 500;
    digitalWrite(CONF_PIN_VIB_MOTOR, vibOn ? HIGH : LOW);
    
    // 2. Volume Ramp (Increase every 3 seconds)
    int maxVol = settings.getBaseVolume();
    int rampStep = duration / 3000; 
    int newVol = 5 + rampStep;
    
    if (newVol > maxVol) newVol = maxVol;
    
    // Only update if changed
    if (newVol != currentAlarmVol) {
        currentAlarmVol = newVol;
        // Map 0-42 -> 0-21
        audio.setVolume(map(currentAlarmVol, 0, 42, 0, 21));
        Serial.printf("Alarm Vol Ramp: %d/%d\n", currentAlarmVol, maxVol);
    }
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
            playSound("/ringtones/" + String(number) + ".wav", true);
            return;
        }
    }

    if (dial.isOffHook()) {
        // Dialing while off-hook -> Interrupt and play new selection
        speakCompliment(number);
    } else {
        Serial.printf("Setting Timer for %d minutes\n", number);
        alarmEndTime = millis() + (number * 60000);
        timerRunning = true;
        statusLed.setWifiConnecting(); 
        playSound("/system/beep.wav", true); // Confirmation
    }
}

void onHook(bool offHook) {
    Serial.printf("Hook State: %s\n", offHook ? "OFF HOOK (Picked Up)" : "ON HOOK (Hung Up)");
    
    if (offHook) {
        statusLed.setIdle();
        
        // Stop Alarm if ringing
        if (isAlarmRinging || timerRunning) {
            stopAlarm();
            timerRunning = false;
            Serial.println("Alarm/Timer Stopped by Pickup");
        } else {
            // Automatic Surprise Mix on Pickup
            Serial.println("Auto-Start: Random Surprise Mix");
            speakCompliment(0); 
        }
    } else {
        if (audio.isRunning()) {
            audio.stopSong();
            statusLed.setIdle();
        }
    }
}

void onButton() {
    Serial.println("Extra Button Pressed: Speak Time");
    speakTime();
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
    setAudioOutput(OUT_HANDSET);
    
    dial.onDialComplete(onDial);
    dial.onHookChange(onHook);
    dial.onButtonPress(onButton);
    dial.begin();
    
    webManager.begin();
    
    Serial2.begin(CONF_GPS_BAUD, SERIAL_8N1, CONF_GPS_RX, CONF_GPS_TX);
    
    Serial.println("Dial-A-Charmer Started");
    
    // Init Motor
    pinMode(CONF_PIN_VIB_MOTOR, OUTPUT);
    digitalWrite(CONF_PIN_VIB_MOTOR, LOW);
    
    playSound("/system/beep.wav", true); // Speaker
}

void loop() {
    audio.loop();
    webManager.loop();
    dial.loop();
    statusLed.loop();
    
    if (isAlarmRinging) {
        handleAlarmLogic();
    }
    
    while (Serial2.available() > 0) {
        gps.encode(Serial2.read());
    }
    
    if (timerRunning && millis() > alarmEndTime) {
        timerRunning = false;
        startAlarm();
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

    statusLed.setIdle();
    
    if (isAlarmRinging) {
         // Loop Alarm Sound
         playSound("/ringtones/" + String(settings.getRingtone()) + ".wav", true);
         
         // Note: setVolume might be reset by playSound internal logic if we called setAudioOutput again?
         // Our modified playSound calls setAudioOutput. 
         // setAudioOutput resets volume to settings.getBaseVolume().
         // FIXME: We need to force current ramp volume back.
         audio.setVolume(map(currentAlarmVol, 0, 42, 0, 21));
    }
}

// Called when stream (TTS) finishes
void audio_eof_stream(const char *info){
    Serial.print("EOF Stream: "); Serial.println(info);
    statusLed.setIdle();
}
