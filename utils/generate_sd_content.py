import os
import hashlib
import shutil
import re
import random
import concurrent.futures
import threading
import time
import subprocess
from pathlib import Path
from gtts import gTTS
from pydub import AudioSegment
from pydub.generators import Sine, Square, Sawtooth, Pulse

# Configuration
PROJECT_ROOT = Path(__file__).parent.parent
SOURCE_DIR = PROJECT_ROOT / "compliments"
CACHE_DIR = SOURCE_DIR / "audio_cache"
SD_ROOT = PROJECT_ROOT / "sd_card_content"
AUDIO_RATE = 44100  # 44.1kHz for ESP32 compatibility

# Piper Configuration
PIPER_BIN = PROJECT_ROOT / "utils/piper/piper/piper"
PIPER_VOICES_DIR = PROJECT_ROOT / "utils/piper_voices"
USE_PIPER = False # Will be auto-detected

# Available Accents (TLDs) for gTTS
ACCENTS_EN = ['com', 'co.uk', 'com.au', 'ca', 'co.in', 'ie', 'co.za']
ACCENTS_DE = ['de'] # 'fr', 'us' etc produce strong foreign accents, 'de' is standard

# Piper Models
PIPER_MODELS_EN = [
    "en_GB-cori-high.onnx",
    "en_GB-alan-medium.onnx",
    "en_GB-alba-medium.onnx",
    "en_GB-aru-medium.onnx",
    "en_GB-jenny_dioco-medium.onnx"
]
PIPER_MODELS_DE = [
    "de_DE-thorsten-high.onnx"
]

# System Prompts Configuration
# Format: "path/to/file.wav": ("Text to speak", "Language Code", "Specific Model Name or None")
SYSTEM_PROMPTS = {
    # English System Messages (Jenny)
    "system/system_ready_en.wav": ("System ready.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/time_unavailable_en.wav": ("Time synchronization failed.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/timer_stopped_en.wav": ("Timer cancelled.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/alarm_stopped_en.wav": ("Alarm cancelled.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/reindex_warning_en.wav": ("Please wait. Re-indexing content.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/alarm_active_en.wav": ("Alarm active.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/alarm_confirm_en.wav": ("Alarm confirmed.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/alarm_deleted_en.wav": ("Alarm deleted.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/alarm_skipped_en.wav": ("Alarm skipped.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/alarms_off_en.wav": ("Alarms disabled.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/alarms_on_en.wav": ("Alarms enabled.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/battery_crit_en.wav": ("Battery critical. Shutting down.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/error_msg_en.wav": ("An error occurred.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/menu_en.wav": ("Main Menu.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/timer_confirm_en.wav": ("Timer started.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/timer_deleted_en.wav": ("Timer deleted.", "en", "en_GB-jenny_dioco-medium.onnx"),
    "system/timer_set_en.wav": ("Timer set.", "en", "en_GB-jenny_dioco-medium.onnx"),
    
    # German System Messages (Thorsten)
    "system/system_ready_de.wav": ("System bereit.", "de", "de_DE-thorsten-high.onnx"),
    "system/time_unavailable_de.wav": ("Zeit-Synchronisation fehlgeschlagen.", "de", "de_DE-thorsten-high.onnx"),
    "system/timer_stopped_de.wav": ("Timer abgebrochen.", "de", "de_DE-thorsten-high.onnx"),
    "system/alarm_stopped_de.wav": ("Alarm abgebrochen.", "de", "de_DE-thorsten-high.onnx"),
    "system/reindex_warning_de.wav": ("Bitte warten. Inhalte werden neu indexiert.", "de", "de_DE-thorsten-high.onnx"),
    "system/alarm_active_de.wav": ("Alarm aktiv.", "de", "de_DE-thorsten-high.onnx"),
    "system/alarm_confirm_de.wav": ("Alarm bestätigt.", "de", "de_DE-thorsten-high.onnx"),
    "system/alarm_deleted_de.wav": ("Alarm gelöscht.", "de", "de_DE-thorsten-high.onnx"),
    "system/alarm_skipped_de.wav": ("Alarm übersprungen.", "de", "de_DE-thorsten-high.onnx"),
    "system/alarms_off_de.wav": ("Alarme deaktiviert.", "de", "de_DE-thorsten-high.onnx"),
    "system/alarms_on_de.wav": ("Alarme aktiviert.", "de", "de_DE-thorsten-high.onnx"),
    "system/battery_crit_de.wav": ("Batterie kritisch. System wird heruntergefahren.", "de", "de_DE-thorsten-high.onnx"),
    "system/error_msg_de.wav": ("Ein Fehler ist aufgetreten.", "de", "de_DE-thorsten-high.onnx"),
    "system/menu_de.wav": ("Hauptmenü.", "de", "de_DE-thorsten-high.onnx"),
    "system/timer_confirm_de.wav": ("Timer gestartet.", "de", "de_DE-thorsten-high.onnx"),
    "system/timer_deleted_de.wav": ("Timer gelöscht.", "de", "de_DE-thorsten-high.onnx"),
    "system/timer_set_de.wav": ("Timer gesetzt.", "de", "de_DE-thorsten-high.onnx"),
}

# Static Files to Copy (Source Path relative to template, Destination relative to SD Root)
STATIC_COPY_MAP = {
    # Sound Effects
    "system/beep.wav": "system/beep.wav",
    "system/click.wav": "system/click.wav",
    "system/dialtone_0.wav": "system/dialtone_0.wav",
    "system/dialtone_1.wav": "system/dialtone_1.wav",
    "system/busy_tone.wav": "system/busy_tone.wav",
    "system/computing.wav": "system/computing.wav",
    "system/error_tone.wav": "system/error_tone.wav",
    "system/startup.wav": "system/startup.wav",
    "system/fallback_alarm.wav": "system/fallback_alarm.wav",
    "system/battery_low.wav": "system/battery_low.wav",
}

# Time Announcements (Range Definitions)
TIME_CONFIG = {
    "hours": range(0, 24), # 0-23
    "minutes": range(0, 60), # 0-59
    "days": range(1, 32), # 1-31
    "months": range(0, 12), # 0-11
    "weekdays": range(0, 7), # 0-6
    "years": range(2025, 2036) # 2025-2035
}

def generate_time_announcements():
    """Generates time announcement files for DE and EN."""
    
    # German (Thorsten)
    model_de = "de_DE-thorsten-high.onnx"
    # English (Jenny)
    model_en = "en_GB-jenny_dioco-medium.onnx"
    
    base_dir = SD_ROOT / "time"
    (base_dir / "de").mkdir(parents=True, exist_ok=True)
    (base_dir / "en").mkdir(parents=True, exist_ok=True)
    
    tasks = []

    # Helper to add task
    def add_task(text, lang, path, model):
        tasks.append((text, lang, path, model))

    print("  [PLAN] Preparing Time Announcements...")

    # Hours
    for h in TIME_CONFIG["hours"]:
        add_task(f"{h}", "de", base_dir / f"de/h_{h}.wav", model_de)
        add_task(f"{h}", "en", base_dir / f"en/h_{h}.wav", model_en)

    # Minutes
    for m in TIME_CONFIG["minutes"]:
        # German: 0-9 can cover "00"-"09" if needed, but usually just numbers.
        # Format "m_XX.wav" (always 2 digits? or 1? template had m_00)
        # We will generate m_00 to m_59
        fname = f"m_{m:02d}.wav"
        add_task(f"{m}", "de", base_dir / f"de/{fname}", model_de)
        add_task(f"{m}", "en", base_dir / f"en/{fname}", model_en)

    # Days
    for d in TIME_CONFIG["days"]:
        add_task(f"{d}.", "de", base_dir / f"de/day_{d}.wav", model_de) # Ordinal in DE? "erster"? 
        # Actually Piper might read "1." as "Erster". Let's try explicit text if needed.
        # But simple number might suffice for "Der erste"? No, usually "Der Eins" is wrong.
        # Let's trust Piper's norm for "1."
        add_task(f"{d}", "en", base_dir / f"en/day_{d}.wav", model_en) # "1st"?

    # Months
    months_de = ["Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"]
    months_en = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"]
    
    for i in TIME_CONFIG["months"]:
        add_task(months_de[i], "de", base_dir / f"de/month_{i}.wav", model_de)
        add_task(months_en[i], "en", base_dir / f"en/month_{i}.wav", model_en)

    # Weekdays
    wday_de = ["Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"]
    wday_en = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"]
    
    for i in TIME_CONFIG["weekdays"]:
         add_task(wday_de[i], "de", base_dir / f"de/wday_{i}.wav", model_de)
         add_task(wday_en[i], "en", base_dir / f"en/wday_{i}.wav", model_en)

    # Years
    for y in TIME_CONFIG["years"]:
        add_task(f"{y}", "de", base_dir / f"de/year_{y}.wav", model_de)
        add_task(f"{y}", "en", base_dir / f"en/year_{y}.wav", model_en)

    # Specials
    add_task("Uhr", "de", base_dir / "de/uhr.wav", model_de)
    add_task("Es ist", "de", base_dir / "de/intro.wav", model_de)
    add_task("Heute ist", "de", base_dir / "de/date_intro.wav", model_de)
    add_task("Sommerzeit", "de", base_dir / "de/dst_summer.wav", model_de)
    add_task("Winterzeit", "de", base_dir / "de/dst_winter.wav", model_de)
    
    add_task("It is", "en", base_dir / "en/intro.wav", model_en)
    add_task("Today is", "en", base_dir / "en/date_intro.wav", model_en)
    add_task("Summer time", "en", base_dir / "en/dst_summer.wav", model_en)
    add_task("Winter time", "en", base_dir / "en/dst_winter.wav", model_en)

    return tasks

def check_piper_status():
    """Checks if Piper is installed and voices are available."""
    global USE_PIPER
    if PIPER_BIN.exists() and PIPER_VOICES_DIR.exists():
        # Check for Jenny (System) and Thorsten (DE) at minimum
        has_jenny = (PIPER_VOICES_DIR / "en_GB-jenny_dioco-medium.onnx").exists()
        has_thorsten = (PIPER_VOICES_DIR / "de_DE-thorsten-high.onnx").exists()
        
        if has_jenny and has_thorsten:
            USE_PIPER = True
            print("[INFO] Piper TTS detected. Google TTS disabled (Piper only mode).")
        else:
            print("[INFO] Piper binary found, but key Voice Models (Jenny/Thorsten) missing.")
            print("       Run 'python3 utils/setup_piper.py' to download them.")
            USE_PIPER = False
            exit(1) # Strict mode
    else:
        print("[ERROR] Piper not detected. Please install Piper to proceed.")
        print("        Check 'utils/piper/' and 'utils/piper_voices/'")
        exit(1) # Strict mode

def generate_with_piper(text, lang, output_path, model_name=None):
    """Generates audio using local Piper binary."""
    
    if not model_name:
        # Default selection if not specified
        if lang == 'de':
             model_name = random.choice(PIPER_MODELS_DE)
        else:
             model_name = random.choice(PIPER_MODELS_EN)
            
    model_path = PIPER_VOICES_DIR / model_name
    
    if not model_path.exists():
        # Fallback to Jenny or Thorsten High if random selection failed?
        if lang == 'de' and (PIPER_VOICES_DIR / "de_DE-thorsten-high.onnx").exists():
            model_path = PIPER_VOICES_DIR / "de_DE-thorsten-high.onnx"
        elif (PIPER_VOICES_DIR / "en_GB-jenny_dioco-medium.onnx").exists():
             model_path = PIPER_VOICES_DIR / "en_GB-jenny_dioco-medium.onnx"
        else:
             raise FileNotFoundError(f"Model {model_name} not found.")
    
    cmd = [
        str(PIPER_BIN),
        "--model", str(model_path),
        "--output_file", str(output_path),
        "--quiet"
    ]
    
    # Piper expects text via stdin
    process = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    stdout, stderr = process.communicate(input=text.encode('utf-8'))
    
    if process.returncode != 0:
        raise Exception(f"Piper failed: {stderr.decode()}")
    
    return True


def get_file_hash(text):
    """Returns MD5 hash of the text content."""
    return hashlib.md5(text.strip().encode('utf-8')).hexdigest()

def ensure_dirs():
    """Ensure necessary directories exist."""
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    SD_ROOT.mkdir(parents=True, exist_ok=True)

def parse_filename(filename):
    """
    Parses 'Category_Lang.txt' or 'Category.txt'.
    Returns (category, lang).
    """
    stem = Path(filename).stem
    parts = stem.split('_')
    
    # Check if last part looks like a language code (2 chars)
    if len(parts) > 1 and len(parts[-1]) == 2:
        lang = parts[-1].lower()
        category = "_".join(parts[:-1])
    else:
        lang = "en"
        category = stem
        
    return category, lang

print_lock = threading.Lock()

def safe_print(*args, **kwargs):
    with print_lock:
        print(*args, **kwargs)

def format_time(seconds):
    """Formats seconds into MM:SS"""
    m, s = divmod(int(seconds), 60)
    return f"{m}:{s:02d}"

def process_single_file(args):
    """
    Worker function to generate a single audio file.
    """
    line, lang, category, wav_path, line_hash = args
    
    try:
        # Deterministic Randomization based on Hash using local Random instance (Thread-safe)
        seed_val = int(line_hash, 16)
        rng = random.Random(seed_val)
        
        temp_base = wav_path.with_suffix('.temp')
        
        # Piper Generation
        wav_temp = temp_base.with_suffix('.wav')
        
        # Deterministic Model Selection
        chosen_model = None
        if lang == 'de':
            chosen_model = rng.choice(PIPER_MODELS_DE)
        elif lang == 'en':
            chosen_model = rng.choice(PIPER_MODELS_EN)
        
        try:
            generate_with_piper(line, lang, wav_temp, model_name=chosen_model)
            sound = AudioSegment.from_wav(str(wav_temp))
            if wav_temp.exists(): os.remove(wav_temp)
            
            # Post-Process (Wav Convert only, no FX)
            
            # Final format for ESP32
            sound = sound.set_channels(1) # Mono
            sound = sound.set_sample_width(2) # 16-bit
            sound = sound.set_frame_rate(AUDIO_RATE) # Ensure 44.1k
            
            sound.export(str(wav_path), format="wav")
            
            return True

        except Exception as e:
                safe_print(f"  [ERROR] Piper failed for '{line[:20]}...': {e}")
                if wav_temp.exists(): os.remove(wav_temp)
                return False

    except Exception as e:
        safe_print(f"  [ERROR] General failure generating '{line[:20]}...': {e}")
        return False

def generate_audio_cache():
    """
    Syncs text files in SOURCE_DIR with CACHE_DIR.
    Generates missing audio with randomized voice properties based on text hash.
    Returns a dictionary of available categories.
    """
    print(f"--- Syncing Audio Cache ---")
    print(f"Source: {SOURCE_DIR}")
    print(f"Cache:  {CACHE_DIR}\n")

    ensure_dirs()
    check_piper_status()
    
    valid_hashes = set()
    categories = {}  # {category_name: {lang: [files...]}}
    
    tasks = [] # List of args for process_single_file

    # 1. Scan source files
    txt_files = list(SOURCE_DIR.glob("*.txt"))
    
    for txt_file in txt_files:
        category, lang = parse_filename(txt_file.name)
        
        if category not in categories:
            categories[category] = set()
        categories[category].add(lang)

        print(f"Scanning {txt_file.name} (Cat: {category}, Lang: {lang})...")

        with open(txt_file, 'r', encoding='utf-8') as f:
            lines = [line.strip() for line in f if line.strip()]

        for line in lines:
            line_hash = get_file_hash(line)
            valid_hashes.add(line_hash)
            
            # format: Category_Lang_Hash.wav
            safe_cat = re.sub(r'[^a-zA-Z0-9_\-]', '', category)
            wav_filename = f"{safe_cat}_{lang}_{line_hash}.wav"
            wav_path = CACHE_DIR / wav_filename
            
            if not wav_path.exists():
                tasks.append((line, lang, category, wav_path, line_hash))

    # 2. Execute Tasks in Parallel
    if tasks:
        total_tasks = len(tasks)
        print(f"\n[INFO] Found {total_tasks} missing audio files. Generating in parallel...")
        
        start_time = time.time()
        completed = 0
        
        # Adjust max_workers based on your network/CPU comfort.
        # NOTE: severe rate-limiting by Google if too high (>5). Keeping it low for stability.
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
            # We use as_completed to update progress immediately
            future_to_task = {executor.submit(process_single_file, task): task for task in tasks}
            
            for future in concurrent.futures.as_completed(future_to_task):
                completed += 1
                
                # Calculate timing
                elapsed = time.time() - start_time
                if completed > 0:
                    avg_time = elapsed / completed
                    remaining = avg_time * (total_tasks - completed)
                else:
                    remaining = 0
                
                # Progress Bar
                percent = (completed / total_tasks) * 100
                bar_len = 30
                filled = int(bar_len * completed / total_tasks)
                bar = '█' * filled + '-' * (bar_len - filled)
                
                status_line = (
                    f"\r[{bar}] {percent:5.1f}% | "
                    f"{completed}/{total_tasks} | "
                    f"Time: {format_time(elapsed)} (ETA: {format_time(remaining)})"
                )
                print(status_line, end='', flush=True)

        print() # Newline after done
    else:
        print("\n[INFO] All audio files are up to date.")

    # 3. Cleanup obsolete files
    print("\n--- Cleaning up obsolete files ---")
    cache_files = list(CACHE_DIR.glob("*.wav"))
    deleted_count = 0
    
    for wav_file in cache_files:
        try:
            stem = wav_file.stem
            parts = stem.rsplit('_', 1)
            file_hash = parts[-1]
            
            if file_hash not in valid_hashes:
                os.remove(wav_file)
                print(f"  [DEL] Obsolete: {wav_file.name}")
                deleted_count += 1
        except Exception:
            continue
            
    if deleted_count == 0:
        print("  No obsolete files found.")
        
    return categories

def generate_sd_card_structure(categories):
    """
    Interactive phase to build SD card content.
    """
    print("\n\n--- Generating SD Card Content ---")
    
    sorted_cats = sorted(categories.keys())
    if not sorted_cats:
        print("No categories found. Exiting.")
        return

    print("Available Categories:")
    for i, cat in enumerate(sorted_cats):
        langs = ", ".join(categories[cat])
        print(f"  {i+1}. {cat} ({langs})")
        
    for persona_idx in range(1, 6):
        default_name = ""
        
        # Smart Defaults
        if persona_idx == 1: default_name = next((c for c in sorted_cats if "Badran" in c), "")
        elif persona_idx == 5: default_name = next((c for c in sorted_cats if "Fortune" in c), "")

        prompt_text = f"\nSelect Category for Persona {persona_idx}"
        if default_name:
            prompt_text += f" (Default: {default_name})"
        prompt_text += ": "
        
        while True:
            selection = input(prompt_text).strip()
            selected_cat = ""
            
            if not selection and default_name:
                selected_cat = default_name
            elif selection.isdigit():
                idx = int(selection) - 1
                if 0 <= idx < len(sorted_cats):
                    selected_cat = sorted_cats[idx]
            
            if selected_cat:
                print(f"  -> Assigned '{selected_cat}' to Persona {persona_idx}")
                
                # Create persona directories
                p_dir = SD_ROOT / f"persona_{persona_idx:02d}"
                if p_dir.exists():
                    shutil.rmtree(p_dir)
                p_dir.mkdir(parents=True) 
                
                # Prepare Playlist Data
                playlist_tracks = {'de': [], 'en': []}
                
                safe_cat = re.sub(r'[^a-zA-Z0-9_\-]', '', selected_cat)
                
                copied_count = 0
                for wav_file in CACHE_DIR.glob("*.wav"):
                    if wav_file.name.startswith(safe_cat + "_"):
                        parts = wav_file.stem.split('_')
                        if len(parts) >= 3:
                            f_lang = parts[-2]
                            f_cat = "_".join(parts[:-2])
                            
                            if f_cat == safe_cat and f_lang in ['de', 'en']:
                                target_dir = p_dir / f_lang
                                target_dir.mkdir(exist_ok=True)
                                
                                # Copy File
                                shutil.copy2(wav_file, target_dir / wav_file.name)
                                
                                # Add to Playlist
                                # Path format: /persona_01/de/file.wav
                                relative_path = f"/persona_{persona_idx:02d}/{f_lang}/{wav_file.name}"
                                playlist_tracks[f_lang].append(relative_path)
                                
                                copied_count += 1
                
                print(f"     Copied {copied_count} files.")
                
                # Generate Playlists
                playlist_dir = SD_ROOT / "playlists"
                playlist_dir.mkdir(exist_ok=True)
                
                for lang in ['de', 'en']:
                    tracks = playlist_tracks[lang]
                    if tracks:
                        random.shuffle(tracks)
                        # Filename: cat_1_de_v3.m3u
                        pl_filename = f"cat_{persona_idx}_{lang}_v3.m3u"
                        pl_path = playlist_dir / pl_filename
                        
                        with open(pl_path, 'w', encoding='utf-8') as f:
                            for track in tracks:
                                f.write(track + "\n")
                        print(f"     Created Playlist: {pl_filename} ({len(tracks)} tracks)")
                    else:
                        # Clean up old playlist if it exists? 
                        # Or ensure we don't leave stale ones?
                        pass

                break
            else:
                print("Invalid selection. Try again.")

    print(f"\nDone! SD Card content generated in: {SD_ROOT}")

def generate_procedural_tones():
    """
    Generates synthesized ringtones and alarm sounds using Pydub generators.
    This creates perfectly clean, noise-free classic tones.
    """
    print("\n\n--- Generating Procedural Ringtones & Alarms ---")
    
    target_dir = SD_ROOT / "ringtones"
    target_dir.mkdir(parents=True, exist_ok=True)
    
    # Helper to standardise export
    def save_tone(audio_seg, filename, gain_db=-3.0):
        # Normalize and set format
        out = audio_seg.set_channels(1).set_sample_width(2).set_frame_rate(AUDIO_RATE)
        out = out.apply_gain(gain_db)
        out_path = target_dir / filename
        out.export(str(out_path), format="wav")
        print(f"  [SYNTH] {filename}")

    # 1. Classic Phone (Mix of 440Hz and 480Hz, modulated)
    # The US Standard ring is 2s ON, 4s OFF. The Tone is ~440+480Hz
    print("  Synthesizing Classic Phone...")
    tone1 = Sine(440).to_audio_segment(duration=2000)
    tone2 = Sine(480).to_audio_segment(duration=2000)
    base_ring = tone1.overlay(tone2).apply_gain(-3.0)
    # Add a slight amplitude modulation (tremolo) to simulate mechanical bells
    low_freq_osc = Sine(20).to_audio_segment(duration=2000).apply_gain(0.2) # Not real AM, but close enough layering
    classic = base_ring.overlay(low_freq_osc)
    silence = AudioSegment.silent(duration=1000) # 1s silence gap
    final_classic = classic + silence
    save_tone(final_classic, "classic_phone.wav", -2.0)

    # 2. Digital Alarm ("BEEP BEEP BEEP")
    # Square wave 2000Hz
    print("  Synthesizing Digital Alarm...")
    beep = Square(2000).to_audio_segment(duration=100).fade_out(10)
    gap = AudioSegment.silent(duration=100)
    long_gap = AudioSegment.silent(duration=500)
    pattern = (beep + gap + beep + gap + beep + gap + beep + long_gap)
    save_tone(pattern * 2, "digital_alarm.wav", -6.0)

    # 3. Sonar Ping (Sine wave with long reverb-like tail)
    print("  Synthesizing Sonar...")
    ping = Sine(880).to_audio_segment(duration=50).fade_out(20)
    tail = Sine(880).to_audio_segment(duration=1500).apply_gain(-20).fade_out(1000) # Fake echo
    sonar = ping + tail
    save_tone(sonar, "sonar_ping.wav", -3.0)
    
    # 4. Soft Chime (Major Triad: C-E-G)
    print("  Synthesizing Soft Chime...")
    dur = 2000
    root = Sine(523.25).to_audio_segment(duration=dur).fade_out(dur) # C5
    third = Sine(659.25).to_audio_segment(duration=dur).fade_out(dur).apply_gain(-2) # E5
    fifth = Sine(783.99).to_audio_segment(duration=dur).fade_out(dur).apply_gain(-4) # G5
    chime = root.overlay(third).overlay(fifth)
    save_tone(chime, "soft_chime.wav", -3.0)

    # 5. Nuke Warning (Sawtooth Sweep)
    # Pydub doesn't do frequency sweeps easily, so we step it
    print("  Synthesizing Warning Siren...")
    siren = AudioSegment.silent(duration=0)
    for f in range(400, 1200, 20):
        siren += Sawtooth(f).to_audio_segment(duration=20)
    # And down
    for f in range(1200, 400, -20):
        siren += Sawtooth(f).to_audio_segment(duration=20)
        
    save_tone(siren * 2, "warning_siren.wav", -12.0) # Sawtooth is loud!

def generate_system_sounds():
    """Generates mapped system prompts using Piper AND copies static files."""
    print("\n\n--- Generating System Sounds & Time Announcements ---")
    
    # 1. Generate Static System Prompts
    for rel_path, (text, lang, model) in SYSTEM_PROMPTS.items():
        out_path = SD_ROOT / rel_path
        out_path.parent.mkdir(parents=True, exist_ok=True)
        
        if not out_path.exists():
            print(f"  [GEN] {rel_path}...")
            try:
                generate_with_piper(text, lang, out_path, model_name=model)
                # Convert to target format
                sound = AudioSegment.from_wav(str(out_path))
                sound = sound.set_channels(1).set_sample_width(2).set_frame_rate(AUDIO_RATE)
                sound.export(str(out_path), format="wav")
            except Exception as e:
                print(f"    [ERR] Failed to generate {rel_path}: {e}")

    # 2. Generate Time Announcements
    time_tasks = generate_time_announcements()
    total_time = len(time_tasks)
    print(f"  [GEN] Generating {total_time} time announcements...")
    
    # Process time tasks
    # We use a simple loop here because there are many small files
    # Using the pool might be better, but blocking sequential is safer to avoid Piper crashes if any
    
    # Let's try Parallel again for speed, but catch errors carefully
    
    # Helper for Time
    def process_time_task(task):
        text, lang, path, model = task
        if path.exists(): return
        
        # print(f"    -> {path.name}") 
        temp_path = path.with_suffix(".tmp.wav")
        try:
             generate_with_piper(text, lang, temp_path, model_name=model)
             sound = AudioSegment.from_wav(str(temp_path))
             sound = sound.set_channels(1).set_sample_width(2).set_frame_rate(AUDIO_RATE)
             sound.export(str(path), format="wav")
             if temp_path.exists(): os.remove(temp_path)
             return True
        except Exception as e:
             safe_print(f"    [ERR] {path.name}: {e}")
             if temp_path.exists(): os.remove(temp_path)
             return False

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        list(executor.map(process_time_task, time_tasks))


    # 3. Copy Static Files (SFX, Ringtones)
    template_root = PROJECT_ROOT / "effects"
    
    # Specific mapped files
    for src_rel, dest_rel in STATIC_COPY_MAP.items():
        src = template_root / src_rel
        dest = SD_ROOT / dest_rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        
        if src.exists():
             if not dest.exists():
                print(f"  [COPY] {src_rel} -> {dest_rel}")
                shutil.copy2(src, dest)
        else:
            print(f"  [WARN] Source file missing: {src}")

    # 3b. Copy Fonts (from Project Root)
    fonts_src = PROJECT_ROOT / "fonts"
    fonts_dest = SD_ROOT / "system" / "fonts"
    if fonts_src.exists():
         fonts_dest.mkdir(parents=True, exist_ok=True)
         for font in fonts_src.glob("*.*"):
             dest_file = fonts_dest / font.name
             if not dest_file.exists():
                 print(f"  [COPY] Font {font.name}")
                 shutil.copy2(font, dest_file)

    # 4. Copy Ringtones (Whole folder)
    ringtones_src = template_root / "ringtones"
    ringtones_dest = SD_ROOT / "ringtones"
    if ringtones_src.exists():
        ringtones_dest.mkdir(parents=True, exist_ok=True)
        for wav in ringtones_src.glob("*.wav"):
            dest_file = ringtones_dest / wav.name
            if not dest_file.exists():
                print(f"  [COPY] Ringtone {wav.name}")
                shutil.copy2(wav, dest_file)
    
    print("System sounds update complete.")


def main():
    try:
        categories = generate_audio_cache()
        generate_sd_card_structure(categories)
        generate_procedural_tones()
        generate_system_sounds()
    except KeyboardInterrupt:
        print("\nAborted.")

if __name__ == "__main__":
    main()
