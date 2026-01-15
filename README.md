# The-Atomic-Charmer 

The Atomic Charmer is a vintage telephone brought back to life with more personality than ever. It announces GNSS-precise time with the confidence of someone whos never been late, doubles as an alarm and timer, and delivers compliments on demandbecause who wouldnt want a phone that cheers them on? Created for my mentor Sandra, whose nerdiness and warmth outshine even the fanciest electronics.

## Table of Contents
- [Hardware Components](#hardware-components)
- [Features](#features)
- [User Interface & Control](#user-interface--control)
- [Documentation & Release Notes](#documentation--release-notes)

## Documentation & Release Notes
- **[QUICK_REFERENCE_GUIDE.md](QUICK_REFERENCE_GUIDE.md)**: How to use the phone and SD card.
- **[RELEASE_NOTES.txt](RELEASE_NOTES.txt)**: Version history and changelog.

## Hardware Components
- **Microcontroller**: [Wemos Lolin D32 Pro (ESP32)](https://www.wemos.cc/en/latest/d32/d32_pro.html)
  - *Note*: Has built-in SD Card slot.
- **Positioning**: M10 GNSS Module (GPS/GLONASS/Galileo)
- **Audio**: 
  - Module with ES8311 Codec + NS4150B Amplifier
  - On-board Microphone (ES8311)
- **Power**: 3000mAh Battery
- **Input**: 
  - Vintage Telephone Rotary Dial
  - Hook Switch (Gabeltaster)
  - Additional Button
- **Status**: 1x WS2812 LED
- **Enclosure**: Upcycled Vintage Telephone

## Features
1. **Atomic Clock**: Syncs time via GNSS satellites.
2. **Timer**: Set a countdown using the rotary dial.
3. **Alarm Clock**: Wake up to custom sounds or compliments.
4. **Compliment Dispenser**: Dial a specific number.
5. **Captive Portal**: Connect to Wi-Fi 'AtomicCharmer' to configure credentials and timezone.
6. **Standalone**: Operates completely independently without Home Assistant or external servers.

## User Interface & Control
- **Hook Switch (Gabeltaster)**:
  - *On Hook (Down)*: Idle / Settings Mode. Dialing sets Timer/Alarm.
  - *Off Hook (Up)*: Active Mode. Speak Time. Dialing plays Compliments.
  - *Toggle (Ringing)*: Stop Alarm/Timer.
- **Rotary Dial**: Input numbers.
- **Web Interface**: Configure System.

## SD Card Preparation

Use a FAT32 formatted SD Card. The file structure is crucial for the Atomic Charmer to function properly.

```
/
├── startup.mp3                (Played on boot)
├── compliments/               (MP3 Compliments)
│   ├── trump/                 (Files for Dial 1)
│   ├── badran/                (Files for Dial 2)
│   ├── yoda/                  (Files for Dial 3)
│   └── neutral/               (Files for Dial 4)
├── playlists/                 (System generated automatically)
├── ringtones/                 (Ringtones for Alarm/Ring)
│   ├── 1.wav
│   ├── 2.wav
│   └── ...
└── system/                    (System Tones)
    ├── dial_tone.wav
    ├── busy_tone.wav
    ├── error_tone.wav
    └── beep.wav
```

### Generating Audio Files
1. **Compliments:** Run the Text-to-Speech generation on the text files in `utils/compliments/grouped/`.
2. **System Tones:** Run `python utils/generate_tones.py` to create the WAV files in `sd_card_template/system`.

## Target Audience
Built specially for Sandra, a nerdy and inspiring colleague/mentor.
