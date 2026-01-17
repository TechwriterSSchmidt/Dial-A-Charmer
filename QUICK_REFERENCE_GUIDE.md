# Quick Reference Guide - Dial-A-Charmer

## 📞 How to Use
1. **Hear a Compliment:**
   - Lift the receiver.
   - **Automatic:** The **Random Surprise Mix** starts immediately.
   - **Dial 1:** Persona 1 Style (Trump).
   - **Dial 2:** Persona 2 Style (Badran).
   - **Dial 3:** Persona 3 Style (Yoda).
   - **Dial 4:** Persona 4 Style (Neutral).
   - **Dial 0:** Next Random Surprise Track.

2. **Hear the Time:**
   - Press the **Extra Button** (if available).
   
3. **Set a Kitchen Timer:**
   - Keep receiver **ON** the hook.
   - Dial a number (e.g., 5).
   - A timer is set for **5 minutes**.
   - Phone rings when time is up. Lift receiver to stop ringing.

4. **Set a Single Priority Alarm:**
   - Keep receiver **ON** the hook.
   - Press and **HOLD** the Extra Button.
   - Dial 4 digits for the time (e.g., `0`, `7`, `3`, `0` for 07:30).
   - A confirmation sound ("Alarm Set") plays.
   - *Note: This alarm rings once and then clears itself.*

5. **Stop / Snooze Alarm:**
   - **Stop:** Lift receiver and hang up again.
   - **Snooze:** Lift receiver but **DO NOT** hang up (put it aside). 
   - *Snooze duration is configurable in Web Interface (Default: 9 min).*

6. **Voice Menu & Admin:**
   - **Dial 9:** Hear current status and menu options.
   - **Dial 90:** Toggle **ALL** Alarms (On/Off).
   - **Dial 91:** Skip **Next Repeating Alarm** (e.g. skip tomorrow morning, but keep schedule active).
   - **Dial 8:** Speak full system status (IP, Signal, etc.).
   - **Delete Single Alarm:** Press and hold Extra Button, then lift receiver briefly.

## 📂 Content Management (SD Card)
Ensure your SD card is formatted (FAT32) and structured as follows:
```text
/
├── startup.mp3             (Played on boot)
├── dial_tone.mp3           (Played when pickup)
├── timer_set.mp3           (Ack sound for timer)
├── time_intro.mp3          (Preface for clock)
├── ringtones/              
│   ├── 1.mp3 ... 5.mp3     (Ringtone options)
├── persona_01/             (Persona 1 - Dial 1)
├── persona_02/             (Persona 2 - Dial 2)
├── persona_03/             (Persona 3 - Dial 3)
├── persona_04/             (Persona 4 - Dial 4)
```
*Note: Use `python utils/split_audio.py` to chop long files into individual clips.*

## ⚙️ Web Configuration
Connect to the WiFi Access Point named **Dial-A-Charmer** (No Password) or your local network IP to access settings.

👉 **http://dial-a-charmer.local**

**Basic Settings (Home Page):**
- **Language**: German/English.
- **Volume**: Separate sliders for Handset (Voice) and Ringer (Alarm).
- **Ringtones**: Select and Preview from 5 distinct styles.
- **Repeating Alarm**: Set a daily schedule (Time + Active Days) for your regular wake-up call.
- **Snooze Duration**: Configurable 0-20 minutes.
- **LED Brightness**: Day/Night levels.

**Advanced Settings (/advanced):**
- **WiFi**: Scan and connect logic.
- **Timezone**: Set offset (e.g. UTC+1 Zurich).
- **Half-Duplex**: Enhanced echo cancellation for AI features.
- **AI Settings**: Gemini API Key for dynamic interaction.
- **Firmware Update**: OTA upload for system updates.
