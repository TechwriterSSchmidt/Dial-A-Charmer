from pydub import AudioSegment
import os
import shutil

# Configuration
SOURCE_FILE = "freesound_community-phone-hang-up-46793.mp3"
UTILS_DIR = os.path.dirname(os.path.abspath(__file__))
FILE_PATH = os.path.join(UTILS_DIR, SOURCE_FILE)

# Destinations
PROJECT_ROOT = os.path.dirname(UTILS_DIR)
STATIC_SYSTEM_DIR = os.path.join(PROJECT_ROOT, "static_assets", "system")
SD_SYSTEM_DIR = os.path.join(PROJECT_ROOT, "sd_card_content", "system")

def process_new_hangup():
    if not os.path.exists(FILE_PATH):
        print(f"Error: Source file '{SOURCE_FILE}' not found in utils folder.")
        return

    print(f"Loading {SOURCE_FILE}...")
    audio = AudioSegment.from_mp3(FILE_PATH)
    
    # Normalize
    audio = audio.normalize()

    # Convert settings: Mono, 16-bit, 44100Hz
    audio = audio.set_channels(1).set_sample_width(2).set_frame_rate(44100)

    # Export to Static Assets (Golden Master)
    os.makedirs(STATIC_SYSTEM_DIR, exist_ok=True)
    static_path = os.path.join(STATIC_SYSTEM_DIR, "hook_hangup.wav")
    audio.export(static_path, format="wav")
    print(f"Updated static asset: {static_path}")

    # Export to SD Card Content (Ready to use)
    os.makedirs(SD_SYSTEM_DIR, exist_ok=True)
    sd_path = os.path.join(SD_SYSTEM_DIR, "hook_hangup.wav")
    audio.export(sd_path, format="wav")
    print(f"Updated SD card content: {sd_path}")

if __name__ == "__main__":
    process_new_hangup()
