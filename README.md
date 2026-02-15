# Dial-A-Charmer

Dial-A-Charmer is a vintage telephone brought back to life with more personality than ever. It announces internet-precise time with the confidence of someone who's never been late, doubles as an alarm and timer, and delivers compliments on demand—because who wouldn't want a phone that cheers them on?

**Easy to Install:**

1. **ESP-IDF Build (Recommended):** Build and flash from source with ESP-IDF.
2. **Manual Flash:** Download the latest build from [firmware/dial-a-charmer-2026-02-14.bin](firmware/dial-a-charmer-2026-02-14.bin) and upload it manually.

## Support my projects

If you like this project, consider a tip. Your tip motivates me to continue developing useful stuff for the DIY community. Thank you very much for your support!

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/D1D01HVT9A)

## Table of Contents

* [Hardware Support & Pinout](#hardware-support--pinout)
* [Project Structure](#project-structure)
* [Audio Configuration](#audio-configuration)
* [Status Indication](#status-indication)
* [Acoustic Signals](#acoustic-signals)
* [Getting Started](#getting-started)
* [Maintenance](#maintenance)
* [Documentation](QUICK_REFERENCE_GUIDE.md)
* [Release Notes](RELEASE_NOTES.txt)
* [License](#license)

## Functionality Overview

### User Interactions

| Function | Action / Trigger | Description |
| :--- | :--- | :--- |
| **Kitchen Timer** | **Receiver On Hook** → Dial `1`-`500` | Sets a countdown timer in minutes. Phone rings when time expires. |
| **Play Personas** | **Receiver Lifted** → Dial `1`-`5` | Plays audio content from specific categories (Personas). |
| **Timer Remaining** | **Receiver Lifted** → Dial `8` | Announces the remaining kitchen-timer time in minutes. |
| **Random Mix** | **Receiver Lifted** → Dial `11` | Plays a randomized mix from all Persona tracks. |
| **Voice Menu** | **Receiver Lifted** → Dial `0` | Plays spoken instructions for system codes. |
| **Voice Menu Options** | **While menu speaks** → Dial `1`-`4`, or `999` | Executes menu action immediately (Next Alarm, Night Mode, Phonebook, System Status, Reboot). |
| **Stop Ringing** | **Ringing** → Lift Receiver | Stops the alarm or timer alert. |
| **Snooze** | **Ringing** → Press Extra Button | Snoozes the daily alarm for the configured duration (set in Web UI), keeps an active kitchen timer untouched, and activates a slow breathing Signallampe effect. |
| **Web Interface** | Browser: `dial-a-charmer.local` | Manage settings, phonebook entries, and recurring alarm schedules. |

### System Capabilities

| Capability | Description |
| :--- | :--- |
| **Timekeeping** | Syncs via **NTP** with optional **DS3231 RTC** fallback (invalid RTC data is ignored until SNTP sync). |
| **Persistence** | Alarms and settings are saved to NVS and survive reboots. |
| **Night Mode** | Uses reduced base-speaker volume from Web UI (`night_base_volume`) and LED night profile. System sounds are not auto-muted. Manual toggle via voice menu stays active until next configured day-start hour. |
| **Trigger Priority** | If daily alarm and kitchen timer are due in the same loop cycle, daily alarm starts first; timer alert is processed right after alarm state is free. |

## Phonebook Defaults

The phonebook ships with default numbers. You can edit these entries in the Web UI (phonebook page).

| Number | Name | Action |
| :--- | :--- | :--- |
| `1` | Persona 1 | Play Persona 1 compliments |
| `2` | Persona 2 | Play Persona 2 compliments |
| `3` | Persona 3 | Play Persona 3 compliments |
| `4` | Persona 4 | Play Persona 4 compliments |
| `5` | Persona 5 | Play Persona 5 compliments |
| `8` | Timer Restzeit | Announce remaining kitchen timer minutes |
| `11` | Random Mix (Surprise) | Play a randomized mix |
| `110` | Zeitauskunft | Announce time (local) |
| `0` | Voice Admin Menu | Play spoken admin menu |
| `999` | System Reboot | Reboot device |

Voice Menu actions (dial while the menu speaks):

* `1` Next alarm
* `2` Night mode toggle
* `3` Phonebook options (reads numbers)
* `4` System status (WiFi, IP, NTP, SD)
* `999` System reboot

## Hardware Support & Pinout

Target hardware: **Ai-Thinker Audio Kit v2.2** (ES8388). Most peripherals connect via Header P2.

**Pinout (Header P2):**

| Component | Function | GPIO | Notes |
| :--- | :--- | :--- | :--- |
| **Audio** | Codec (I2S/I2C) | *Internal* | ES8388 (Stereo In/Out) |
| **Controls** | Pulse / Hook / Extra Btn | `5`, `19`, `36` | Connect to Header P2 |
| **Dial Mode** | State Contact | *Disabled* | GPIO released for WS2812 |
| **LED** | WS2812 Data | `23` | *Note: Disables Key 4* |
| **I2C Ext.** | SDA / SCL | `21`, `22` | For RTC module (disabled by default) |
| **SD Card** | SPI | *Internal* | On-board Slot |

### Audio Routing (Ai-Thinker)

The firmware leverages the stereo capabilities of the ES8388 codec to drive two separate audio paths:

* **Handset Audio** (Ear piece) is routed to the **Left Channel (LOUT)**.
* **Base Speaker** (Ringing/Speakerphone) is routed to the **Right Channel (ROUT)**.

The firmware automatically manages muting/unmuting these channels based on the device state (On-Hook/Off-Hook).

## Accessing the Configuration

Once the device is connected to your WiFi network (or you are connected to its Access Point), you can access the configuration interface by navigating to:

👉 **[http://dial-a-charmer.local](http://dial-a-charmer.local)**

(Requires a modern browser on Android, iOS, Windows, or macOS. If mDNS fails, use the IP announced via the Voice Menu: Dial `0`, then `4` for Systemstatus.)

### Signallampe Settings (Web UI)

The Configuration page includes a **Signallampe** card (below **Timer-Ton**) with:

* LED enable/disable toggle
* Day and Night brightness sliders (percent)
* Day/Night start hour selection (0-23)

## Project Structure

* **`main/`**: Main firmware C++ source code.
* **`main/include/`**: Configuration headers (`app_config.h`).
* **`main/web_ui/`**: Embedded web UI assets.
* **`components/`**: Custom firmware components.
* **`utils/`**: Python helper scripts for audio management.
* **`sd_card_content/`**: Generated SD card file structure.

## Audio Configuration

Use a FAT32 formatted SD Card. The file structure is crucial for the Dial-A-Charmer to function properly.

### SD Card Structure

```text
/
├── persona_01/                (Persona 1 tracks - Dial 1)
├── persona_02/                (Persona 2 tracks - Dial 2)
├── persona_03/                (Persona 3 tracks - Dial 3)
├── persona_04/                (Persona 4 tracks - Dial 4)
├── persona_05/                (Persona 5 tracks - Dial 5)
├── playlists/                 (Generated on PC by utils/generate_sd_content.py)
├── ringtones/                 (Alarm and ring tones)
├── system/                    (System prompts and tones)
└── time/                      (Time announcements, DE/EN)
```

### Audio Utilities (`utils/`)

**`generate_sd_content.py`** (Recommended)
This all-in-one script prepares a complete `sd_card_content` folder for you. It ensures your device works immediately without hunting for MP3 files.

* **Usage:** `python utils/generate_sd_content.py`
* **Feature (Structure):** Creates all required folders (`persona_XX`, `system`, `time` etc.).
* **Feature (TTS Integration):** Automatically downloads high-quality Google TTS speech files for Numbers, Dates, and System Messages in both **German & English**.
* **Feature (Tone Synthesis):** Generates clean `wav` files for Dial Tone, Busy Signal, and Beeps using Python's audio libraries (no recording needed).
* **Feature (Voice Prompts):** Builds voice menu prompts, phonebook options (with numbers), and hook pickup/hangup SFX.
* **Feature (Telephony Tones):** Uses classic US dial tone and busy signal, and outputs them at a lower level for better balance.
* **Feature (Offline Fonts):** Copies `AATriple.otf` and `3620-plaisir-app.otf` into `/fonts/` so the Web UI looks correct offline. License texts are included alongside the fonts in `/fonts/` and on the SD card. Thanks to the font creators. Sources: [https://fontesk.com/triple-font/](https://fontesk.com/triple-font/) and [https://fontesk.com/plaisir-font/](https://fontesk.com/plaisir-font/)
* **Output:** Populates `sd_card_content/` which you just copy to your SD card.

**`split_audio.py`**
This tool splits large, long audio files (e.g. combined recordings) into individual MP3 files based on silence.

* **Usage:** `python utils/split_audio.py -o my_output_folder (input_files)`
* **Function:** Detects silence gaps >1000ms and chops the file. Resulting files (`001.mp3`, `002.mp3`...) are ready for `persona_XX` folders.

## Status Indication

Single WS2812B LED provides visual feedback depending on the system state.

Brightness and schedule are configurable in the **Signallampe** card (Web UI).

| State | Color/Effect | Meaning |
| :--- | :--- | :--- |
| **Booting** | 🔵 Blue/Gold Pulsing | System initializing or connecting to WiFi |
| **Idle** | 🟠 Vintage Orange Glow | Ready, Filament-style flickering |
| **Alarm Ringing** | ⚪ Warm White Pulsing | Alarm is active (Wake up!) |
| **Timer Alert** | 🔴 Fast Red Pulsing | Timer finished (Panic Mode) |
| **Snooze** | ⚪ Slow Warm White Breathing | Snooze active (configured duration) |
| **Error / No SD** | 🔴 Red SOS Pattern | Hardware fault or Missing SD Card |

## Acoustic Signals

The system uses specific WAV files in `/system/` for feedback:

| File | Context |
| :--- | :--- |
| `dial_tone.wav` | Played when handset is lifted. |
| `beep.wav` | Keypress confirmation or mode change. |
| `busy_tone.wav` | Call ended or invalid state. |
| `hook_pickup.wav` | Short pickup click before persona playback. |
| `hook_hangup.wav` | Short hangup click after persona playback. |

Note: During **Night Mode**, base speaker level follows the configured night slider value. System sounds remain enabled. Manual activation via voice menu (`0` → `2`) remains active until the next configured day-start hour from Web UI.

### Night Mode Test Plan (Hardware)

1. Set `Day Start Hour` in Web UI to a known near-future value (for example current hour + 1).
2. Lift receiver, dial `0`, then dial `2` while voice menu is active.
3. Verify `night_on` announcement, reduced base-speaker volume, and night LED profile.
4. Wait until configured `Day Start Hour` is reached and verify automatic return to day mode.
5. Repeat `0` → `2` to disable manually and verify `night_off` announcement and immediate return to day settings.

## Getting Started

1. **Prepare Hardware:** Assemble the ESP32, Audio Module, and Rotary Phone mechanics according to the pinout.
2. **Prepare SD Card:**
    * **Run Generator:** Execute `python utils/generate_sd_content.py` to create the essential system files, voice prompts, and fonts.
    * **Format:** Format your MicroSD card to **FAT32**.
    * **Copy:** Copy the *contents* of the generated `sd_card_content` folder to the root of the SD card.
    * **Personalize:** Add your own tracks to the `persona_XX` folders.
3. **Flash Firmware (ESP-IDF):**
    * Open project in VS Code with ESP-IDF.
    * Connect your ESP32 board via USB.
    * **Build/Flash**: Run `idf.py build flash`.
4. **Configure:**
    * On first boot, connect to WiFi AP `Dial-A-Charmer`.
    * Go to `192.168.4.1` to set your Timezone and Credentials.

## Maintenance

* **Time:** Time is maintained via NTP. Ensure the device connects to Wi-Fi at least once on boot to sync the clock.

## Debug Logging (SD Card)

You can enable buffered SD logging during testing (see `app_config.h`). Logs are written to:

* `/sdcard/logs/app.log`

Logging is buffered and flushed periodically to reduce interference with audio playback.

Runtime state tags are available to diagnose alarm/timer interactions quickly:

* `TIMER_STATE` (set/cancel/expired/cleared)
* `SNOOZE_STATE` (set/expired/cleared)

## Planned Improvements

* Loud noise on reboot > need to ensure that amps are not powered during reboot?
* Alarm-off text output is currently played over handset > should be switched to base speaker + instead of using a specific announcement, we should play random messages and make this feature switchable for each daily alarm
* WIFI-bug ... system seems to hang when not connected to WIFI while using captive portal on iPhone to make settings
* IP-Address should be displayed in Captive Portal and on Configuration page of the web-ui

## License

This project is licensed under the **PolyForm Noncommercial License 1.0.0**.

* **Noncommercial Use**: You may use this software for personal, educational, or evaluation purposes.
* **Commercial Use Restricted**: You may NOT use this software for commercial purposes (selling, paid services, business use) without prior written consent.

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
