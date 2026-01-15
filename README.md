# The-Atomic-Charmer

The Atomic Charmer is a vintage telephone brought back to life with more personality than ever. It announces GNSS-precise time with the confidence of someone whos never been late, doubles as an alarm and timer, and delivers compliments on demandbecause who wouldnt want a phone that cheers them on? Created for my mentor Sandra, whose wisdom and warmth outshine even the fanciest electronics.

## Hardware Components
- **Microcontroller**: [Wemos Lolin D32 Pro (ESP32)](https://www.wemos.cc/en/latest/d32/d32_pro.html)
  - *Note*: Has built-in SD Card slot.
- **Positioning**: M10 GNSS Module (GPS/GLONASS/Galileo)
- **Audio**: Amplifier with Speaker (I2S or DAC)
- **Power**: 3000mAh Battery
- **Input**: 
  - Vintage Telephone Rotary Dial
  - Hook Switch (Gabeltaster)
  - Additional Button
- **Status**: 1x WS2812 LED
- **Enclosure**: Upcycled Vintage Telephone

## Features
1. **Atomic Clock**: Syncs time via GNSS satellites.
2. **Timer**: Set a countdown using the rotary dial.
3. **Alarm Clock**: Wake up to custom sounds or compliments.
4. **Compliment Dispenser**: Dial a specific number.
5. **Captive Portal**: Connect to Wi-Fi 'AtomicCharmer' to configure credentials and timezone.

## User Interface & Control
- **Hook Switch (Gabeltaster)**:
  - *On Hook (Down)*: Idle / Settings Mode. Dialing sets Timer/Alarm.
  - *Off Hook (Up)*: Active Mode. Speak Time. Dialing plays Compliments.
  - *Toggle (Ringing)*: Stop Alarm/Timer.
- **Rotary Dial**: Input numbers.
- **Web Interface**: Configure System.

## Target Audience
Built specially for Sandra, a nerdy and inspiring colleague/mentor.
