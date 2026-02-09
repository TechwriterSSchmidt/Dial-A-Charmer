from pydub import AudioSegment
from pydub.silence import split_on_silence
import os

# Configuration
SOURCE_FILE = "freesound_community-rotary-phone-pick-up-and-slam-down-37543 (1).mp3"
TARGET_DIR = "../sd_card_content/system"
UTILS_DIR = os.path.dirname(os.path.abspath(__file__))
FILE_PATH = os.path.join(UTILS_DIR, SOURCE_FILE)

def process_hooks():
    if not os.path.exists(FILE_PATH):
        print(f"Error: Source file '{SOURCE_FILE}' not found in utils folder.")
        return

    print(f"Loading {SOURCE_FILE}...")
    audio = AudioSegment.from_mp3(FILE_PATH)
    
    # Normalize
    audio = audio.normalize()

    # Split on silence
    # Adjust thresholds if necessary. 
    # Usually "Pick up and slam down" has a distinct pause in between.
    chunks = split_on_silence(
        audio, 
        min_silence_len=200, # ms
        silence_thresh=-35   # dB
    )

    print(f"Found {len(chunks)} audio chunks.")

    if len(chunks) >= 2:
        # Assuming Chunk 0 = Pickup, Chunk 1 (+ others?) = Hangup
        # Often recordings might have a small noise at start, but let's assume biggest chunks.
        # Or just take first and last if multiple?
        # Let's try simpler: take the first two chunks.
        
        pickup = chunks[0]
        hangup = chunks[1]
        
        # If there are more chunks (e.g. echoes), maybe append them or ignore?
        # "Pick up" is usually short. "Hang up" might be "Klack-Klack".
        if len(chunks) > 2:
            print("Warning: More than 2 sounds detected. Using first two.")
            # If the hangup is 'clack-clack' and split_on_silence split them, we might need to join
            # But let's start with 0 and 1.

        os.makedirs(TARGET_DIR, exist_ok=True)
        
        # Export settings: Mono, 16-bit, 44100Hz (standard for this project)
        pickup = pickup.set_channels(1).set_sample_width(2).set_frame_rate(44100)
        hangup = hangup.set_channels(1).set_sample_width(2).set_frame_rate(44100)

        pickup.export(os.path.join(TARGET_DIR, "hook_pickup.wav"), format="wav")
        hangup.export(os.path.join(TARGET_DIR, "hook_hangup.wav"), format="wav")
        
        print("Success!")
        print(f"Exported: {os.path.join(TARGET_DIR, 'hook_pickup.wav')}")
        print(f"Exported: {os.path.join(TARGET_DIR, 'hook_hangup.wav')}")
        
    else:
        print("Error: Could not detect 2 distinct sounds. Try adjusting silence thresholds.")
        print(f"Audio length: {len(audio)}ms")

if __name__ == "__main__":
    process_hooks()
