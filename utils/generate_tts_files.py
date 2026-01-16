import os
import urllib.request
import urllib.parse
import ssl
import time

# Configuration
TARGET_DIR = r"..\sd_card_template\system"

def ensure_dir(directory):
    if not os.path.exists(directory):
        os.makedirs(directory)

def generate_tts_mp3(text, filename, lang='de'):
    """Download TTS audio from Google Translate"""
    ensure_dir(TARGET_DIR)
    path = os.path.join(TARGET_DIR, filename)
    print(f"Generating ({lang}) '{text}' -> {filename}...")
    
    # Google Translate TTS Endpoint (Unofficial)
    base_url = "https://translate.google.com/translate_tts"
    params = {
        "ie": "UTF-8",
        "q": text,
        "tl": lang,
        "client": "tw-ob"
    }
    url = f"{base_url}?{urllib.parse.urlencode(params)}"
    
    try:
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        
        with urllib.request.urlopen(req, context=ctx) as response:
            data = response.read()
            with open(path, 'wb') as f:
                f.write(data)
        print("Success.")
        time.sleep(0.5) # Be gentle with the API
    except Exception as e:
        print(f"Failed to generate TTS: {e}")

if __name__ == "__main__":
    print("Generating Multi-Language TTS Files for Dial-A-Charmer...")
    
    # English Prompts
    prompts_en = [
        ("Welcome to the Voice Menu. Dial 9 0 to toggle alarms. 9 1 to skip the next alarm. 8 for System Status.", "menu_en.mp3"),
        ("All alarms enabled.", "alarms_on_en.mp3"),
        ("All alarms disabled.", "alarms_off_en.mp3"),
        ("Next alarm will be skipped.", "alarm_skipped_en.mp3"),
        ("Error. Playlist empty.", "error_tone.mp3") # Fallback to TTS if wav missing
    ]
    
    # German Prompts
    prompts_de = [
        ("Willkommen im System-Menü. Wähle 9 0 für Alarm Umschaltung. 9 1 für Alarm überspringen. 8 für System Status.", "menu_de.mp3"),
        ("Alle Alarme aktiviert.", "alarms_on_de.mp3"),
        ("Alle Alarme deaktiviert.", "alarms_off_de.mp3"),
        ("Nächster Alarm wird übersprungen.", "alarm_skipped_de.mp3")
    ]
    
    # Generate English
    for text, fname in prompts_en:
        generate_tts_mp3(text, fname, "en")
        
    # Generate German
    for text, fname in prompts_de:
        generate_tts_mp3(text, fname, "de")
        
    print("\nDone! Copy the files from 'sd_card_template/system' to your SD Card.")
