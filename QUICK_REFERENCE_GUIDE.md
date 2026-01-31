# Quick Reference Guide - Dial-A-Charmer

## 📞 How to Use
1. **Active Mode (Make a Call):**
   - Lift the receiver. Wait for the **Dial Tone**.
   - **Important:** specific content must be dialed within **5 seconds**. 
   - *Behavior:* If no number is dialed, a **Busy Signal** plays. Hang up to reset.
   - **Dial 0:** Gemini AI (if configured).
   - **Dial 1-5:** Persona Playlists (Configurable).
   - **Dial 6:** Random Mix (non-repeating until all tracks played).

2. **Set a Kitchen Timer:**
   - Keep receiver **ON** the hook.
   - Dial a number (e.g., dial `5`).
   - A timer is set for **5 minutes** (Confirmed by voice).
   - Phone rings when time is up. Lift receiver to stop ringing.

3. **Stop / Snooze Alarm:**
   - **Stop:** Lift receiver and hang up again.
   - **Snooze:** Lift receiver but **DO NOT** hang up (put it aside). 
   - *Snooze duration is configurable in Web Interface (Default: 9 min).*

4. **Voice Menu & Admin:**
   - **Dial 9:** System Menu Instructions.
   - **Dial 90:** Toggle **ALL** Alarms (On/Off).
   - **Dial 91:** Skip **Next Repeating Alarm** (e.g. skip tomorrow morning).
   - **Dial 95:** Force **Manual Re-Index** of SD Card (Use after adding files manually).
   - **Dial 8:** Speak full system status (IP, Signal).

## 📂 Content Management (SD Card)
Ensure your SD card is formatted (FAT32). The `utils/generate_sd_content.py` script automatically creates the required structure:
```text
/
├── system/
│   ├── dial_tone.wav       (Played on pickup)
│   ├── timer_set.mp3       (Ack sound for timer)
│   ├── beep.wav, click.wav ...
├── time/                   (Voice assets for clock)
│   ├── de/                 (German assets)
│   ├── en/                 (English assets)
├── ringtones/              
│   ├── 1.wav ... 5.wav     (Ringtone options)
├── playlists/              (Auto-generated playlists)
├── persona_01/             (Persona 1 - Dial 1)
├── persona_02/             (Persona 2 - Dial 2)
├── persona_03/             (Persona 3 - Dial 3)
├── persona_04/             (Persona 4 - Dial 4)
├── persona_05/             (Persona 5 - Dial 5, Fortune)
│   └── fortune.txt         (Enables Fortune mode detection)
```
*Note: Run `python utils/generate_sd_content.py` to generate system assets.*

## ⚙️ Web Configuration
Connect to the WiFi Access Point named **Dial-A-Charmer** (No Password) or your local network IP to access settings.

👉 **http://dial-a-charmer.local** (Fallback: use the IP announced via Dial `8` if mDNS fails)

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
