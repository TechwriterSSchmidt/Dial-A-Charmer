import os
import wave
import math
import struct
import random
import urllib.request
import urllib.parse
import ssl
import subprocess
import shutil
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

# --- CONFIGURATION ---
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR) # Parent of utils/
SD_TEMPLATE_DIR = os.path.join(PROJECT_ROOT, "sd_card_template")
FFMPEG_EXE = os.path.join(SCRIPT_DIR, "ffmpeg.exe")

SAMPLE_RATE = 44100
AMPLITUDE = 16000  # 16-bit PCM (max 32767)

# Directory Structure to Enforce
REQUIRED_DIRS = [
    "system",
    "time",
    "ringtones",
    "playlists",
    "persona_01",
    "persona_02",
    "persona_03",
    "persona_04",
    "persona_05"
]

# --- TTS CONFIGURATION (TEXTS) ---
TTS_PROMPTS = {
    # English
    "en": [
        ("System Menu. Dial 9 0 to toggle all alarms. 9 1 to skip the next routine alarm. 8 for system status.", "menu_en.mp3"),
        ("All alarms enabled.", "alarms_on_en.mp3"),
        ("All alarms disabled.", "alarms_off_en.mp3"),
        ("Next recurring alarm skipped.", "alarm_skipped_en.mp3"),
        ("Recurring alarm reactivated.", "alarm_active_en.mp3"),
        ("Alarm set.", "timer_set.mp3"),
        ("Error. Playlist empty.", "error_msg_en.mp3"),
        ("Timer set for:", "timer_confirm_en.mp3"),
        ("Alarm set for:", "alarm_confirm_en.mp3"),
        ("Timer cancelled.", "timer_deleted_en.mp3"),
        ("Alarm deleted.", "alarm_deleted_en.mp3")
    ],
    # German
    "de": [
        ("System-Menü. Wähle 9 0 um alle Alarme ein- oder auszuschalten. 9 1 um den nächsten Routine-Wecker zu überspringen. 8 für System Status.", "menu_de.mp3"),
        ("Alle Alarme aktiviert.", "alarms_on_de.mp3"),
        ("Alle Alarme deaktiviert.", "alarms_off_de.mp3"),
        ("Nächster wiederkehrender Wecker übersprungen.", "alarm_skipped_de.mp3"),
        ("Wiederkehrender Wecker wieder aktiv.", "alarm_active_de.mp3"),
        ("Alarm gesetzt.", "timer_set_de.mp3"),
        ("Warnung. Energiezellen kritisch.", "battery_crit.mp3"),
        ("System bereit.", "system_ready.mp3"),
        ("Timer gesetzt auf:", "timer_confirm_de.mp3"),
        ("Wecker gestellt auf:", "alarm_confirm_de.mp3"),
        ("Timer beendet.", "timer_deleted_de.mp3"),
        ("Wecker gelöscht.", "alarm_deleted_de.mp3")
    ]
}

TIME_CONFIG = {
    "de": {
        "intro": "Es ist jetzt:",
        "divider": "Uhr",
        "h_one": "Ein" # Special case for 1:00
    },
    "en": {
        "intro": "It is now:",
        "divider": "O'Clock",
        "h_one": "One"
    }
}

CALENDAR_CONFIG = {
    "de": {
        "weekdays": ["Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"],
        "months": ["Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"],
        "date_intro": "Heute ist",
        "dst": ["Es ist Winterzeit", "Es ist Sommerzeit"]
    },
    "en": {
        "weekdays": ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"],
        "months": ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"],
        "date_intro": "Today is",
        "dst": ["It is Winter time", "It is Summer time"]
    }
}

# --- HELPER FUNCTIONS ---

def ensure_folder_structure():
    """Creates all required directories."""
    print(f"Creating SD Card Structure in '{SD_TEMPLATE_DIR}'...")
    if not os.path.exists(SD_TEMPLATE_DIR):
        os.makedirs(SD_TEMPLATE_DIR)
    
    for d in REQUIRED_DIRS:
        path = os.path.join(SD_TEMPLATE_DIR, d)
        if not os.path.exists(path):
            os.makedirs(path)
            print(f"  + Created {d}/")
        else:
            print(f"  . Exists {d}/")
            
    # Language subfolders for time
    for lang in ["de", "en"]:
        path = os.path.join(SD_TEMPLATE_DIR, "time", lang)
        if not os.path.exists(path):
            os.makedirs(path)
            print(f"  + Created time/{lang}/")

def generate_tts_mp3(text, filename, lang='de', output_subdir="system"):
    """
    Generates TTS audio, Thread-Safe wrapper.
    """
    target_path = os.path.join(SD_TEMPLATE_DIR, output_subdir, filename)
    
    # Check if exists (Simple Cache)
    if os.path.exists(target_path) and os.path.getsize(target_path) > 100:
        return # Skip if already there

    # 1. Try Piper TTS Local - NOT THREAD SAFE usually, skip for parallel
    # pipe logic removed for simplicity in threaded context to avoid fork issues
    # Fallback directly to Google TTS for this script version

    # 2. Google TTS
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
        
        # Randomize User-Agent slightly to avoid sticky load balancer issues
        user_agents = [
            'Mozilla/5.0 (Windows NT 10.0; Win64; x64)',
            'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)',
            'Mozilla/5.0 (X11; Linux x86_64)'
        ]
        ua = random.choice(user_agents)
        
        req = urllib.request.Request(url, headers={'User-Agent': ua})
        
        # Reduced timeout to 5s to fail fast (avoid hanging threads)
        with urllib.request.urlopen(req, context=ctx, timeout=5) as response:
            data = response.read()
            with open(target_path, 'wb') as f:
                f.write(data)
        
        # Progress indicator
        print(f".", end="", flush=True) 
    except Exception as e:
        print(f"\n  [ERROR] TTS Failed for '{filename}': {e}")


def save_wav(filename, samples, output_subdir="system"):
    """Saves a list of samples/integers to a WAV file."""
    path = os.path.join(SD_TEMPLATE_DIR, output_subdir, filename)
    # print(f"  WAV: Generating {filename}...")
    
    with wave.open(path, 'w') as wav_file:
        wav_file.setnchannels(1) # Mono
        wav_file.setsampwidth(2) # 16-bit
        wav_file.setframerate(SAMPLE_RATE)
        
        packed_data = bytearray()
        for s in samples:
            # Clip
            s = max(min(int(s), 32767), -32768)
            packed_data.extend(struct.pack('<h', s))
            
        wav_file.writeframes(packed_data)

def generate_sine_wave(frequency, duration_ms):
    """Generates pure sine samples."""
    num_samples = int(SAMPLE_RATE * duration_ms / 1000.0)
    samples = []
    for i in range(num_samples):
        t = float(i) / SAMPLE_RATE
        val = AMPLITUDE * math.sin(2 * math.pi * frequency * t)
        samples.append(val)
    return samples

def generate_silence(duration_ms):
    num_samples = int(SAMPLE_RATE * duration_ms / 1000.0)
    return [0] * num_samples

def generate_complex_tone(filename, frequencies, duration_sec, pulse_on_ms=0, pulse_off_ms=0):
    """Generates multi-frequency tones with optional pulsing (busy signals, etc)."""
    if not isinstance(frequencies, list):
        frequencies = [frequencies]
    
    print(f"  Tone: {filename} (Freqs: {frequencies}, Dur: {duration_sec}s)")
    
    num_samples = int(SAMPLE_RATE * duration_sec)
    samples = []
    
    # Pulse Logic Pre-calcs
    pulse_period_samples = 0
    on_samples = 0
    if pulse_on_ms > 0:
        pulse_period_samples = int(SAMPLE_RATE * (pulse_on_ms + pulse_off_ms) / 1000.0)
        on_samples = int(SAMPLE_RATE * pulse_on_ms / 1000.0)
    
    for i in range(num_samples):
        # Pulsing
        is_silent = False
        if pulse_period_samples > 0:
            cycle_pos = i % pulse_period_samples
            if cycle_pos >= on_samples:
                is_silent = True
        
        if is_silent:
            final_sample = 0
        else:
            val = 0
            t = i
            # Mix Frequencies
            for freq in frequencies:
                val += math.sin(2.0 * math.pi * freq * i / SAMPLE_RATE)
            
            # Avg & Scale
            val = (val / len(frequencies)) * AMPLITUDE
            
            # Add Subtle 50Hz Hum for "Authenticity"
            hum = (AMPLITUDE * 0.05) * math.sin(2.0 * math.pi * 50 * i / SAMPLE_RATE)
            final_sample = val + hum
            
        samples.append(final_sample)
        
    save_wav(filename, samples)

# --- SPECIFIC GENERATORS ---

def make_ui_sounds():
    print("Generating UI Sounds (Computing, Battery, Beeps)...")
    
    # 1. Computing Sound (Retro Blips)
    audio = []
    total_duration = 3000
    current_time = 0
    while current_time < total_duration:
        dur = random.randint(30, 80)
        freq = random.randint(800, 2500)
        
        # Simple Square/Sine mix
        num_s = int(SAMPLE_RATE * dur / 1000.0)
        for i in range(num_s):
            t = float(i) / SAMPLE_RATE
            if random.choice([True, False]): # Square
                val = AMPLITUDE if math.sin(2 * math.pi * freq * t) > 0 else -AMPLITUDE
            else: # Sine
                val = AMPLITUDE * math.sin(2 * math.pi * freq * t)
            audio.append(val)
            
        pause = random.randint(5, 20)
        audio.extend(generate_silence(pause))
        current_time += (dur + pause)
    save_wav("computing.wav", audio)
    
    # 2. Battery Low (Slide Down)
    audio = []
    samples_len = int(SAMPLE_RATE * 1.0) # 1 sec
    start_f, end_f = 800.0, 100.0
    phase = 0.0
    for i in range(samples_len):
        prog = i / samples_len
        freq = start_f * (1.0 - prog) + end_f * prog
        phase += freq / SAMPLE_RATE
        val = AMPLITUDE * math.sin(2 * math.pi * phase)
        vol = 1.0 - (prog * prog) # Fade out
        audio.append(val * vol)
    
    # Thump
    audio.extend(generate_silence(200))
    # simplistic thump
    thump_len = int(SAMPLE_RATE * 0.4)
    for i in range(thump_len):
        val = AMPLITUDE * 0.8 * math.sin(2*math.pi*80*i/SAMPLE_RATE)
        if val > 0: val = AMPLITUDE * 0.8 # distort to square-ish
        else: val = -AMPLITUDE * 0.8
        audio.append(val)
    save_wav("battery_low.wav", audio)
    
    # 3. Beep (Clean)
    save_wav("beep.wav", generate_sine_wave(1200, 50))
    
    # 4. Error Tone (Dissonant Buzz)
    samples = []
    dur = 400
    num_s = int(SAMPLE_RATE * dur / 1000.0)
    f1, f2 = 150, 180
    for i in range(num_s):
        t = float(i) / SAMPLE_RATE
        # Twisted waves
        v1 = math.sin(2*math.pi*f1*t) + 0.5*math.sin(2*math.pi*f1*2*t)
        v2 = math.sin(2*math.pi*f2*t) + 0.5*math.sin(2*math.pi*f2*2*t)
        samples.append(AMPLITUDE * 0.5 * (v1+v2))
    save_wav("error_tone.wav", samples)

    # 5. Mechanical Click (Rotary Feedback)
    # Short burst of high frequency + noise to simulate mechanical switch
    click_audio = []
    click_dur_ms = 40 
    click_s = int(SAMPLE_RATE * click_dur_ms / 1000.0)

    # Simple model: Decaying sine wave at 2.5kHz mixed with noise
    for i in range(click_s):
        t = float(i) / SAMPLE_RATE
        progress = i / click_s
        envelope = math.exp(-15 * progress)  # Sharp decay
        
        sine_comp = math.sin(2 * math.pi * 2500 * t)
        noise_comp = random.uniform(-1, 1)
        
        # Mix mostly noise for the "clack" sound
        val = (0.3 * sine_comp + 0.8 * noise_comp) * envelope * AMPLITUDE
        click_audio.append(val)
        
    save_wav("click.wav", click_audio)

    # Fallback Alarm (Penetrant Square Wave)
    # Used when file access fails
    fallback_audio = []
    dur_ms = 1000
    num = int(SAMPLE_RATE * dur_ms / 1000.0)
    for i in range(num):
        # 800Hz / 1200Hz alternating every 250ms
        t = i / SAMPLE_RATE
        freq = 800 if int(t * 4) % 2 == 0 else 1200
        # Square wave
        val = AMPLITUDE * (1.0 if math.sin(2 * math.pi * freq * t) > 0 else -1.0)
        fallback_audio.append(val)
    save_wav("fallback_alarm.wav", fallback_audio, "system")

def make_telephony_tones():
    print("Generating Telephony Tones...")
    # Dial Tone DE (425Hz)
    generate_complex_tone("dialtone_0.wav", [425], 10)
    # Dial Tone US (350+440Hz)
    generate_complex_tone("dialtone_1.wav", [350, 440], 10)
    # Busy Tone (425Hz, 480ms on/off)
    generate_complex_tone("busy_tone.wav", [425], 10, 480, 480)

def make_all_tts():
    print("Generating System TTS (Parallel)...")
    
    tasks = [] # List of (text, filename, lang, subdir)

    # 1. Collect all Tasks
    
    # System Prompts
    for lang, prompts in TTS_PROMPTS.items():
        for text, fname in prompts:
            tasks.append((text, fname, lang, "system"))
            
    # Time Prompts (DE & EN)
    for lang in ["de", "en"]:
        cfg = TIME_CONFIG[lang]
        subdir = os.path.join("time", lang)
        
        # Intro & Divider
        tasks.append((cfg["intro"], "intro.mp3", lang, subdir))
        tasks.append((cfg["divider"], "uhr.mp3", lang, subdir))
        
        # Hours 0-23
        for h in range(24):
            txt = str(h)
            if h == 1: txt = cfg["h_one"]
            tasks.append((txt, f"h_{h}.mp3", lang, subdir))
            
        # Minutes 00-59
        for m in range(60):
            txt = str(m)
            if lang == "en" and m < 10 and m > 0: txt = f"Oh {m}"
            fname = f"m_{m}.mp3"
            if m < 10: fname = f"m_0{m}.mp3"
            tasks.append((txt, fname, lang, subdir))
            
    # Calendar TTS
    for lang in ["de", "en"]:
        c_cfg = CALENDAR_CONFIG[lang]
        subdir = os.path.join("time", lang)
        
        tasks.append((c_cfg["date_intro"], "date_intro.mp3", lang, subdir))
        tasks.append((c_cfg["dst"][0], "dst_winter.mp3", lang, subdir))
        tasks.append((c_cfg["dst"][1], "dst_summer.mp3", lang, subdir))
        
        for i, wd in enumerate(c_cfg["weekdays"]):
            tasks.append((wd, f"wday_{i}.mp3", lang, subdir))
            
        for i, mon in enumerate(c_cfg["months"]):
            tasks.append((mon, f"month_{i}.mp3", lang, subdir))
            
        for y in range(2024, 2036):
            tasks.append((str(y), f"year_{y}.mp3", lang, subdir))
            
        for d in range(1, 32):
            txt = str(d)
            if lang == "de": txt = f"Der {d}." 
            else:
                if 10 < d < 20: suffix = "th"
                else:
                    last = d % 10
                    if last == 1: suffix = "st"
                    elif last == 2: suffix = "nd"
                    elif last == 3: suffix = "rd"
                    else: suffix = "th"
                txt = f"The {d}{suffix}"
            tasks.append((txt, f"day_{d}.mp3", lang, subdir))

    # 2. Execute Tasks in ThreadPool
    total_tasks = len(tasks)
    print(f"  Queued {total_tasks} TTS items. Processing with 16 threads...")
    
    # Increased threads to 16 for aggressive parallelization
    with ThreadPoolExecutor(max_workers=16) as executor:
        futures = []
        for t in tasks:
            futures.append(executor.submit(generate_tts_mp3, t[0], t[1], t[2], t[3]))
        
        # Wait for completion
        completed = 0
        for _ in as_completed(futures):
            completed += 1
            if completed % 20 == 0:
                print(f" ({completed}/{total_tasks}) ", end="", flush=True)

    # Generate Silence (Fast, no TTS)
    save_wav("silence.wav", generate_silence(200), "time")



def make_startup_music():
    print("Generating Startup Sound (Ambient Swell)...")
    path_wav = os.path.join(SD_TEMPLATE_DIR, "system", "startup.wav") # temp
    path_mp3 = os.path.join(SD_TEMPLATE_DIR, "system", "startup.mp3")
    
    # Chord: Eb Major Add9 (Eb, Bb, G, F...)
    freqs = [155.56, 233.08, 311.13, 392.00, 466.16, 622.25]
    duration = 5.0
    num_samples = int(SAMPLE_RATE * duration)
    samples = []
    
    for i in range(num_samples):
        # Env
        prog = i / num_samples
        # Attack 10%, Sustain, Release 30%
        if prog < 0.1: vol = prog / 0.1
        elif prog > 0.7: vol = 1.0 - ((prog - 0.7) / 0.3)
        else: vol = 1.0
        
        val = 0
        for f in freqs:
            # Slight detuning for warmth?
            val += math.sin(2 * math.pi * f * i / SAMPLE_RATE)
        
        val = (val / len(freqs)) * AMPLITUDE * vol
        samples.append(val)
        
    # Write WAV
    with wave.open(path_wav, 'w') as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(SAMPLE_RATE)
        packed = bytearray()
        for s in samples:
            packed.extend(struct.pack('<h', int(s)))
        wav_file.writeframes(packed)
        
    # Convert to MP3 if ffmpeg exists
    ffmpeg_cmd = "ffmpeg"
    if os.path.exists(FFMPEG_EXE):
        ffmpeg_cmd = FFMPEG_EXE
    
    if shutil.which(ffmpeg_cmd) or os.path.exists(ffmpeg_cmd):
        try:
            print("  Converting startup.wav -> startup.mp3 ...")
            subprocess.run([ffmpeg_cmd, "-y", "-i", path_wav, path_mp3], 
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            os.remove(path_wav)
            print("  Done.")
        except Exception:
            print("  [WARN] FFmpeg failed. Keeping startup.wav.")
    else:
        print("  [WARN] FFmpeg not found. 'startup.wav' created instead of mp3.")

def download_fonts():
    print("Checking Fonts...")
    font_dir = os.path.join(SD_TEMPLATE_DIR, "system", "fonts")
    os.makedirs(font_dir, exist_ok=True)
    
    fonts = [
        ("https://github.com/google/fonts/raw/main/ofl/zentokyozoo/ZenTokyoZoo-Regular.ttf", "ZenTokyoZoo-Regular.ttf"),
        ("https://github.com/google/fonts/raw/main/ofl/pompiere/Pompiere-Regular.ttf", "Pompiere-Regular.ttf")
    ]
    
    for url, fname in fonts:
        path = os.path.join(font_dir, fname)
        if not os.path.exists(path):
            print(f"  Downloading {fname}...")
            try:
                subprocess.run(["curl", "-L", "-o", path, url], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                print("    Done.")
            except Exception as e:
                print(f"    [ERR] Failed to download font: {e}")
        else:
            print(f"  {fname} already exists.")

def generate_fortune_mp3s():
    """Reads fortune_examples.txt and generates 50 random MP3s for Persona 5"""
    text_file = os.path.join(SCRIPT_DIR, "fortune_examples.txt")
    output_dir = os.path.join(SD_TEMPLATE_DIR, "persona_05")
    
    if not os.path.exists(text_file):
        print(f"Skipping Fortune Gen: {text_file} not found.")
        return

    print("Generating Fortune Cookies (MP3)...")
    
    with open(text_file, "r", encoding="utf-8") as f:
        lines = [l.strip() for l in f if len(l.strip()) > 5]
    
    # Use ALL quotes as requested
    selected = lines
    print(f"Generating {len(selected)} Fortune Cookies with 16 threads...")
    
    tasks = []
    for i, line in enumerate(selected):
        # Default to German if no prefix
        lang = "de"
        text = line
        
        # Check for prefixes "DE|" or "EN|"
        if "|" in line:
            parts = line.split("|", 1)
            if len(parts) == 2:
                prefix = parts[0].strip().lower()
                if prefix in ["de", "en"]:
                    lang = prefix
                    text = parts[1].strip()
        
        filename = f"fortune_{i+1:03d}.mp3"
        tasks.append((text, filename, lang, "persona_05"))
        
    with ThreadPoolExecutor(max_workers=16) as executor:
        futures = [executor.submit(generate_tts_mp3, *t) for t in tasks]
        completed = 0
        for _ in as_completed(futures):
            completed += 1
            if completed % 10 == 0:
                print(f" ({completed}/{len(selected)}) ", end="", flush=True)
    
    # Also create fortune.txt for automatic phonebook naming (Triggers "Fortune" name in firmware)
    with open(os.path.join(output_dir, "fortune.txt"), "w") as f:
        f.write("System: Fortune Cookie")

# --- MAIN EXECUTION ---

if __name__ == "__main__":
    print(f"=== ChainJuicer / AtomicCharmer SD Asset Generator ===")
    print(f"Output: {SD_TEMPLATE_DIR}\n")
    
    ensure_folder_structure()
    
    make_ui_sounds()       # Beeps, Blips
    make_telephony_tones() # Dial, Busy
    
    print("--- Expect ~30-60s for TTS downloads ---")
    make_all_tts()         # System & Time
    
    make_startup_music()   # Startup pad
    
    download_fonts()       # Added Fonts

    generate_fortune_mp3s() # Added Fortune Cookies
    
    print("\n=== GENERATION COMPLETE ===")
    print(f"Copy the contents of '{SD_TEMPLATE_DIR}' to your SD Card.")

