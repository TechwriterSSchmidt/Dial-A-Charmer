# Quick Reference Guide - Dial-A-Charmer

## 📞 How to Use

1. **Active Mode (Make a Call):**
   - Lift the receiver. Wait for the **Dial Tone**.
   - **Important:** specific content must be dialed within **5 seconds**.
   - *Behavior:* If no number is dialed, a **Busy Signal** plays. Hang up to reset.
   - **Dial 1-5:** Persona Playlists (Configurable).
   - **Dial 11:** Random Mix.

2. **Set a Kitchen Timer:**
   - Keep receiver **ON** the hook.
   - Dial a number (e.g., dial `5`).
   - A timer is set for **5 minutes** (Confirmed by voice).
   - Phone rings when time is up. Lift receiver to stop ringing.

3. **Stop / Snooze Alarm:**
   - **Stop:** Lift receiver and hang up again.
   - **Snooze:** Press the **Extra Button** (if installed).
   - **Signallampe:** Slow warm-white breathing while snooze is active.
   - *Snooze duration is configurable in Web Interface (Default: 5 min).*

4. **Voice Menu & Admin:**
   - **Dial 0:** System Menu Instructions.
   - **While the menu speaks:** Dial `1`-`4`, or `999` for quick actions.

## 📂 Content Management (SD Card)

Ensure your SD card is formatted (FAT32). The `utils/generate_sd_content.py` script automatically creates the required structure:

```text
/
├── system/
│   ├── dial_tone.wav       (Played on pickup)
│   ├── timer_set_de.wav / timer_set_en.wav (Ack sound for timer)
│   ├── beep.wav, click.wav ...
├── time/                   (Voice assets for clock)
│   ├── de/                 (German assets)
│   ├── en/                 (English assets)
├── ringtones/              
│   ├── *.wav               (Ringtone options)
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

👉 <http://dial-a-charmer.local> (Fallback: use the IP announced via Dial `0`, then `4` if mDNS fails)

**Basic Settings (Home Page):**

- **Language**: German/English.
- **Volume**: Separate sliders for Handset (Voice) and Ringer (Alarm).
- **Ringtones**: Select and Preview from 5 distinct styles.
- **Repeating Alarm**: Set a daily schedule (Time + Active Days) for your regular wake-up call.
- **Snooze Duration**: Configurable 0-20 minutes.
- **Signallampe (LED)**: On/Off, Day/Night brightness, and day/night start hours.
- **Night Base Volume**: Base-speaker volume used while night mode is active.

**Night Mode Behavior:**

- When Night Mode is active, base-speaker volume follows the configured night slider.
- Manual activation via voice menu (`0` → `2`) remains active until the next configured day-start hour.
- System prompts remain enabled.

**Night Mode Test (Hardware):**

1. Set `Day Start Hour` in Web UI to a near-future value.
2. Dial `0`, then `2` while the menu speaks.
3. Verify `night_on` announcement and active night settings.
4. Verify automatic return to day mode at configured `Day Start Hour`.
5. Repeat `0` → `2` to disable manually and verify `night_off` announcement.

**Advanced Settings (/advanced):**

- **WiFi**: Scan and connect logic.
- **Timezone**: Set offset (e.g. UTC+1 Zurich).
- **Firmware Update**: OTA upload for system updates.
