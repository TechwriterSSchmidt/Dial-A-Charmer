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
        ("System Menu. Dial 9 0 to toggle all alarms. 9 1 to skip the next routine alarm. 8 for system status.", "menu_en.mp3"),
        ("All alarms enabled.", "alarms_on_en.mp3"),
        ("All alarms disabled.", "alarms_off_en.mp3"),
        ("Next recurring alarm skipped.", "alarm_skipped_en.mp3"),
        ("Recurring alarm reactivated.", "alarm_active_en.mp3"),
        ("Alarm set.", "timer_set.mp3"), 
        ("Error. Playlist empty.", "error_tone.mp3") 
    ]
    
    # German Prompts
    prompts_de = [
        ("System-Menü. Wähle 9 0 um alle Alarme ein- oder auszuschalten. 9 1 um den nächsten Routine-Wecker zu überspringen. 8 für System Status.", "menu_de.mp3"),
        ("Alle Alarme aktiviert.", "alarms_on_de.mp3"),
        ("Alle Alarme deaktiviert.", "alarms_off_de.mp3"),
        ("Nächster wiederkehrender Wecker übersprungen.", "alarm_skipped_de.mp3"),
        ("Wiederkehrender Wecker wieder aktiv.", "alarm_active_de.mp3"),
        ("Alarm gesetzt.", "timer_set_de.mp3") # Optional variant if needed
    ]
    
    # Generate English
    for text, fname in prompts_en:
        generate_tts_mp3(text, fname, "en")
        
    # Generate German
    for text, fname in prompts_de:
        generate_tts_mp3(text, fname, "de")
        
    # --- Time Announcement Generation (German) ---
    print("\nGenerating Time Announcement Files (German)...")
    time_dir = os.path.join(TARGET_DIR, "..", "time") # TARGET_DIR is system/, so up one level then time/
    if not os.path.exists(time_dir):
        os.makedirs(time_dir)
        
    def generate_time_mp3(text, filename, lang='de'):
         full_path = os.path.join(time_dir, filename)
         # Using same logic but different path, re-implementing briefly or calling generic if I refactored.
         # For simplicity, copy-paste the core logic or call generate_tts_mp3 with a hack.
         # Let's just do the call correctly.
         
         # Hack: modify TARGET_DIR temporarily or pass full path if function supported it.
         # My generate_tts_mp3 function uses TARGET_DIR global. 
         # I should update generate_tts_mp3 to take an optional dir or handle absolute paths.
         
         # But I can't easily change the function definition without replacing the whole file header.
         # So I will just implement the download here.
         print(f"Generating Time: '{text}' -> {filename}...")
         base_url = "https://translate.google.com/translate_tts"
         params = {"ie": "UTF-8", "q": text, "tl": lang, "client": "tw-ob"}
         url = f"{base_url}?{urllib.parse.urlencode(params)}"
         try:
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req, context=ctx) as response:
                data = response.read()
                with open(full_path, 'wb') as f:
                    f.write(data)
            time.sleep(0.2)
         except Exception as e:
            print(f"Error: {e}")

    # Intro & Separator
    generate_time_mp3("Es ist", "intro.mp3")
    generate_time_mp3("Uhr", "uhr.mp3")
    
    # Hours (0-23)
    # Special case for 1: "Ein" instead of "Eins" when followed by "Uhr"
    for h in range(24):
        txt = str(h)
        if h == 1: txt = "Ein" 
        generate_time_mp3(txt, f"h_{h}.mp3")
        
    # Minutes (00-59)
    # 0 is usually not spoken ("14 Uhr"), but if we need it for "14 Uhr Null", we can gen it.
    # Code: "m_05.mp3" for < 10.
    for m in range(60):
        txt = str(m)
        if m < 10:
            fname = f"m_0{m}.mp3"
            if m == 0: 
                continue # Skip 0 minute? Code handles currentMinute==0 by stopping at "Uhr".
            # "Null Eins", "Null Zwei"? Or just "Eins", "Zwei"?
            # Standard German: "Vierzehn Uhr Fünf" (not Null Fünf). 
            # But digital style might be "Null Fünf".
            # Let's use simple number. "5".
        else:
            fname = f"m_{m}.mp3"
        generate_time_mp3(txt, fname)

    print("\nDone! Copy the files from 'sd_card_template/system' and 'time' to your SD Card.")
