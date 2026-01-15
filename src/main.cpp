#include <Arduino.h>
#include <TinyGPS++.h>
#include <Audio.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>

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
    String path = "/compliments/" + String(number) + ".mp3";
    if (number == 0) path = "/compliments/random.mp3"; 
    
    Serial.print("Playing compliment (Fallback): ");
    Serial.println(path);
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
