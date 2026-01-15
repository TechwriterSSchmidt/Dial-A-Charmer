#include <Arduino.h>
#include <TinyGPS++.h>
#include <Audio.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>

#include "Settings.h"
#include "WebManager.h"
#include "RotaryDial.h"
#include "LedStatus.h"

// --- Configuration ---
// Audio (I2S) - MAX98357A / PCM5102
#define I2S_BCLK 26
#define I2S_LRC  25
#define I2S_DOUT 22

// GPS (Serial2)
#define GPS_RX 16
#define GPS_TX 17
#define GPS_BAUD 9600

// Pins
#define DIAL_PULSE_PIN 34
#define HOOK_SWITCH_PIN 32
#define EXTRA_BTN_PIN 33
// LED_PIN is 13 (in LedStatus.h)

// --- Objects ---
TinyGPSPlus gps;
Audio audio;
RotaryDial dial(DIAL_PULSE_PIN, HOOK_SWITCH_PIN, EXTRA_BTN_PIN);

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
    // Logic to construct sentence: "Es ist [Stunde] Uhr [Minuten]"
    // Requires mp3 files for numbers 0-59, "uhr.mp3", etc.
    // Placeholder:
    Serial.print("Speaking Time...");
    playSound("/time_intro.mp3"); 
}

void speakCompliment(int number) {
    // Determine category or specific file based on number
    String path = "/compliments/" + String(number) + ".mp3";
    if (number == 0) path = "/compliments/random.mp3"; 
    
    Serial.print("Playing compliment: ");
    Serial.println(path);
    playSound(path);
}

// --- Callbacks ---
void onDial(int number) {
    Serial.printf("Dialed: %d\n", number);
    
    // Logic depends on Hook State
    if (dial.isOffHook()) {
        // Active Mode
        if (number == 0) {
            speakTime();
        } else {
            speakCompliment(number);
        }
    } else {
        // Idle/Settings Mode (Receiver Down)
        // Dialing here sets Timer
        Serial.printf("Setting Timer for %d minutes\n", number);
        alarmEndTime = millis() + (number * 60000);
        timerRunning = true;
        statusLed.setWifiConnecting(); // Yellow blink for timer set?
        // Maybe play a short "Timer Set" beep
        playSound("/timer_set.mp3");
    }
}

void onHook(bool offHook) {
    Serial.printf("Hook State: %s\n", offHook ? "OFF HOOK (Picked Up)" : "ON HOOK (Hung Up)");
    
    if (offHook) {
        // Picked up
        statusLed.setIdle(); // Or ready light
        
        // If alarm/timer is ringing, stop it
        if (timerSoundPlaying || alarmActive) {
            if (audio.isRunning()) audio.stopSong();
            timerSoundPlaying = false;
            alarmActive = false;
            timerRunning = false;
            Serial.println("Alarm/Timer Stopped by Pickup");
        } else {
            // Normal pickup
            playSound("/dial_tone.mp3"); // Optional: Fake dial tone
        }
        
    } else {
        // Hung up
        // Stop any current playback
        if (audio.isRunning()) {
            audio.stopSong();
            statusLed.setIdle();
        }
    }
}

void onButton() {
    Serial.println("Extra Button Pressed");
    // Toggle Web/AP Mode? Or Repeat?
    // Current requirement: "Wecker und Timer ausschalten könnte man über den Gabeltaster"
    // So this button is free. Maybe Force Time Sync?
    
    // For now: Announce IP Address
    playSound("/ip_announce_start.mp3");
    // Logic to speak IP digits would go here
}

void setup() {
    Serial.begin(115200);
    settings.begin();
    statusLed.begin();
    
    // Init SD (Lolin D32 Pro default pins are VSPI: MOSI 23, MISO 19, CLK 18, CS 4)
    if(!SD.begin(4)){
        Serial.println("SD Card Mount Failed");
        statusLed.setWarning();
    } else {
        Serial.println("SD Card Mounted");
    }
    
    // Init Audio
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(15);
    
    // Init Dial
    dial.onDialComplete(onDial);
    dial.onHookChange(onHook);
    dial.onButtonPress(onButton);
    dial.begin();
    
    // Init Web
    webManager.begin();
    
    // Init GPS
    Serial2.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
    
    Serial.println("Atomic Charmer Started");
    playSound("/startup.mp3");
}

void loop() {
    audio.loop();
    webManager.loop();
    dial.loop();
    statusLed.loop(); // For LED animations
    
    // GPS
    while (Serial2.available() > 0) {
        gps.encode(Serial2.read());
    }
    
    // Timer Check
    if (timerRunning && millis() > alarmEndTime) {
        timerRunning = false;
        timerSoundPlaying = true;
        Serial.println("Timer Finished! Ringing...");
        playSound("/alarm_ring.mp3");
    }
}

// Audio Events
void audio_eof_mp3(const char *info){
    Serial.print("EOF: "); Serial.println(info);
    statusLed.setIdle();
    
    // If it was the alarm ringing, loop it!
    if (timerSoundPlaying) {
        audio.connecttofs(SD, "/alarm_ring.mp3");
    }
}
