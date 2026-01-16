# Quick Reference Guide - Dial-A-Charmer

## 📞 How to Use
1. **Hear a Compliment:**
   - Lift the receiver.
   - **Automatic:** The **Random Surprise Mix** starts immediately.
   - **Dial 1:** Donald Trump Style (Interrupts mix).
   - **Dial 2:** Jacqueline Badran Style (Swiss Politics).
   - **Dial 3:** Yoda Style.
   - **Dial 4:** Neutral / Classic Nerd Style.
   - **Dial 0:** Next Random Surprise Track.

2. **Hear the Time:**
   - Press the **Extra Button** (if available).
   - *Note: Default Firmware v1.3 maps 'Pickup' to Random Playlist and 'Button' to Time.*

3. **Set a Timer:**
   - Keep receiver **ON** the hook.
   - Dial a number (e.g., 5).
   - A timer is set for **5 minutes**.
   - Phone rings when time is up. Lift receiver to stop ringing.

## 📂 SD Card Structure
Ensure your SD card is formatted (FAT32) and structured as follows:
```text
/
├── startup.mp3             (Played on boot)
├── dial_tone.mp3           (Played when pickup)
├── timer_set.mp3           (Ack sound for timer)
├── time_intro.mp3          (Preface for clock)
├── ringtones/              
│   ├── 1.mp3 ... 5.mp3     (Ringtone options)
├── mp3_group_01/           (Trump Compliments - Dial 1)
├── mp3_group_02/           (Badran Compliments - Dial 2)
├── mp3_group_03/           (Yoda Compliments - Dial 3)
├── mp3_group_04/           (Neutral Compliments - Dial 4)
```

## 🛠️ Content Updates
To add new compliments:
1. **Prepare Audio:** Recording or TTS.
2. **Split Files:** If you have one long file, use `python utils/split_audio.py` to chop it into pieces.
3. **Copy to SD:** Save mp3s into the respective folder (`mp3_group_XX`) on the SD card.
4. **Reboot:** Reboot the Dial-A-Charmer (Power Cycle) to re-index the playlists.

## ⚙️ Web Configuration
Connect to the WiFi Access Point named **Dial-A-Charmer** (No Password) to access settings.

Once connected (either directly or if the device is in your home WiFi), open:
👉 **http://dial-a-charmer.local**

**Configurable Options:**
- **WiFi Settings**: Connect Dial-A-Charmer to your home network.
- **Time Settings**: Set your timezone offset.
- **Audio Settings**: 
  - **Handset Volume**: Adjust the volume for voice/compliments (0-42).
  - **Ringer Volume**: Adjust the volume for alarms and timers (0-42).
- **LED Settings**: 
  - **Day Brightness**: 0-42
  - **Night Brightness**: 0-42
  - **Night Schedule**: Start/End hours for auto-dimming.
- **AI Settings**: Gemini API Key for dynamic compliments.
