import os
import json
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
from pydub.generators import Sine, Square, Sawtooth, Pulse, WhiteNoise

# Configuration
PROJECT_ROOT = Path(__file__).parent.parent
SOURCE_DIR = PROJECT_ROOT / "compliments"
CACHE_DIR = SOURCE_DIR / "audio_cache"
SD_ROOT = PROJECT_ROOT / "sd_card_content"
AUDIO_RATE = 44100  # 44.1kHz for ESP32 compatibility

# Piper Configuration
_sys_piper = shutil.which("piper")
PIPER_BIN = Path(_sys_piper) if _sys_piper else Path("piper_not_found_on_system_path")
PIPER_BIN_VENV = PROJECT_ROOT / ".venv/bin/piper"
PIPER_BIN_FALLBACK = PROJECT_ROOT / "utils/piper/piper/piper"
PIPER_VOICES_DIR = PROJECT_ROOT / "utils/piper_voices"
PIPER_VOICES_DIR_EN = PIPER_VOICES_DIR / "en"
PIPER_VOICES_DIR_DE = PIPER_VOICES_DIR / "de"
USE_PIPER = False # Will be auto-detected

# TTS Routing (Fixed): EN -> Piper, DE -> Google TTS
GTTS_DE_MAX_WORKERS = 6

# Language generation toggle
ENABLED_LANGS = {"de", "en"}

# Non-interactive mode for persona assignment
NON_INTERACTIVE = False
PERSONA_DEFAULTS = {
    1: "TGIF",
    2: "badran",
    3: "computer",
    4: "dad_jokes",
    5: "trump",
}

# Available Accents (TLDs) for gTTS
ACCENTS_EN = ['com', 'co.uk', 'com.au', 'ca', 'co.in', 'ie', 'co.za']
ACCENTS_DE = ['de'] # 'fr', 'us' etc produce strong foreign accents, 'de' is standard

# Piper Models (auto-discovered from utils/piper_voices/en, de)
PIPER_MODELS_EN = []
PIPER_MODELS_DE = []

# Voice preferences
EN_HAL_MODEL = "hal.onnx"
EN_GLADOS_MODEL = "en_us-glados-high.onnx"
DE_GLADOS_MODEL = "de_glados-medium.onnx"
EN_TRUMP_MODEL = "en_US-trump-high.onnx"
EN_EMINEM_MODEL = "en_US-eminem-medium.onnx"
EN_FEMALE_PREFERRED = ["en_GB-jenny_dioco-medium.onnx", "en_GB-cori-high.onnx"]
EN_MALE_PREFERRED = ["en_GB-alan-medium.onnx", "en_US-ryan-high.onnx", "en_GB-aru-medium.onnx"]

def load_piper_models_en():
    if not PIPER_VOICES_DIR_EN.exists():
        return []
    return sorted(p.name for p in PIPER_VOICES_DIR_EN.glob("*.onnx"))

def load_piper_models_de():
    if not PIPER_VOICES_DIR_DE.exists():
        return []
    return sorted(p.name for p in PIPER_VOICES_DIR_DE.glob("*.onnx"))

def refresh_piper_models_en():
    global PIPER_MODELS_EN
    PIPER_MODELS_EN = load_piper_models_en()
    return PIPER_MODELS_EN

def refresh_piper_models_de():
    global PIPER_MODELS_DE
    PIPER_MODELS_DE = load_piper_models_de()
    return PIPER_MODELS_DE

def get_default_piper_en_model():
    if not PIPER_MODELS_EN:
        return None
    preferred = "en_GB-jenny_dioco-medium.onnx"
    if preferred in PIPER_MODELS_EN:
        return preferred
    return PIPER_MODELS_EN[0]

def resolve_preferred_en_model(candidates):
    for model in candidates:
        if model in PIPER_MODELS_EN:
            return model
    return None

def get_hal_en_model():
    if EN_HAL_MODEL in PIPER_MODELS_EN:
        return EN_HAL_MODEL
    return None

def get_glados_en_model():
    if EN_GLADOS_MODEL in PIPER_MODELS_EN:
        return EN_GLADOS_MODEL
    return None

def get_glados_de_model():
    if DE_GLADOS_MODEL in PIPER_MODELS_DE:
        return DE_GLADOS_MODEL
    return None

def get_trump_en_model():
    if EN_TRUMP_MODEL in PIPER_MODELS_EN:
        return EN_TRUMP_MODEL
    return None

def get_eminem_en_model():
    if EN_EMINEM_MODEL in PIPER_MODELS_EN:
        return EN_EMINEM_MODEL
    return None

def pick_random_en_model(rng, exclude_special=True):
    models = PIPER_MODELS_EN
    if exclude_special:
        models = [m for m in PIPER_MODELS_EN if m not in {EN_HAL_MODEL, EN_GLADOS_MODEL, EN_TRUMP_MODEL, EN_EMINEM_MODEL}]
    if not models:
        return get_default_piper_en_model()
    return rng.choice(models)

def select_en_model_for_category(category, rng):
    if category == "computer":
        return get_hal_en_model() or get_default_piper_en_model()
    if category == "robo":
        return get_glados_en_model() or get_default_piper_en_model()
    if category == "trump":
        return get_trump_en_model() or get_default_piper_en_model()
    if category == "eminem":
        return get_eminem_en_model() or get_default_piper_en_model()
    if category == "badran":
        return resolve_preferred_en_model(EN_FEMALE_PREFERRED) or get_default_piper_en_model()
    if category in {"dad_jokes", "trump"}:
        return resolve_preferred_en_model(EN_MALE_PREFERRED) or get_default_piper_en_model()
    return pick_random_en_model(rng, exclude_special=True)

# System Prompts Configuration
# Format: "path/to/file.wav": ("Text to speak", "Language Code", "Specific Model Name or None")
# Note: If model is None, it defaults to gTTS (DE) or the default Piper EN model.
SYSTEM_PROMPTS = {
    # English System Messages (Piper)
    "system/snooze_active_en.wav": ("Snooze active.", "en", None),
    "system/system_ready_en.wav": ("System ready.", "en", None),
    "system/time_unavailable_en.wav": ("Time synchronization failed.", "en", None),
    "system/timer_stopped_en.wav": ("Timer cancelled.", "en", None),
    "system/alarm_stopped_en.wav": ("Alarm cancelled.", "en", None),
    "system/reindex_warning_en.wav": ("Please wait. Re-indexing content.", "en", None),
    "system/alarm_active_en.wav": ("Alarm active.", "en", None),
    "system/alarm_confirm_en.wav": ("Alarm confirmed.", "en", None),
    "system/alarm_deleted_en.wav": ("Alarm deleted.", "en", None),
    "system/alarm_skipped_en.wav": ("Alarm skipped.", "en", None),
    "system/alarms_off_en.wav": ("Alarms disabled.", "en", None),
    "system/alarms_on_en.wav": ("Alarms enabled.", "en", None),
    "system/battery_crit_en.wav": ("Battery critical. Shutting down.", "en", None),
    "system/error_msg_en.wav": ("An error occurred.", "en", None),
    "system/menu_en.wav": ("Main Menu.", "en", None),
    "system/timer_confirm_en.wav": ("Timer started.", "en", None),
    "system/timer_deleted_en.wav": ("Timer deleted.", "en", None),
    "system/timer_set_en.wav": ("Timer set.", "en", None),
    "system/next_alarm_en.wav": ("Next alarm.", "en", None),
    "system/next_alarm_none_en.wav": ("No alarms scheduled.", "en", None),
    "system/night_on_en.wav": ("Night mode on.", "en", None),
    "system/night_off_en.wav": ("Night mode off.", "en", None),
    "system/phonebook_export_en.wav": ("Phonebook entries.", "en", None),
    "system/pb_persona1_en.wav": ("Persona one.", "en", None),
    "system/pb_persona2_en.wav": ("Persona two.", "en", None),
    "system/pb_persona3_en.wav": ("Persona three.", "en", None),
    "system/pb_persona4_en.wav": ("Persona four.", "en", None),
    "system/pb_persona5_en.wav": ("Persona five.", "en", None),
    "system/pb_random_mix_en.wav": ("Random mix.", "en", None),
    "system/pb_time_en.wav": ("Time announcement.", "en", None),
    "system/pb_menu_en.wav": ("Voice menu.", "en", None),
    "system/pb_reboot_en.wav": ("System reboot.", "en", None),
    "system/system_check_en.wav": ("System check.", "en", None),
    "system/ip_en.wav": ("IP address.", "en", None),
    "system/dot_en.wav": ("dot", "en", None),
    "system/wifi_en.wav": ("WiFi signal.", "en", None),
    "system/dbm_en.wav": ("d b m.", "en", None),
    "system/ntp_en.wav": ("Last N T P sync.", "en", None),
    "system/minutes_en.wav": ("minutes.", "en", None),
    "system/sd_free_en.wav": ("SD free.", "en", None),
    "system/mb_en.wav": ("megabytes.", "en", None),
    "system/sd_ok_en.wav": ("SD card ok.", "en", None),
    "system/sd_missing_en.wav": ("SD card missing.", "en", None),
    
    # German System Messages (Google TTS, female voice)
    "system/snooze_active_de.wav": ("Snooze aktiv.", "de", None),
    "system/system_ready_de.wav": ("System bereit.", "de", None),
    "system/time_unavailable_de.wav": ("Zeit-Synchronisation fehlgeschlagen.", "de", None),
    "system/timer_stopped_de.wav": ("Timer abgebrochen.", "de", None),
    "system/alarm_stopped_de.wav": ("Alarm abgebrochen.", "de", None),
    "system/reindex_warning_de.wav": ("Bitte warten. Inhalte werden neu indexiert.", "de", None),
    "system/alarm_active_de.wav": ("Alarm aktiv.", "de", None),
    "system/alarm_confirm_de.wav": ("Alarm bestätigt.", "de", None),
    "system/alarm_deleted_de.wav": ("Alarm gelöscht.", "de", None),
    "system/alarm_skipped_de.wav": ("Alarm übersprungen.", "de", None),
    "system/alarms_off_de.wav": ("Alarme deaktiviert.", "de", None),
    "system/alarms_on_de.wav": ("Alarme aktiviert.", "de", None),
    "system/battery_crit_de.wav": ("Batterie kritisch. System wird heruntergefahren.", "de", None),
    "system/error_msg_de.wav": ("Ein Fehler ist aufgetreten.", "de", None),
    "system/menu_de.wav": ("Hauptmenü.", "de", None),
    "system/timer_confirm_de.wav": ("Timer gestartet.", "de", None),
    "system/timer_deleted_de.wav": ("Timer gelöscht.", "de", None),
    "system/timer_set_de.wav": ("Timer gesetzt.", "de", None),
    "system/next_alarm_de.wav": ("Naechster Wecker.", "de", None),
    "system/next_alarm_none_de.wav": ("Kein Wecker aktiv.", "de", None),
    "system/night_on_de.wav": ("Nachtmodus an.", "de", None),
    "system/night_off_de.wav": ("Nachtmodus aus.", "de", None),
    "system/phonebook_export_de.wav": ("Telefonbuch Eintraege.", "de", None),
    "system/pb_persona1_de.wav": ("Persona eins.", "de", None),
    "system/pb_persona2_de.wav": ("Persona zwei.", "de", None),
    "system/pb_persona3_de.wav": ("Persona drei.", "de", None),
    "system/pb_persona4_de.wav": ("Persona vier.", "de", None),
    "system/pb_persona5_de.wav": ("Persona fuenf.", "de", None),
    "system/pb_random_mix_de.wav": ("Zufallsmix.", "de", None),
    "system/pb_time_de.wav": ("Zeitauskunft.", "de", None),
    "system/pb_menu_de.wav": ("Sprachmenue.", "de", None),
    "system/pb_reboot_de.wav": ("System Neustart.", "de", None),
    "system/system_check_de.wav": ("Systempruefung.", "de", None),
    "system/ip_de.wav": ("IP Adresse.", "de", None),
    "system/dot_de.wav": ("Punkt", "de", None),
    "system/wifi_de.wav": ("WLAN Signal.", "de", None),
    "system/dbm_de.wav": ("d b m.", "de", None),
    "system/ntp_de.wav": ("Letzter N T P Abgleich.", "de", None),
    "system/minutes_de.wav": ("Minuten.", "de", None),
    "system/sd_free_de.wav": ("SD frei.", "de", None),
    "system/mb_de.wav": ("Megabyte.", "de", None),
    "system/sd_ok_de.wav": ("SD Karte ok.", "de", None),
    "system/sd_missing_de.wav": ("SD Karte fehlt.", "de", None),
}

# Static Files to Copy (Source Path relative to template, Destination relative to SD Root)
STATIC_COPY_MAP = {
    # Ringtones are handled separately via folder copy
}

# Time Announcements (Range Definitions)
TIME_CONFIG = {
    "hours": range(0, 24), # 0-23
    "minutes": range(0, 301), # 0-300 (Extended for Timer)
    "days": range(1, 32), # 1-31
    "months": range(0, 12), # 0-11
    "weekdays": range(0, 7), # 0-6
    "years": range(2025, 2036) # 2025-2035
}

def generate_tones(base_dir):
    """Generates standard system tones and sound effects (US standard + Extras)."""
    print("Generating system tones (US Standard + Extras)...")
    system_dir = base_dir / "system"
    system_dir.mkdir(parents=True, exist_ok=True)

    # Helper: Save tone
    def save(seg, name):
        seg = seg.set_channels(1).set_sample_width(2).set_frame_rate(AUDIO_RATE)
        seg.export(system_dir / name, format="wav")
        print(f"  Generated {system_dir}/{name}")

    # 1. Dial Tone (US): 350Hz + 440Hz continuous
    tone_len = 10000 
    dt_350 = Sine(350).to_audio_segment(duration=tone_len)
    dt_440 = Sine(440).to_audio_segment(duration=tone_len)
    dial_tone = dt_350.overlay(dt_440).apply_gain(-3.0) 
    save(dial_tone, "dial_tone.wav")

    # 2. Busy Tone (US): 480Hz + 620Hz, 0.5s on, 0.5s off
    busy_on = (Sine(480).to_audio_segment(duration=500)
               .overlay(Sine(620).to_audio_segment(duration=500))
               .apply_gain(-3.0))
    busy_off = AudioSegment.silent(duration=500)
    busy_tone = (busy_on + busy_off) * 6
    save(busy_tone, "busy_tone.wav")

    # 3. Error Tone (Fast Busy): 480Hz + 620Hz, 0.25s on, 0.25s off
    err_on = (Sine(480).to_audio_segment(duration=250)
              .overlay(Sine(620).to_audio_segment(duration=250))
              .apply_gain(-3.0))
    err_off = AudioSegment.silent(duration=250)
    error_tone = (err_on + err_off) * 6
    save(error_tone, "error_tone.wav")

    # 4. Beep (Simple 1000Hz)
    beep = Sine(1000).to_audio_segment(duration=200).apply_gain(-5.0)
    save(beep, "beep.wav")

    # 4b. Silence (300ms)
    silence = AudioSegment.silent(duration=300)
    save(silence, "silence_300ms.wav")
    
    # 5. Click (User Interaction)
    click = Sine(2500).to_audio_segment(duration=30).apply_gain(-10.0).fade_out(10)
    save(click, "click.wav")

    # 6. Computing (Random "Thinking" Sound)
    computing = AudioSegment.silent(duration=0)
    for _ in range(15):
        freq = random.choice([400, 600, 800, 1200, 1500, 2000])
        dur = random.randint(30, 80)
        tone = Square(freq).to_audio_segment(duration=dur).apply_gain(-15.0)
        silence = AudioSegment.silent(duration=random.randint(10, 50))
        computing += tone + silence
    save(computing, "computing.wav")

    # 7. Startup Sound (Arpeggio C-E-G-C)
    note_dur = 150
    start_c = Sine(261.63).to_audio_segment(duration=note_dur).fade_in(10).fade_out(10)
    start_e = Sine(329.63).to_audio_segment(duration=note_dur).fade_in(10).fade_out(10)
    start_g = Sine(392.00).to_audio_segment(duration=note_dur).fade_in(10).fade_out(10)
    start_c2 = Sine(523.25).to_audio_segment(duration=400).fade_in(10).fade_out(200)
    startup = (start_c + start_e + start_g + start_c2).apply_gain(-4.0)
    save(startup, "startup.wav")
    
    # 8. Fallback Alarm (Annoying Beep)
    fb_on = Square(800).to_audio_segment(duration=400).apply_gain(-5.0)
    fb_off = AudioSegment.silent(duration=400)
    fallback = (fb_on + fb_off) * 10
    save(fallback, "fallback_alarm.wav")
    
    # 9. Battery Low (Descending)
    bat_1 = Sine(880).to_audio_segment(duration=300).apply_gain(-5.0)
    bat_2 = Sine(440).to_audio_segment(duration=600).fade_out(200).apply_gain(-5.0)
    battery = bat_1 + bat_2
    save(battery, "battery_low.wav")

def generate_time_announcements():
    """Generates time announcement files for DE and EN."""
    
    # German (Google TTS)
    model_de = None
    inst_de = ""
    # English (Piper)
    if not PIPER_MODELS_EN:
        refresh_piper_models_en()
    model_en = get_default_piper_en_model()
    inst_en = ""
    
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
        if "de" in ENABLED_LANGS:
            add_task(f"{inst_de}{h}", "de", base_dir / f"de/h_{h}.wav", model_de)
        if "en" in ENABLED_LANGS:
            add_task(f"{inst_en}{h}", "en", base_dir / f"en/h_{h}.wav", model_en)

    # Minutes
    for m in TIME_CONFIG["minutes"]:
        # German: 0-9 can cover "00"-"09" if needed, but usually just numbers.
        # Format "m_XX.wav" (always 2 digits? or 1? template had m_00)
        # We will generate m_00 to m_59
        fname = f"m_{m:02d}.wav"
        if "de" in ENABLED_LANGS:
            add_task(f"{inst_de}{m}", "de", base_dir / f"de/{fname}", model_de)
        if "en" in ENABLED_LANGS:
            add_task(f"{inst_en}{m}", "en", base_dir / f"en/{fname}", model_en)

    # Days
    for d in TIME_CONFIG["days"]:
        if "de" in ENABLED_LANGS:
            add_task(f"{inst_de}{d}.", "de", base_dir / f"de/day_{d}.wav", model_de) # Ordinal in DE? "erster"? 
        if "en" in ENABLED_LANGS:
            add_task(f"{inst_en}{d}", "en", base_dir / f"en/day_{d}.wav", model_en) # "1st"?

    # Months
    months_de = ["Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"]
    months_en = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"]
    
    for i in TIME_CONFIG["months"]:
        if "de" in ENABLED_LANGS:
            add_task(f"{inst_de}{months_de[i]}", "de", base_dir / f"de/month_{i}.wav", model_de)
        if "en" in ENABLED_LANGS:
            add_task(f"{inst_en}{months_en[i]}", "en", base_dir / f"en/month_{i}.wav", model_en)

    # Weekdays
    wday_de = ["Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"]
    wday_en = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"]
    
    for i in TIME_CONFIG["weekdays"]:
         if "de" in ENABLED_LANGS:
             add_task(f"{inst_de}{wday_de[i]}", "de", base_dir / f"de/wday_{i}.wav", model_de)
         if "en" in ENABLED_LANGS:
             add_task(f"{inst_en}{wday_en[i]}", "en", base_dir / f"en/wday_{i}.wav", model_en)

    # Years
    for y in TIME_CONFIG["years"]:
        if "de" in ENABLED_LANGS:
            add_task(f"{inst_de}{y}", "de", base_dir / f"de/year_{y}.wav", model_de)
        if "en" in ENABLED_LANGS:
            add_task(f"{inst_en}{y}", "en", base_dir / f"en/year_{y}.wav", model_en)

    # Specials
    if "de" in ENABLED_LANGS:
        add_task(f"{inst_de}Uhr", "de", base_dir / "de/uhr.wav", model_de)
        add_task(f"{inst_de}Es ist", "de", base_dir / "de/intro.wav", model_de)
        add_task(f"{inst_de}Heute ist", "de", base_dir / "de/date_intro.wav", model_de)
        add_task(f"{inst_de}Sommerzeit", "de", base_dir / "de/dst_summer.wav", model_de)
        add_task(f"{inst_de}Winterzeit", "de", base_dir / "de/dst_winter.wav", model_de)
    
    if "en" in ENABLED_LANGS:
        add_task(f"{inst_en}It is", "en", base_dir / "en/intro.wav", model_en)
        add_task(f"{inst_en}Today is", "en", base_dir / "en/date_intro.wav", model_en)
        add_task(f"{inst_en}Summer time", "en", base_dir / "en/dst_summer.wav", model_en)
        add_task(f"{inst_en}Winter time", "en", base_dir / "en/dst_winter.wav", model_en)

    return tasks

def check_piper_status():
    """Checks if Piper is installed and voices are available."""
    global USE_PIPER
    if PIPER_BIN.exists():
        piper_bin = PIPER_BIN
    elif PIPER_BIN_VENV.exists():
        piper_bin = PIPER_BIN_VENV
    else:
        piper_bin = PIPER_BIN_FALLBACK
    if piper_bin.exists() and PIPER_VOICES_DIR_EN.exists():
        refresh_piper_models_en()
        refresh_piper_models_de()
        if PIPER_MODELS_EN:
            USE_PIPER = True
            print("[INFO] Piper TTS detected for English voices.")
        else:
            print("[ERROR] No English Piper voices found in utils/piper_voices/en")
            USE_PIPER = False
            exit(1)
    else:
        print("[ERROR] Piper not detected. Please install Piper to proceed.")
        print("        Check 'utils/piper/' or your PATH, and 'utils/piper_voices/en'")
        exit(1)

def generate_speech(text, lang, output_path, model_name=None, force_piper=False):
    """Generates audio using Piper (EN) or Google TTS (DE)."""
    
    # German: Google TTS only
    if lang == 'de' and not force_piper:
        try:
            # Use 'com' for English (US) and 'de' for German
            tld = 'de' if lang == 'de' else 'com'
            safe_print(f"  [gTTS] de -> {output_path.name}: {text[:60]}{'...' if len(text) > 60 else ''}")
            tts = gTTS(text=text, lang=lang, tld=tld, slow=False)
            
            # gTTS saves MP3. We must convert to WAV if output is WAV.
            temp_mp3 = str(output_path) + ".mp3"
            tts.save(temp_mp3)
            
            # Convert
            sound = AudioSegment.from_mp3(temp_mp3)
            sound = sound.set_channels(1).set_sample_width(2).set_frame_rate(AUDIO_RATE)
            sound.export(str(output_path), format="wav")
            
            if os.path.exists(temp_mp3):
                os.remove(temp_mp3)
                
            return True
        except Exception as e:
            if 'temp_mp3' in locals() and os.path.exists(temp_mp3):
                os.remove(temp_mp3)
            raise Exception(f"gTTS failed: {e}")

    # Piper (EN or forced DE)
    if not PIPER_MODELS_EN:
        refresh_piper_models_en()
    if not PIPER_MODELS_DE:
        refresh_piper_models_de()

    if lang == 'de' and force_piper:
        effective_model = model_name or get_glados_de_model()
        if not effective_model:
            raise FileNotFoundError("No German Piper models found in utils/piper_voices/de")
        model_path = PIPER_VOICES_DIR_DE / effective_model
    else:
        effective_model = model_name or get_default_piper_en_model()
        if not effective_model:
            raise FileNotFoundError("No English Piper models found in utils/piper_voices/en")
        model_path = PIPER_VOICES_DIR_EN / effective_model
    
        if not model_path.exists():
            raise FileNotFoundError(f"Piper Model {effective_model} not found at {model_path}")
    
    cmd = [
        str(PIPER_BIN if PIPER_BIN.exists() else (PIPER_BIN_VENV if PIPER_BIN_VENV.exists() else PIPER_BIN_FALLBACK)),
        "--model", str(model_path),
        "--output_file", str(output_path)
    ]
    
    # Piper expects text via stdin
    process = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    stdout, stderr = process.communicate(input=text.encode('utf-8'))
    
    if process.returncode != 0:
        raise Exception(f"Piper failed: {stderr.decode()}")

    try:
        sound = AudioSegment.from_wav(str(output_path))
        sound = sound.set_channels(1).set_sample_width(2).set_frame_rate(AUDIO_RATE)
        sound.export(str(output_path), format="wav")
    except Exception as e:
        raise Exception(f"Piper WAV normalize failed: {e}")
    
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
        force_piper = False
        if lang == 'en':
            if not PIPER_MODELS_EN:
                refresh_piper_models_en()
            chosen_model = select_en_model_for_category(category, rng)
        elif lang == 'de' and category == 'robo':
            if not PIPER_MODELS_DE:
                refresh_piper_models_de()
            chosen_model = get_glados_de_model()
            force_piper = True
        
        try:
            generate_speech(line, lang, wav_temp, model_name=chosen_model, force_piper=force_piper)
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
            
            if (lang in ENABLED_LANGS) and (not wav_path.exists()):
                tasks.append((line, lang, category, wav_path, line_hash))

    # 2. Execute Tasks in Parallel
    if tasks:
        total_tasks = len(tasks)
        print(f"\n[INFO] Found {total_tasks} missing audio files. Generating in parallel...")
        
        start_time = time.time()
        completed = 0
        
        # Adjust max_workers based on your network/CPU comfort.
        # NOTE: severe rate-limiting by Google if too high (>5). Keeping it low for stability.
        def run_task_batch(batch_tasks, max_workers):
            nonlocal completed
            with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as executor:
                future_to_task = {executor.submit(process_single_file, task): task for task in batch_tasks}
                for future in concurrent.futures.as_completed(future_to_task):
                    completed += 1

                    elapsed = time.time() - start_time
                    if completed > 0:
                        avg_time = elapsed / completed
                        remaining = avg_time * (total_tasks - completed)
                    else:
                        remaining = 0

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

        tasks_de = [t for t in tasks if t[1] == 'de']
        tasks_en = [t for t in tasks if t[1] == 'en']

        if tasks_de:
            run_task_batch(tasks_de, GTTS_DE_MAX_WORKERS)
        if tasks_en:
            run_task_batch(tasks_en, 4)

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

    # Clean up old playlists before regenerating
    playlist_dir = SD_ROOT / "playlists"
    if playlist_dir.exists():
        for old_pl in playlist_dir.glob("cat_*_v3.m3u"):
            try:
                old_pl.unlink()
                print(f"  [DEL] Old playlist: {old_pl.name}")
            except Exception as e:
                print(f"  [WARN] Could not delete {old_pl.name}: {e}")
        
    def select_persona_category(persona_idx):
        preferred = PERSONA_DEFAULTS.get(persona_idx, "")
        if preferred in sorted_cats:
            return preferred
        if sorted_cats:
            return sorted_cats[(persona_idx - 1) % len(sorted_cats)]
        return ""

    def extract_category_from_wav_name(filename):
        stem = Path(filename).stem
        parts = stem.split('_')
        if len(parts) < 3:
            return stem
        return "_".join(parts[:-2])

    persona_assignments = {}

    for persona_idx in range(1, 6):
        default_name = ""
        
        # Smart Defaults
        if persona_idx == 1: default_name = next((c for c in sorted_cats if "Badran" in c), "")
        elif persona_idx == 5: default_name = next((c for c in sorted_cats if "Fortune" in c), "")

        if NON_INTERACTIVE:
            selected_cat = select_persona_category(persona_idx)
            if not selected_cat:
                print(f"  [WARN] No categories available for Persona {persona_idx}")
                continue
            print(f"  -> Assigned '{selected_cat}' to Persona {persona_idx} (auto)")
        else:
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
                    break
                else:
                    print("Invalid selection. Try again.")

        if selected_cat:
            persona_assignments[persona_idx] = selected_cat
            
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

            display_name = ""
            if playlist_tracks["en"]:
                display_name = extract_category_from_wav_name(Path(playlist_tracks["en"][0]).name)
            elif playlist_tracks["de"]:
                display_name = extract_category_from_wav_name(Path(playlist_tracks["de"][0]).name)
            if display_name:
                persona_assignments[persona_idx] = display_name
            
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
                    pl_filename = f"cat_{persona_idx}_{lang}_v3.m3u"
                    pl_path = playlist_dir / pl_filename
                    if pl_path.exists():
                        pl_path.unlink()
                        print(f"     Removed Playlist: {pl_filename} (0 tracks)")

    print(f"\nDone! SD Card content generated in: {SD_ROOT}")

    # phonebook.json generation removed; defaults are now in firmware.

def generate_tones(base_dir):
    """
    Generates standard telephony tones (Dial tone, Busy signal, etc.)
    """
    print("\n\n--- Generating Telephony Tones ---")
    target_dir = base_dir / "system"
    target_dir.mkdir(parents=True, exist_ok=True)
    
    # helper
    def save(seg, name):
        out_path = target_dir / name
        seg = seg.set_channels(1).set_sample_width(2).set_frame_rate(AUDIO_RATE)
        seg.export(str(out_path), format="wav")
        print(f"  [GEN] {name}")

    # 1. Dial Tone 1 (German standard: 425Hz continuous)
    # 10 seconds long
    dial_tone = Sine(425).to_audio_segment(duration=10000).apply_gain(-3.0)
    save(dial_tone, "dialtone_1.wav")

    # 2. Busy Tone (425Hz, 480ms ON, 480ms OFF)
    # Repeat for ~5 seconds -> (480+480) * 5 approx 5s
    # 5000 / 960 = 5.2 cycles. Let's do 6 cycles.
    tone_on = Sine(425).to_audio_segment(duration=480).apply_gain(-3.0)
    tone_off = AudioSegment.silent(duration=480)
    busy_cycle = tone_on + tone_off
    busy_tone = busy_cycle * 6 
    save(busy_tone, "busy_tone.wav")

    # 3. Beep (1000Hz, 200ms)
    beep = Sine(1000).to_audio_segment(duration=200).apply_gain(-3.0)
    save(beep, "beep.wav")

    # 3b. Silence (300ms)
    silence = AudioSegment.silent(duration=300)
    save(silence, "silence_300ms.wav")

    # 4. Error Tone (150Hz Sawtooth, 500ms)
    error = Sawtooth(150).to_audio_segment(duration=500).apply_gain(-3.0)
    save(error, "error_tone.wav")

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

    # 6. Zen Garden (Low Gong)
    print("  Synthesizing Zen Garden...")
    gong_fund = Sine(220).to_audio_segment(duration=3000).fade_out(2500)
    gong_over = Sine(220 * 1.6).to_audio_segment(duration=3000).fade_out(2500).apply_gain(-6)
    gong = gong_fund.overlay(gong_over).apply_gain(-2)
    save_tone(gong, "zen_garden.wav", -3.0)

    # 7. 8-Bit Powerup (Rapid Arpeggio)
    print("  Synthesizing 8-Bit Powerup...")
    notes = [440, 554, 659, 880, 1108, 1318] # A major arpeggio
    powerup = AudioSegment.silent(duration=0)
    for freq in notes:
        powerup += Square(freq).to_audio_segment(duration=60).fade_out(5)
    save_tone(powerup * 3, "8bit_powerup.wav", -6.0)

    # 8. Gentle Wakeup (Slow Swell)
    print("  Synthesizing Gentle Wakeup...")
    # Generate 3 sine waves slightly detuned for richness
    swell1 = Sine(330).to_audio_segment(duration=4000) # E natural
    swell2 = Sine(332).to_audio_segment(duration=4000)
    swell3 = Sine(440).to_audio_segment(duration=4000) # A natural
    
    pad = swell1.overlay(swell2).overlay(swell3)
    # Fade in 2s, Fade out 2s
    pad = pad.fade_in(2000).fade_out(2000)
    save_tone(pad, "gentle_wakeup.wav", -5.0)
    
    # 9. Funky Beat (Simple Bass-Kicks)
    print("  Synthesizing Funky Beat...")
    kick = Sine(60).to_audio_segment(duration=150).fade_out(20).apply_gain(2) # Low thump
    snare = WhiteNoise().to_audio_segment(duration=100).fade_out(20).apply_gain(-5) # Noise burst
    hat   = WhiteNoise().to_audio_segment(duration=30).fade_out(5).apply_gain(-10) # Short click
    
    silence_short = AudioSegment.silent(duration=100)
    silence_long = AudioSegment.silent(duration=250)
    
    # Kick... Hat.. Snare.. Hat..
    beat = kick + silence_short + hat + silence_short + snare + silence_short + hat + silence_short
    save_tone(beat * 4, "funky_beat.wav", -3.0)

    # 10. Ocean Waves (Filtered Noise Swells)
    print("  Synthesizing Ocean Waves...")
    # Layering varying low-passed white noise to simulate waves
    wave_dur = 6000
    ocean = AudioSegment.silent(duration=wave_dur)
    wave1 = WhiteNoise().to_audio_segment(duration=wave_dur).low_pass_filter(300).fade_in(2000).fade_out(2000).apply_gain(-3)
    wave2 = WhiteNoise().to_audio_segment(duration=wave_dur).low_pass_filter(600).fade_in(3000).fade_out(3000).apply_gain(-9)
    ocean = wave1.overlay(wave2)
    save_tone(ocean, "ocean_waves.wav", -3.0)

    # 11. Gentle Rain (Constant noise with steady high-cut)
    print("  Synthesizing Gentle Rain...")
    rain_dur = 5000
    rain = WhiteNoise().to_audio_segment(duration=rain_dur).low_pass_filter(1000)
    rain = rain.fade_in(500).fade_out(500).apply_gain(-6)
    save_tone(rain, "gentle_rain.wav", -3.0)

    # 11b. Pink Noise (approximated by low-pass filtering white noise)
    print("  Synthesizing Pink Noise...")
    pink_dur = 5000
    pink = WhiteNoise().to_audio_segment(duration=pink_dur)
    pink = pink.low_pass_filter(1200).low_pass_filter(1200)
    pink = pink.fade_in(200).fade_out(200).apply_gain(-8)
    save_tone(pink, "pink_noise.wav", -3.0)

    # 12. Wind Chimes (Random Pentatonic High Sines)
    print("  Synthesizing Wind Chimes...")
    chimes = AudioSegment.silent(duration=6000)
    # C Major Pentatonic: C6, D6, E6, G6, A6
    scale = [1046.5, 1174.6, 1318.5, 1567.9, 1760.0] 
    for _ in range(8):
        freq = random.choice(scale)
        start = random.randint(0, 4500)
        dur = random.randint(1000, 2000)
        note = Sine(freq).to_audio_segment(duration=dur).fade_out(dur).apply_gain(-12)
        chimes = chimes.overlay(note, position=start)
    save_tone(chimes, "wind_chimes.wav", -3.0)

    # 13. Cosmic Drift (Filtered Sawtooth Pad)
    print("  Synthesizing Cosmic Drift...")
    drift_dur = 6000
    # Left channel-ish freq and Right channel-ish freq mixed slightly detuned
    pad1 = Sawtooth(110).to_audio_segment(duration=drift_dur).low_pass_filter(300).fade_in(2000).fade_out(2000)
    pad2 = Sawtooth(111).to_audio_segment(duration=drift_dur).low_pass_filter(400).fade_in(2000).fade_out(2000)
    cosmic = pad1.overlay(pad2).apply_gain(-10)
    save_tone(cosmic, "cosmic_drift.wav", -3.0)

    # 14. Crystal Glass (Clean Sine Sequence)
    print("  Synthesizing Crystal Glass...")
    glass_notes = [523.25, 659.25, 783.99, 1046.50]
    glass = AudioSegment.silent(duration=2000 + len(glass_notes)*500)
    for i, freq in enumerate(glass_notes):
        tone = Sine(freq).to_audio_segment(duration=2000).fade_out(1500).apply_gain(-10)
        glass = glass.overlay(tone, position=i * 500)
    save_tone(glass, "crystal_glass.wav", -3.0)
    
    # 15. Singing Bowl (Harmonic constant)
    print("  Synthesizing Singing Bowl...")
    bowl_dur = 6000
    base = Sine(220).to_audio_segment(duration=bowl_dur).fade_in(100).fade_out(4000)
    # Add a non-integer harmonic for the metallic "beating" sound
    harm = Sine(220 * 2.3).to_audio_segment(duration=bowl_dur).fade_in(100).fade_out(3000).apply_gain(-12)
    bowl = base.overlay(harm).apply_gain(-3)
    save_tone(bowl, "singing_bowl.wav", -3.0)

    # 16. Happy Marimba (Short percussive sines)
    print("  Synthesizing Happy Marimba...")
    # C Major: C E G A C
    notes = [523, 659, 784, 880, 1046]
    marimba = AudioSegment.silent(duration=5000)
    for i in range(8):
        freq = random.choice(notes)
        start = i * 250
        # Short envelope simulating mallet hit
        tone = Sine(freq).to_audio_segment(duration=400).fade_out(300).apply_gain(-5)
        marimba = marimba.overlay(tone, position=start)
    save_tone(marimba, "happy_marimba.wav", -3.0)

    # 17. Sunrise Birds (High chirp simulation)
    print("  Synthesizing Sunrise Birds...")
    birds = AudioSegment.silent(duration=5000)
    for i in range(6):
        start = random.randint(0, 4000)
        dur = random.randint(100, 300)
        freq_start = random.randint(2000, 3000)
        freq_end = random.randint(1500, 2500)
        # Chirp is a sine sweep
        # Since pydub doesn't have sweeps, we approximate with short segments or just high sine
        chirp = Sine(freq_start).to_audio_segment(duration=dur).fade_in(10).fade_out(50).apply_gain(-12)
        birds = birds.overlay(chirp, position=start)
    save_tone(birds, "sunrise_birds.wav", -6.0)

    # 18. Bouncy Synth (Square with fast envelope)
    print("  Synthesizing Bouncy Synth...")
    bouncy = AudioSegment.silent(duration=4000)
    b_notes = [261, 329, 392, 523] # C E G C
    for i in range(8):
        freq = b_notes[i % 4]
        start = i * 400
        # Square wave for retro feel
        tone = Square(freq).to_audio_segment(duration=200).fade_out(100).apply_gain(-10)
        bouncy = bouncy.overlay(tone, position=start)
    save_tone(bouncy, "bouncy_synth.wav", -6.0)

def generate_wake_up_rants():
    """
    Generates sassy wake-up quotes using Piper.
    """
    print("\n\n--- Generating Sassy Wake Up Quotes (Piper) ---")
    target_dir = SD_ROOT / "ringtones"
    target_dir.mkdir(parents=True, exist_ok=True)
    
    # Text, Filename, Lang, Model
    rants = [
        # German (Sassy/Frech) - Thorsten
        ("Aufstehen, Sonnenschein! Oder auch nicht, mir egal. Aber der Wecker sagt ja.", "rant_de_sunshine.wav", "de", None),
        ("Der frühe Vogel kann mich mal... aber du musst leider raus. Hopp hopp!", "rant_de_bird.wav", "de", None),
        ("Guten Morgen! Dein Bett hat gerade angerufen, es will sich von dir trennen.", "rant_de_breakup.wav", "de", None),
        ("Alarm! Alarm! Die Realität ruft an. Leider kannst du das nicht wegdrücken.", "rant_de_reality.wav", "de", None),
        ("Na, auch schon wach? Ich mache das hier nur, weil ich programmiert wurde, dich zu nerven.", "rant_de_annoy.wav", "de", None),
        
        # English (Sassy) - Jenny
        ("Rise and shine! The world awaits your glorious confusion.", "rant_en_confusion.wav", "en", None),
        ("Wake up! Your bed called, it's dumping you for the day.", "rant_en_breakup.wav", "en", None),
        ("Good morning starshine! The earth says hello... loudly!", "rant_en_starshine.wav", "en", None),
        ("I surrender! I surrender! Oh wait, it's just morning. Get up.", "rant_en_surrender.wav", "en", None),
        ("Alert! Alert! You are critically low on caffeine. Initiate wake up sequence.", "rant_en_caffeine.wav", "en", None),
    ]
    
    for text, fname, lang, model in rants:
        if lang not in ENABLED_LANGS:
            continue
        out_path = target_dir / fname
        if not out_path.exists():
            try:
                # generate_speech handles format conversion now
                generate_speech(text, lang, out_path, model_name=model)
                print(f"  [GEN] {fname}")
            except Exception as e:
                print(f"    [ERR] Failed to generate {fname}: {e}")
        else:
             print(f"  [SKP] {fname} (exists)")




def generate_system_sounds():
    """Generates mapped system prompts using Piper AND copies static files."""
    print("\n\n--- Generating System Sounds & Time Announcements ---")
    
    # 1. Generate Static System Prompts
    def process_system_prompt(item):
        rel_path, (text, lang, model) = item
        if lang not in ENABLED_LANGS:
            return
        out_path = SD_ROOT / rel_path
        out_path.parent.mkdir(parents=True, exist_ok=True)
        
        if not out_path.exists():
            # print(f"  [GEN] {rel_path}...")
            try:
                # generate_speech handles format conversion now
                generate_speech(text, lang, out_path, model_name=model)
                print(f"  [OK] {rel_path}")
            except Exception as e:
                print(f"    [ERR] Failed to generate {rel_path}: {e}")

    print(f"  [PLAN] Generating {len(SYSTEM_PROMPTS)} system prompts...")
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        list(executor.map(process_system_prompt, SYSTEM_PROMPTS.items()))

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
        if path.exists(): return False # Skipped
        
        temp_path = path.with_suffix(".tmp.wav")
        try:
             generate_speech(text, lang, temp_path, model_name=model)
             sound = AudioSegment.from_wav(str(temp_path))
             sound = sound.set_channels(1).set_sample_width(2).set_frame_rate(AUDIO_RATE)
             sound.export(str(path), format="wav")
             if temp_path.exists(): os.remove(temp_path)
             return True
        except Exception as e:
             safe_print(f"    [ERR] {path.name}: {e}")
             if temp_path.exists(): os.remove(temp_path)
             return False

    completed = 0
    start_time = time.time()

    def run_time_batch(batch_tasks, max_workers):
        nonlocal completed
        with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as executor:
            future_to_task = {executor.submit(process_time_task, t): t for t in batch_tasks}

            for future in concurrent.futures.as_completed(future_to_task):
                completed += 1
                if completed % 1 == 0:
                    elapsed = time.time() - start_time
                    if completed > 0:
                        avg_time = elapsed / completed
                        remaining = avg_time * (total_time - completed)
                    else:
                        remaining = 0

                    percent = (completed / total_time) * 100
                    print(f"\r  [GEN] {completed}/{total_time} ({percent:.1f}%) | ETA: {format_time(remaining)}   ", end='', flush=True)

    time_tasks_de = [t for t in time_tasks if t[1] == 'de']
    time_tasks_en = [t for t in time_tasks if t[1] == 'en']

    if time_tasks_de:
        run_time_batch(time_tasks_de, GTTS_DE_MAX_WORKERS)
    if time_tasks_en:
        run_time_batch(time_tasks_en, 4)
    print() # Newline


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
    fonts_dest = SD_ROOT / "fonts"
    if fonts_src.exists():
         fonts_dest.mkdir(parents=True, exist_ok=True)
         for font in fonts_src.glob("*.*"):
             dest_file = fonts_dest / font.name
             if not dest_file.exists():
                 print(f"  [COPY] Font {font.name}")
                 shutil.copy2(font, dest_file)

    # 3c. (DISABLED) web_ui is embedded in Flash now.
    # web_ui_src = PROJECT_ROOT / "main" / "web_ui"
    # web_ui_dest = SD_ROOT / "data"
    # if web_ui_src.exists():
    #      web_ui_dest.mkdir(parents=True, exist_ok=True)
    #      for asset in web_ui_src.glob("*.*"):
    #          dest_file = web_ui_dest / asset.name
    #          shutil.copy2(asset, dest_file)
    #          print(f"  [COPY] Web UI {asset.name}")
    # else:
    #      print(f"  [WARN] Web UI source missing: {web_ui_src}")

    # 4. Copy Ringtones (From multiple potential sources)
    ringtones_dest = SD_ROOT / "ringtones"
    ringtones_dest.mkdir(parents=True, exist_ok=True)
    
    # Check both root/ringtones and effects/ringtones
    ringtone_sources = [
        PROJECT_ROOT / "ringtones",
        PROJECT_ROOT / "effects" / "ringtones"
    ]
    
    for r_src in ringtone_sources:
        if r_src.exists():
            print(f"  [SCAN] Checking {r_src}...")
            for wav in r_src.glob("*.wav"):
                dest_file = ringtones_dest / wav.name
                if not dest_file.exists():
                    print(f"  [COPY] Ringtone {wav.name}")
                    shutil.copy2(wav, dest_file)
    
    print("System sounds update complete.")


def main():
    try:
        categories = generate_audio_cache()
        generate_sd_card_structure(categories)
        # Procedural ringtones disabled; use curated assets only.
        generate_wake_up_rants()
        generate_tones(SD_ROOT)
        generate_system_sounds()
    except KeyboardInterrupt:
        print("\nAborted.")

if __name__ == "__main__":
    main()
