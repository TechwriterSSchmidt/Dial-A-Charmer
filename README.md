# Dial-A-Charmer

Dial-A-Charmer is a vintage telephone brought back to life with more personality than ever. It announces GNSS-precise time with the confidence of someone who's never been late, doubles as an alarm and timer, and delivers compliments on demand—because who wouldn't want a phone that cheers them on? Created for my mentor Sandra, whose nerdiness and warmth outshine even the fanciest electronics.

## Support my projects

If you like this project, consider a tip. Your tip motivates me to continue developing useful stuff for the DIY community. Thank you very much for your support!

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/D1D01HVT9A)

## Table of Contents
*   [Features](#features)
*   [Operational Logic](#operational-logic)
*   [Hardware Support & Pinout](#hardware-support--pinout)
*   [Project Structure](#project-structure)
*   [Audio Configuration](#audio-configuration)
*   [Status Indication](#status-indication)
*   [Acoustic Signals](#acoustic-signals)
*   [Getting Started](#getting-started)
*   [Maintenance](#maintenance)
*   [Documentation](QUICK_REFERENCE_GUIDE.md)
*   [Release Notes](RELEASE_NOTES.txt)

## Features

| Category | Feature | Description |
| :--- | :--- | :--- |
| **Timekeeping** | **Atomic Clock** | Syncs time via GNSS (M10 Module) for precision. |
| | **Alarm Clock** | Wake up to custom sounds or compliments. |
| | **Kitchen Timer** | Set a countdown using the rotary dial (e.g., dial '5' for 5 minutes). |
| **Interaction** | **Compliment Dispenser** | Dial specific numbers to hear compliments from different "personas". |
| | **Vintage Feel** | Uses original rotary dial and hook switch mechanics. |
| **System** | **Standalone** | Operates completely independently without Home Assistant or external servers. |
| | **Captive Portal** | Wi-Fi hotspot 'DialACharmer' for simple timezone/settings configuration. |

## Operational Logic

The device differentiates between two main usage modes based on the handset state:

### 1. Idle Mode (Handset On-Hook)
*   **State:** The phone is waiting on the cradle.
*   **Action (Dialing):** Rotating the dial sets the **Timer**.
    *   Dial `5` -> Sets 5 minute timer.
    *   Dial `0` -> Cancels timer.
*   **Action (Button):** Pressing the extra button toggles the **Alarm** (On/Off).

### 2. Active Mode (Handset Lifted)
*   **Trigger:** Lift the handset (Off-Hook).
*   **Behavior:** The phone "wakes up", potentially announcing the time or playing a greeting.
*   **Action (Dialing):** Input numbers to request content:
    *   **Dial `1`**: Trump Compliments (`mp3_group_01`)
    *   **Dial `2`**: Badran Compliments (`mp3_group_02`)
    *   **Dial `3`**: Yoda Compliments (`mp3_group_03`)
    *   **Dial `4`**: Neutral Compliments (`mp3_group_04`)
    *   **Dial `0`**: Play Main Playlist (Music/mixed)

### 3. Ringing Mode (Alarm/Timer)
*   **Trigger:** Countdown expires or Alarm time is reached.
*   **Behavior:** Ringtones play, Vibration motor activates, LED flashes.
*   **Stop:** Lift the handset or toggle the hook switch to silence.

## Hardware Support & Pinout

**Target Board:** Wemos Lolin D32 Pro (ESP32)

| Component | Pin Function | GPIO | Notes |
| :--- | :--- | :--- | :--- |
| **I2S Audio** | BCLK | `GPIO 26` | ES8311 / NS4150B |
| | LRC | `GPIO 25` | |
| | DOUT | `GPIO 27` | *Modified from 22 to avoid conflict* |
| **I2C Bus** | SDA | `GPIO 21` | Codec Configuration |
| | SCL | `GPIO 22` | |
| **Input** | Dial Pulse | `GPIO 34` | Input Only |
| | Hook Switch | `GPIO 32` | |
| | Extra Button | `GPIO 33` | |
| **GNSS (GPS)** | RX | `GPIO 16` | M10 Module |
| | TX | `GPIO 17` | |
| **Output** | Vibration | `GPIO 2` | Haptic Feedback |
| | LED Data | `GPIO 13` | WS2812B |
| **Storage** | SD CS | `GPIO 4` | On-board SD Slot |

## Project Structure

*   **`src/`**: Main firmware C++ source code.
*   **`include/`**: Configuration headers (`config.h`).
*   **`utils/`**: Python helper scripts for audio management.
*   **`sd_card_template/`**: Blueprint for the SD card file structure.

## Audio Configuration

Use a FAT32 formatted SD Card. The file structure is crucial for the Dial-A-Charmer to function properly.

### SD Card Structure

```text
/
├── startup.mp3                (Played on boot)
├── mp3_group_01/              (Trump Compliments - Dial 1)
├── mp3_group_02/              (Badran Compliments - Dial 2)
├── mp3_group_03/              (Yoda Compliments - Dial 3)
├── mp3_group_04/              (Neutral Compliments - Dial 4)
├── playlists/                 (System generated automatically)
├── ringtones/                 (Ringtones for Alarm/Ring)
│   ├── 1.wav
│   └── 2.wav
└── system/                    (System Tones)
    ├── dial_tone.wav
    ├── busy_tone.wav
    ├── error_tone.wav
    └── beep.wav
```

### Audio Utilities (`utils/`)

**`split_audio.py`**
This tool splits large, long audio files (e.g. combined recordings) into individual MP3 files based on silence.

*   **Usage:** `python utils/split_audio.py -o my_output_folder (input_files)`
*   **Function:** Detects silence gaps >1000ms and chops the file. Resulting files (`001.mp3`, `002.mp3`...) are ready for `mp3_group_XX` folders.

## Status Indication

Single WS2812B LED provides visual feedback.

| State | Color/Effect | Meaning |
| :--- | :--- | :--- |
| **Booting** | 🔵 Blue | System initializing |
| **GPS Search** | 🟠 Orange | Waiting for Satellite Lock |
| **Ready** | 🟢 Green | Time synced, ready to use |
| **Error** | 🔴 Red | SD Card missing or Hardware fault |
| **Ringing** | ⚪ White Flashing | Alarm or Timer active |

## Acoustic Signals

The system uses specific WAV files in `/system/` for feedback:

| File | Context |
| :--- | :--- |
| `dial_tone.wav` | Played when handset is lifted (if enabled). |
| `beep.wav` | Keypress confirmation or mode change. |
| `error_tone.wav` | Invalid input or system error. |
| `busy_tone.wav` | Call ended or invalid state. |

## Getting Started

1.  **Prepare Hardware:** Assemble the ESP32, Audio Module, and Rotary Phone mechanics according to the pinout.
2.  **Prepare SD Card:**
    *   Format MicroSD card to **FAT32**.
    *   Copy the contents of `sd_card_template` to the root of the card.
    *   Fill `mp3_group_XX` folders with your desired MP3 files.
3.  **Flash Firmware:**
    *   Open project in VS Code with PlatformIO.
    *   Connect Wemos D32 Pro via USB.
    *   Upload Filesystem (Optional, if using LittleFS, otherwise just skip this).
    *   **Upload Firmware**.
4.  **Configure:**
    *   On first boot, connect to WiFi AP `DialACharmer`.
    *   Go to `192.168.4.1` to set your Timezone and Credentials (if needed for future OTA).

## Maintenance

*   **Battery:** The system runs on a 3000mAh LiPo. Charge when the LED indicates low battery (if configured) or audio becomes distorted.
*   **Time:** Thanks to GNSS, time is always accurate as long as the device has occasional sky visibility.

***

*Built specially for Sandra, a nerdy and inspiring colleague/mentor.*
