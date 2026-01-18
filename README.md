# Dial-A-Charmer

Dial-A-Charmer is a vintage telephone brought back to life with more personality than ever. It announces internet-precise time with the confidence of someone who's never been late, doubles as an alarm and timer, and delivers compliments on demand—because who wouldn't want a phone that cheers them on?

**Easy to Install:**
1.  **Web Installer (Recommended):** Use the [Dial-A-Charmer Web Installer](https://TechwriterSSchmidt.github.io/Dial-A-Charmer/) to flash directly from Chrome/Edge.
2.  **PlatformIO:** Build from source for development.
3.  **Manual Flash:** Download the [latest firmware.bin](https://TechwriterSSchmidt.github.io/Dial-A-Charmer/firmware.bin) and upload it via OTA-option.

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
*   [License](#license)

## Features

| Category | Feature | Description |
| :--- | :--- | :--- |
| **Timekeeping** | **Atomic Precision** | Syncs time exclusively via **NTP (Internet Time)** over Wi-Fi. |
| | **Dual Alarm System** | **Single Alarm** (via Rotary Dial) for one-off use + **Repeating Alarm** (via Web) for schedules. |
| | **Smart Skip** | Dial `91` to skip just the *next* recurring alarm (sleep in!) without canceling the schedule. |
| | **Kitchen Timer** | Set a countdown using the rotary dial (e.g., dial '12' for a perfect 12 minutes Pizza timer). |
| **Interaction** | **Compliment Dispenser** | Dial specific numbers to hear compliments from different "personas". |
| | **AI Assistant** | Integrated **Gemini AI** allows the phone to tell jokes or answer prompts via the Phonebook. |
| | **Multi-Language** | Supports **German** and **English** for Voice, System prompts, and Web Interface. |
| | **Half-Duplex Audio** | Intelligent echo-cancellation ensures AI prompts aren't interrupted by their own output. |
| **System** | **Zero-Install Web App** | Modern **Single-Page-Application (SPA)** served directly from the device (requires SPIFFS). |
| | **System Watchdog** | Integrated Hardware Watchdog auto-resets the device if it freezes (>20s). |
| | **Smart Deep Sleep** | Device sleeps when idle and wakes up automatically for the next alarm (or when the receiver is lifted). |
| | **Audio Engine V2** | **Multithreaded** audio core for stutter-free playback even during heavy WiFi traffic. |
| | **Reliability** | **RTC & NVS Support** ensures alarms survive reboots and power outages. Fallback Tones for missing SD files. |
| | **OTA Updates** | Update firmware wirelessly via the Web Interface. |
| | **Captive Portal** | Wi-Fi hotspot 'DialACharmer' for simple initial setup. |

## Operational Logic

The device differentiates between two main usage modes based on the handset state:

### 1. Idle Mode (Handset On-Hook)
*   **State:** The phone is waiting on the cradle.
*   **Action (Dialing):** Rotating the dial sets the **Kitchen Timer**.
    *   Dial `5` -> Sets 5 minute timer.
    *   Lift the handset shortly off the cradle or dial another number to reset.
*   **Action (Set Single Alarm):** Hold the extra button down while dialing 4 digits.
    *   Dial `0730` (while holding button) -> Sets alarm for 07:30.
    *   This alarm has **Priority** and rings once, then clears itself.
*   **Action (Cancel Single Alarm):** Press and **Hold** the extra button, then **Lift** the handset.
    *   Voice confirmation ("Alarm deleted") confirms the deletion of the manual alarm.

### 2. Active Mode (Handset Lifted)
*   **Trigger:** Lift the handset (Off-Hook).
    *   **Special Trigger:** If a **Timer** is running, lifting the handset immediately cancels it (confirmed via Base Speaker).
*   **Behavior:** The phone "wakes up" with a **Dial Tone**.
*   **Action (Dialing):** Input numbers to request content:
    *   **Dial `1`-`4`**: Switch to specific Persona Playlist (Trump, Badran, Yoda, Neutral).
    *   **Dial `0`**: Play next random track.
    *   **Dial `8`**: Speak System IP & Status.
    *   **Dial `9`**: Voice Menu Instructions.
    *   **Dial `90`**: Toggle **ALL** Alarms (On/Off).
    *   **Dial `91`**: Toggle **Skip Next Repeating Alarm**.

### 3. Ringing Mode (Alarm/Timer)
*   **Trigger:** Countdown expires or Alarm time is reached.
*   **Behavior:** Ringtones play, Vibration motor activates, LED flashes.
*   **Stop:** Lift the handset and hang up again to stop the ringing.
*   **Snooze:** Lift the handset but **do not** hang up. The alarm snoozes for the configured duration (0-20 min).

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

**Audio Architecture (v2):**
*   **DAC:** PCM5100A (I2S) - Shared Stereo Output.
    *   **Left Channel:** Handset Earpiece (Low Power).
    *   **Right Channel:** Base Speaker (Amplified/Loud).
*   **ADC:** MAX9814 (Analog) - Electret Microphone with Auto Gain Control.

| Component | Pin Function | GPIO | Notes |
| :--- | :--- | :--- | :--- |
| **I2S Audio Out** | BCLK | `GPIO 26` | PCM5100A I2S Bit Clock |
| (PCM5100A)   | LRC (WS) | `GPIO 25` | PCM5100A Word Select |
|              | DOUT | `GPIO 27` | ESP32 DOUT -> PCM5100A DIN |
|              | MCLK | N/A | Generated internally by PCM5100A |
| **Audio Input**  | ADC | `GPIO 36` | MAX9814 Out -> ESP32 VP/ADC1_CH0 |
| **I2C Bus**  | SDA | `GPIO 21` | Sensors / Port Expanders (Optional) |
|              | SCL | `GPIO 22` | |
| **Controls** | Dial Pulse | `GPIO 5` | Rotary Dial Pulse (Active Low) |
| | Dial Mode | `GPIO 36` | Detects when dial assumes active position |
| | Hook Switch | `GPIO 32` | |
| | Extra Button | `GPIO 33` | |
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
├── persona_01/                (Persona 1 Compliments - Dial 1)
├── persona_02/                (Persona 2 Compliments - Dial 2)
├── persona_03/                (Persona 3 Compliments - Dial 3)
├── persona_04/                (Persona 4 Compliments - Dial 4)
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

**`generate_sd_content.py`** (Recommended)
This all-in-one script prepares a complete `sd_card_template` folder for you. It ensures your device works immediately without hunting for MP3 files.

*   **Usage:** `python utils/generate_sd_content.py`
*   **Features:**
    *   **Structure:** Creates all required folders (`persona_XX`, `system`, `time` etc.).
    *   **TTS Integration:** Automatically downloads high-quality Google TTS speech files for Numbers (0-100), Dates (Days, Months, Years), and System Messages ("Alarm Set", "Error", etc.) in both **German & English**.
    *   **Tone Synthesis:** Generates clean `wav` files for Dial Tone, Busy Signal, and Beeps using Python's audio libraries (no recording needed).
    *   **Offline Fonts:** Downloads "Zen Tokyo Zoo" and "Pompiere" fonts to `/system/fonts/` so the Web UI looks perfect even without Internet.
    *   **Startup Sound:** Synthesizes a musical ambient chord for boot-up.
*   **Output:** Populates `sd_card_template/` which you just copy to your SD card.

**`split_audio.py`**
This tool splits large, long audio files (e.g. combined recordings) into individual MP3 files based on silence.

*   **Usage:** `python utils/split_audio.py -o my_output_folder (input_files)`
*   **Function:** Detects silence gaps >1000ms and chops the file. Resulting files (`001.mp3`, `002.mp3`...) are ready for `persona_XX` folders.

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
    *   **Run Generator:** Execute `python utils/generate_sd_content.py` to create the essential system files, voice prompts, and fonts.
    *   **Format:** Format your MicroSD card to **FAT32**.
    *   **Copy:** Copy the *contents* of the generated `sd_card_template` folder to the root of the SD card.
    *   **Personalize:** Add your own MP3 music/compliments to the `persona_XX` folders.
3.  **Flash Firmware:**
    *   Open project in VS Code with PlatformIO.
    *   Connect Wemos D32 Pro via USB.
    *   **Upload Filesystem Image**: This is **REQUIRED** for the new Web UI. Run `PlatformIO: Upload Filesystem Image`.
    *   **Upload Firmware**: Run `The standard PlatformIO Upload` task.
4.  **Configure:**
    *   On first boot, connect to WiFi AP `DialACharmer`.
    *   Go to `192.168.4.1` to set your Timezone and Credentials (if needed for future OTA).

## Future Options



## Maintenance

*   **Battery:** The system runs on a 3000mAh LiPo. Charge when the LED indicates low battery (if configured) or audio becomes distorted.
*   **Time:** Time is maintained via NTP. Ensure the device connects to Wi-Fi at least once on boot to sync the clock.

## License

This project is licensed under the **PolyForm Noncommercial License 1.0.0**.

- **Noncommercial Use**: You may use this software for personal, educational, or evaluation purposes.
- **Commercial Use Restricted**: You may NOT use this software for commercial purposes (selling, paid services, business use) without prior written consent.

<details>
<summary>View Full License Text</summary>

### PolyForm Noncommercial License 1.0.0

#### 1. Purpose
This license allows you to use the software for noncommercial purposes.

#### 2. Agreement
In order to receive this license, you must agree to its rules. The rules of this license are both obligations (like a contract) and conditions to your license. You must not do anything with this software that triggers a rule that you cannot or will not follow.

#### 3. License Grant
The licensor grants you a copyright license for the software to do everything you might do with the software that would otherwise infringe the licensor's copyright in it for any permitted purpose. However, you may only do so to the extent that such use does not violate the rules.

#### 4. Permitted Purpose
A purpose is a permitted purpose if it consists of:
1. Personal use
2. Evaluation of the software
3. Development of software using the software as a dependency or evaluation tool
4. Educational use

**Commercial use is strictly prohibited without prior written consent from the author.**

#### 5. Rules

##### 5.1. Noncommercial Use
You must not use the software for any commercial purpose. A commercial purpose includes, but is not limited to:
1. Using the software to provide a service to third parties for a fee.
2. Selling the software or a derivative work.
3. Using the software in a commercial environment or business workflow.

##### 5.2. Notices
You must ensure that anyone who gets a copy of any part of the software from you also gets a copy of these terms or the URL for them above, as well as copies of any copyright notice or other rights notice in the software.

#### 6. Disclaimer
**THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.**

</details>


