# Dial-A-Charmer

Dial-A-Charmer is a vintage telephone brought back to life with more personality than ever. It announces GNSS-precise time with the confidence of someone who's never been late, doubles as an alarm and timer, and delivers compliments on demand—because who wouldn't want a phone that cheers them on?

**Easy to Install:**
1.  **Web Installer (Recommended):** Use the [Dial-A-Charmer Web Installer](https://TechwriterSSchmidt.github.io/Dial-A-Charmer/) to flash directly from Chrome/Edge.
2.  **PlatformIO:** Build from source for development.

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
| | **Kitchen Timer** | Set a countdown using the rotary dial (e.g., dial '12' for a perfect 12 minutes Pizza timer). |
| **Interaction** | **Compliment Dispenser** | Dial specific numbers to hear compliments from different "personas". |
| | **Multi-Language** | Supports **German** and **English** for both Voice and System prompts. |
| | **Vintage Feel** | Uses original rotary dial and hook switch mechanics. |
| **System** | **Standalone** | Operates completely independently without Home Assistant or external servers. |
| | **Captive Portal** | Wi-Fi hotspot 'DialACharmer' for simple timezone/settings configuration. |

## Operational Logic

The device differentiates between two main usage modes based on the handset state:

### 1. Idle Mode (Handset On-Hook)
*   **State:** The phone is waiting on the cradle.
*   **Action (Dialing):** Rotating the dial sets the **Timer**.
    *   Dial `5` -> Sets 5 minute timer.
    *   Lift the handset shortly off the cradle -> Resets timer and the alarm.
*   **Action (Set Alarm):** Hold the extra button down while dialing 4 digits.
    *   Dial `0730` (while holding button) -> Sets alarm for 07:30.
    *   Hold button and lift handset -> Deletes current alarm.

### 2. Active Mode (Handset Lifted)
*   **Trigger:** Lift the handset (Off-Hook).
*   **Behavior:** The phone "wakes up" with a dial tone and plays a random compliment after 2s.
*   **Action (Dialing):** Input numbers to request content:
    *   **Dial `1`**: Play from `mp3_group_01` (Persona 1)
    *   **Dial `2`**: Play from `mp3_group_02` (Persona 2)
    *   **Dial `3`**: Play from `mp3_group_03` (Persona 3)
    *   **Dial `4`**: Play from `mp3_group_04` (Persona 4)
    *   **Dial `0`**: Main Menu (Status, Language, etc.)
    *   **Dial `8`**: Speak System IP & Status
    *   **Dial `90`**: Toggle Alarms On/Off

### 3. Ringing Mode (Alarm/Timer)
*   **Trigger:** Countdown expires or Alarm time is reached.
*   **Behavior:** Ringtones play, Vibration motor activates, LED flashes.
*   **Stop:** Lift the handset or toggle the hook switch to reset and silence.

### 4. LED Signaling (Visual Interface)
The device communicates distinct states via the integrated WS2812 LED using organic, vintage-style animations:

*   **Boot / Connecting:** Breathing Slow (Blue/Teal & Gold Mix).
*   **Idle / Ready:** Vintage Filament Glow (Flickering Warm Orange/Gold).
*   **Alarm Clock (Ringing):** Pulsing Warm White.
*   **Timer (Alert):** Fast Pulsing Red (Panic Mode).
*   **Snooze Mode:** Solid Warm White (Steady Glow).
*   **Error / Missing SD:** SOS Pattern (Red).

## Hardware Support & Pinout

**Target Board:** Wemos Lolin D32 Pro (ESP32)

| Component | Pin Function | GPIO | Notes |
| :--- | :--- | :--- | :--- |
| **I2S Audio** | BCLK | `GPIO 26` | ES8311 / NS4150B |
| | LRC | `GPIO 25` | |
| | DOUT | `GPIO 27` | *Modified from 22 to avoid conflict* |
| **I2C Bus** | SDA | `GPIO 21` | Codec Configuration |
| | SCL | `GPIO 22` | |
| **Input** | Dial Pulse | `GPIO 5` | Input with Internal Pull-Up (No Resistor needed!) |
| | Dial Mode | `GPIO 36` | Optional: Contact detecting rotation active (Input Only) |
| | Hook Switch | `GPIO 32` | |
| | Extra Button | `GPIO 33` | |
| **GNSS (GPS)** | RX | `GPIO 35` | M10 Module |
| | TX | `GPIO 0` | |
| **Output** | Vibration | `GPIO 2` | Haptic Feedback |
| | LED Data | `GPIO 13` | WS2812B |
| **Storage** | SD CS | `GPIO 4` | On-board SD Slot |

## Accessing the Configuration

Once the device is connected to your WiFi network (or you are connected to its Access Point), you can access the configuration interface by navigating to:

👉 **http://dial-a-charmer.local**

(Requires a modern browser on Android, iOS, Windows, or macOS. No IP address needed!)

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
├── mp3_group_01/              (Persona 1 Compliments - Dial 1)
├── mp3_group_02/              (Persona 2 Compliments - Dial 2)
├── mp3_group_03/              (Persona 3 Compliments - Dial 3)
├── mp3_group_04/              (Persona 4 Compliments - Dial 4)
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

Single WS2812B LED provides visual feedback depending on the system state.

| State | Color/Effect | Meaning |
| :--- | :--- | :--- |
| **Booting** | 🔵 Blue/Gold Pulsing | System initializing or connecting to WiFi |
| **Idle** | 🟠 Vintage Orange Glow | Ready, Filament-style flickering |
| **Alarm Ringing** | ⚪ Warm White Pulsing | Alarm is active (Wake up!) |
| **Timer Alert** | 🔴 Fast Red Pulsing | Timer finished (Panic Mode) |
| **Snooze** | ⚪ Warm White Solid | Snooze active (9 min sleep) |
| **Error / No SD** | 🔴 Red SOS Pattern | Hardware fault or Missing SD Card |
| **Battery Low** | 🔴 Red Pulsing | Recharge Battery (< 15%) |

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

## Future Options

*   **Clock:** Add option to set individual alarms for for each day of the week via Webinterface.
*   **Voice Menu:** Implement a classic `dial a number` interactive voice menu to guide users through settings (e.g., "Press 1 to change Ringtone").
*   **Media:** AGC for mic and, if possible, AEC for Gemini AI usage.

## Maintenance

*   **Battery:** The system runs on a 3000mAh LiPo. Charge when the LED indicates low battery (if configured) or audio becomes distorted.
*   **Time:** Thanks to GNSS, time is always accurate as long as the device has occasional sky visibility.


