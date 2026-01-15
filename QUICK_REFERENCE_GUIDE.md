# Quick Reference Guide - The Atomic Charmer

## 📞 How to Use
1. **Hear a Compliment:**
   - Lift the receiver.
   - **Dial 1:** Donald Trump Style.
   - **Dial 2:** Jacqueline Badran Style (Swiss Politics).
   - **Dial 3:** Yoda Style.
   - **Dial 4:** Neutral / Classic Nerd Style.
   - **Dial 0:** Random Surprise Mix.

2. **Hear the Time:**
   - Lift the receiver and dial **0** (If configured for Time, otherwise random mix).
   - *Note: Default Firmware v1.2 maps '0' to Random Playlist. Time function moved to separate Extra Button if available.*

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
4. **Reboot:** Reboot the Atomic Charmer (Power Cycle) to re-index the playlists.
