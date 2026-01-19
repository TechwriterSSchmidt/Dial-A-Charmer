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

## Functionality Overview

### User Interactions

| Function | Action / Trigger | Description |
| :--- | :--- | :--- |
| **Kitchen Timer** | **Receiver On Hook** → Dial `1`-`120` | Sets a countdown timer in minutes. Phone rings when time expires. |
| **Set One-Time Alarm** | **Receiver On Hook** → Hold Button + Dial `HHMM` | Sets a single priority alarm (e.g., dial `0730` for 7:30 AM). |
| **Delete Alarm** | **Receiver On Hook** → Hold Button + Lift Receiver | Deletes the manual alarm or cancels a running timer. |
| **Access Point** | **Receiver On Hook** → Hold Button (10s) | Activates configuration hotspot `Dial-A-Charmer` (Password: none). |
| **Play Personnel** | **Receiver Lifted** → Dial `1`-`4` | Plays audio content from specific categories (Personas). |
| **Surprise Mix** | **Receiver Lifted** → Dial `0` | Plays a random track from your collection or AI-generated content. |
| **System Status** | **Receiver Lifted** → Dial `8` | Announces IP address and WiFi signal strength. |
| **Voice Menu** | **Receiver Lifted** → Dial `9` | Plays spoken instructions for system codes. |
| **Toggle Alarms** | **Receiver Lifted** → Dial `90` | Enables or disables all alarms globally. |
| **Skip Next Alarm** | **Receiver Lifted** → Dial `91` | Skips only the next scheduled recurring alarm. |
| **Stop Ringing** | **Ringing** → Lift Receiver, then Hang Up | Stops the alarm or timer alert. |
| **Snooze** | **Ringing** → Lift Receiver (Keep Off Hook) | Snoozes the alarm for the configured duration (default: 9 min). |
| **Web Interface** | Browser: `dial-a-charmer.local` | Manage settings, phonebook entries, and recurring alarm schedules. |

### System Capabilities

| Capability | Description |
| :--- | :--- |
| **Timekeeping** | Syncs exclusively via **NTP (Internet Time)** for atomic precision. |
| **Smart Deep Sleep** | Automatically sleeps when idle and wakes for alarms or receiver activity. |
| **Watchdog** | Hardware watchdog guards against freezes (>20s). |
| **Audio Engine** | Multithreaded core supports stutter-free playback and half-duplex echo cancellation. |
| **Persistence** | Alarms and settings are saved to NVS and survive reboots. |
| **OTA Updates** | Firmware can be updated wirelessly via the Web Interface. |

## Hardware Support & Pinout

The project supports two primary hardware configurations:

### Option 1: DIY Standard (Lolin D32 Pro)
Recommended for custom builds. Uses I2S DAC (PCM5100A) and I2S/Analog Mic (MAX9814).

**Pinout:**
| Component | Function | GPIO | Notes |
| :--- | :--- | :--- | :--- |
| **I2S Audio** | BCLK / LRC / DOUT | `26`, `25`, `27` | PCM5100A DAC |
| **Audio In** | ADC Mic | `36` | MAX9814 (Analog) |
| **Controls** | Pulse / Hook / Btn | `5`, `32`, `33` | Rotary Interface |
| **Dial Mode** | State Contact | `34` | Closed = Dialing Active |
| **Peripherals**| LED / Vibrate / I2C | `13`, `2`, `21`/`22` | WS2812, DRV2605, RTC |

### Option 2: All-in-One (Ai-Thinker Audio Kit v2.2)
Uses the onboard ES8388 Codec. Most peripherals connect via Header P2.

**Pinout (Header P2):**
| Component | Function | GPIO | Notes |
| :--- | :--- | :--- | :--- |
| **Audio** | Codec (I2S/I2C) | *Internal* | ES8388 (Stereo In/Out) |
| **Controls** | Pulse / Hook / Btn | `5`, `19`, `18` | Connect to Header P2 |
| **Dial Mode** | State Contact | `36` | Use Key 1 on Board |
| **LED** | WS2812 Data | `23` | *Note: Disables Key 4* |
| **I2C Ext.** | SDA / SCL | `21`, `22` | For RTC module |
| **SD Card** | SPI | *Internal* | On-board Slot |

*Note: Ensure you select the correct environment (`lolin_d32_pro` or `esp32_audio_kit`) in PlatformIO before flashing.*

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

1. Voice menu with a dialable number to announce the next alarm.
2. Option to directly delete alarms that have been set via rotary dial.

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


