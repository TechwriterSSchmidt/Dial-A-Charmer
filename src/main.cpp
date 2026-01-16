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
#include "LedManager.h"
#include "Es8311Driver.h" // New Audio Codec Driver
#include "AiManager.h"    // New AI Manager

// --- Objects ---
TinyGPSPlus gps;
Audio audio;
RotaryDial dial(CONF_PIN_DIAL_PULSE, CONF_PIN_HOOK, CONF_PIN_EXTRA_BTN);
LedManager ledManager(CONF_PIN_LED);

// Global flag for hardware availability
bool sdAvailable = false;
bool audioCodecAvailable = false;

// --- Globals ---
unsigned long alarmEndTime = 0;
bool timerRunning = false;
bool isAlarmRinging = false;
unsigned long alarmRingingStartTime = 0;
int currentAlarmVol = 0;

// Alarm Clock
int alarmHour = -1;   // -1 = Disabled
int alarmMinute = -1;
bool snoozeActive = false;
unsigned long snoozeEndTime = 0;
const int SNOOZE_DURATION_MS = 9 * 60 * 1000; // 9 Minutes
std::vector<int> dialBuffer;
unsigned long lastDialTime = 0;

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
        int vol = ::map(settings.getVolume(), 0, 42, 0, 21);
        audio.setVolume(vol);
        Serial.println("Output: Handset");
    } else {
        audio.setPinout(CONF_I2S_SPK_BCLK, CONF_I2S_SPK_LRC, CONF_I2S_SPK_DOUT);
        int vol = ::map(settings.getBaseVolume(), 0, 42, 0, 21);
        audio.setVolume(vol);
        Serial.println("Output: Speaker");
    }
    currentOutput = target;
}

void playSound(String filename, bool useSpeaker = false) {
    if (!sdAvailable) {
        Serial.println("SD Not Available, cannot play: " + filename);
        return;
    }
    setAudioOutput(useSpeaker ? OUT_SPEAKER : OUT_HANDSET);

    if (SD.exists(filename)) {
        statusLed.setTalking();
        audio.connecttoFS(SD, filename.c_str());
    } else {
        Serial.print("File missing: ");
        Serial.println(filename);
        statusLed.setWarning();
    }
}

#include <driver/adc.h> // Include Legacy ADC driver

// Check Battery Voltage (Lolin D32 Pro: Divider 100k/100k on IO35)
bool checkBattery() {
    // ADC: CONFLICT Fix
    // Instead of analogRead(35) which uses the NEW driver (simultaneously with WiFi Legacy driver),
    // we use the Legacy driver directly.
    // GPIO 35 is ADC1_CHANNEL_7.
    
    // Ensure width is configured (once, or every time?)
    // Note: It's safe to call multiple times.
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11); // 11dB for 3.3V range

    int raw = adc1_get_raw(ADC1_CHANNEL_7);
    
    // Correction logic inspired by renatobo/bonogps for Lolin D32 Pro
    // They associate raw 4096 with ~7.445V at the battery (post divider x2 logic implicit in factor)
    // Formula: (raw / 4096.0) * 7.445
    float voltage = (float)raw / 4096.0 * 7.445; 
    
    Serial.printf("Battery: %.2f V (Raw: %d)\n", voltage, raw);
    
    if (voltage < 3.3 && voltage > 2.0) { // Ignore 0.0 (USB powered/no bat)
        return false; // Low Battery
    }
    return true; // OK
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
        // statusLed handled by loop/state
        Serial.println("AI Mode Active");
        
        // --- Retro Thinking Sound ---
        // Play sound BEFORE request to bridge the gap.
        // Needs a loop to play out, as getCompliment blocks.
        Serial.println("Playing Thinking Sound...");
        playSound("/system/computing.wav", false); // Assuming file exists
        unsigned long startThink = millis();
        // Play for max 3 seconds or until file ends
        while (audio.isRunning() && (millis() - startThink < 3000)) {
            audio.loop(); 
            delay(1);
        }
        audio.stopSong(); // Clean break
        // ----------------------------
        
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
    audio.setVolume(::map(currentAlarmVol, 0, 42, 0, 21)); 
    
    // Start Sound
    playSound("/ringtones/" + String(settings.getRingtone()) + ".wav", true);
}

void stopAlarm() {
    isAlarmRinging = false;
    digitalWrite(CONF_PIN_VIB_MOTOR, LOW); // Ensure Motor Off
    if (audio.isRunning()) audio.stopSong();
    
    // Restore Volumes to settings
    setAudioOutput(OUT_HANDSET); // Reset default state
    // Vol will be restored by next setAudioOutput call or manual
    Serial.println("Alarm Stopped");
}

void startSnooze() {
    stopAlarm();
    snoozeActive = true;
    snoozeEndTime = millis() + SNOOZE_DURATION_MS;
    Serial.printf("Snooze Started. Resuming in %d min.\n", SNOOZE_DURATION_MS / 60000);
    // Optional: Play a short confirmation
    setAudioOutput(OUT_HANDSET); // Snooze confirmation in handset?
    // playSound("/system/beep.wav", false);
}

void cancelSnooze() {
    if (snoozeActive) {
        snoozeActive = false;
        Serial.println("Snooze Cancelled.");
    }
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
        audio.setVolume(::map(currentAlarmVol, 0, 42, 0, 21));
        Serial.printf("Alarm Vol Ramp: %d/%d\n", currentAlarmVol, maxVol);
    }
}

// --- Callbacks ---
void onDial(int number) {
    Serial.printf("Dialed: %d\n", number);
    
    // 1. Logic: Setting Alarm Clock (Button Held + Dialing 4 digits)
    if (dial.isButtonDown()) {
        // Clear buffer if stale (> 5 seconds since last digit)
        if (millis() - lastDialTime > 5000) {
             dialBuffer.clear();
        }
        lastDialTime = millis();
        
        dialBuffer.push_back(number == 10 ? 0 : number); // 10 pulses usually means 0
        Serial.printf("Alarm Buffer: %d digits\n", dialBuffer.size());
        
        // Ack beep
        setAudioOutput(OUT_HANDSET);
        playSound("/system/beep.wav", false); 
        
        if (dialBuffer.size() == 4) {
            int h = dialBuffer[0] * 10 + dialBuffer[1];
            int m = dialBuffer[2] * 10 + dialBuffer[3];
            
            if (h >= 0 && h < 24 && m >= 0 && m < 60) {
                alarmHour = h;
                alarmMinute = m;
                Serial.printf("Alarm Set: %02d:%02d\n", alarmHour, alarmMinute);
                // Confirmation Sound "Alarm Set"
                 playSound("/system/timer_set.mp3", false); // reusing timer sound or need specific?
            } else {
                Serial.println("Invalid Time Dialed");
                playSound("/system/error_tone.wav", false);
            }
            dialBuffer.clear();
        }
        return;
    }
    
    // Check for Ringtone Setting Mode (Button NOT Held, Logic Removed/Changed?)
    // Original logic: "if (dial.isButtonDown())" -> this block is now taken by Alarm Set
    // User Guide says: "Press Button enabled alarm clock mode. In this mode, dialing 0715..."
    // So the previous logic for Ringtone setting needs to be moved or removed?
    // README doesn't mention setting ringtone via button+dial anymore.
    // It says "Input numbers to request content" in Active Mode. 
    
    // Wait, original read_file showed:
    // if (dial.isButtonDown()) { if (number >= 1 && number <= 5) ... setRingtone ... }
    // This conflicts. I will overwrite it with the new Alarm Logic requested.
    
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
    
    // 2. Logic: Delete Alarm (Button + Lift)
    if (offHook && dial.isButtonDown()) {
        alarmHour = -1;
        alarmMinute = -1;
        Serial.println("Alarm Deleted/Disabled");
        // Audio Feedback
        setAudioOutput(OUT_HANDSET);
        playSound("/system/error_tone.wav", false); // "Deleted" tone
        return;
    }

    if (offHook) {
        statusLed.setIdle();
        
        // Stop Alarm if ringing
        if (isAlarmRinging) {
            startSnooze(); // Lift = Snooze Start (Put Aside)
            return;
        }
        
        if (timerRunning) {
             timerRunning = false;
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
        
        // Automatic Surprise Mix on Pickup
        Serial.println("Auto-Start: Random Surprise Mix");
        speakCompliment(0); 
    } else {
        // ON HOOK (Hung Up)
        
        if (snoozeActive) {
            // User hung up during snooze -> Cancel Snooze (I am awake)
            cancelSnooze();
        }
        
        if (audio.isRunning()) {
            audio.stopSong();
            statusLed.setIdle();
        }
    }
}

void onButton() {
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
    // Heuristic for Lolin D32 Pro via Battery Divider (35)
    // Legacy ADC driver is used in checkBattery()
    // 4.2V = ~2310 raw. 
    // < 0.1V (Raw < 50) -> No Battery -> USB Power
    // > 4.3V (Raw > 2400) -> Overcharged/USB Direct?
    
    int raw = adc1_get_raw(ADC1_CHANNEL_7);
    if (raw < 50 || raw > 2400) return true; 
    return false;
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
    ledManager.setMode(LedManager::BREATHE_SLOW); // Start sequence
    
    // Init Audio Codec (ES8311)
    if (!audioCodec.begin()) {
        Serial.println("ES8311 Init Failed! (No I2C device?)");
        ledManager.setMode(LedManager::SOS);
        audioCodecAvailable = false;
    } else {
        Serial.println("ES8311 Initialized");
        audioCodecAvailable = true;
        audioCodec.setVolume(50); // Set Hardware Volume to 50%
        // Adjust software volume in Audio lib if needed
    }
    
    // Init SD 
    if(!SD.begin(CONF_PIN_SD_CS)){
        Serial.println("SD Card Mount Failed (No Card?)");
        ledManager.setMode(LedManager::SOS);
        sdAvailable = false;
    } else {
        Serial.println("SD Card Mounted");
        sdAvailable = true;
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
    
    // If we woke from Deep Sleep, the GPS is likely in Backup Mode. Wake it up!
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        wakeGps();
    }
    
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
    
    // Pass hour for adaptive brightness
    int h = -1;
    // TinyGPS++ gps is available globally in this file
    if (gps.time.isValid()) {
         h = gps.time.hour() + settings.getTimezoneOffset();
         if (h >= 24) h -= 24;
         if (h < 0) h += 24;
    }
    
    // Check Alarm
    if (alarmHour >= 0 && !isAlarmRinging && !snoozeActive && gps.time.isValid()) {
        int m = gps.time.minute();
        // Simple One-Shot trigger: matches exactly and seconds < 2 to avoid multi-trigger?
        // Or flag logic.
        static int lastTriggerMinute = -1;
        
        if (alarmHour == h && alarmMinute == m && lastTriggerMinute != m) {
            Serial.println("Alarm Time Reached!");
            startAlarm();
            lastTriggerMinute = m;
        }
    }
    
    // Check Snooze End
    if (snoozeActive && millis() > snoozeEndTime) {
        Serial.println("Snooze Ended! Wakey Wakey!");
        cancelSnooze();
        startAlarm();
    }
    
    // Serial.println("L: LED");
    // statusLed.loop(h);
    ledManager.update();
    
    // LED State Logic
    if (isAlarmRinging) {
        ledManager.setMode(LedManager::BREATHE_FAST);
    } 
    else if (snoozeActive) {
        ledManager.setMode(LedManager::BREATHE_SLOW);
    }
    else if (!sdAvailable || !audioCodecAvailable) {
         ledManager.setMode(LedManager::SOS);
    }
    else if (WiFi.status() == WL_CONNECTED) {
        ledManager.setMode(LedManager::IDLE_GLOW);
    }
    else {
         // Not Connected -> Search Mode
         ledManager.setMode(LedManager::BREATHE_SLOW);
    }
    
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
    
    // --- SMART DEEP SLEEP LOGIC ---
    // Prevent sleep if: Off Hook (Active), Audio Playing, Timer Running, or USB Power Detected
    bool systemBusy = dial.isOffHook() || audio.isRunning() || timerRunning || isAlarmRinging || isUsbPowerConnected();
    
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
         audio.setVolume(::map(currentAlarmVol, 0, 42, 0, 21));
    }
}

// Called when stream (TTS) finishes
void audio_eof_stream(const char *info){
    Serial.print("EOF Stream: "); Serial.println(info);
    // statusLed.setIdle();
}
