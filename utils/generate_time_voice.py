import os
import win32com.client as wincl

# Configuration
OUTPUT_DIR = os.path.join("sd_card_template", "time")
LANG = "DE" # DE or EN

def ensure_dir(path):
    if not os.path.exists(path):
        os.makedirs(path)

def generate_speech(text, filename):
    filepath = os.path.join(OUTPUT_DIR, filename)
    if os.path.exists(filepath):
        print(f"Skipping {filename}")
        return

    print(f"Generating '{text}' -> {filename}...")
    
    # Use Windows SAPI
    speaker = wincl.Dispatch("SAPI.SpVoice")
    
    # Create a file stream
    fs = wincl.Dispatch("SAPI.SpFileStream")
    fs.Open(filepath, 3, False) # 3 = SSFMCreateForWrite
    
    # Connect speaker to file stream
    speaker.AudioOutputStream = fs
    speaker.Speak(text)
    fs.Close()

def main():
    # Determine project root
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    global OUTPUT_DIR
    OUTPUT_DIR = os.path.join(project_root, "sd_card_template", "time")
    
    ensure_dir(OUTPUT_DIR)
    
    print("Generating Time Announcement Files (WAV format)...")
    print("Note: You may need to convert these to MP3 or use WAV depending on your ESP32 config.")
    
    # 1. Intro
    generate_speech("Es ist", "intro.wav")
    generate_speech("Uhr", "uhr.wav")
    
    # 2. Hours (0-23)
    # Special case: "Ein" Uhr vs "Eins"
    for i in range(24):
        text = str(i)
        if i == 1: text = "ein"
        generate_speech(text, f"h_{i}.wav")
        
    # 3. Minutes (0-59)
    # 0 is usually silence or often omitted "14 Uhr" vs "14 Uhr Null" -> usually "14 Uhr"
    generate_speech("null", "m_0.wav") 
    for i in range(1, 60):
        text = str(i)
        # Special padding? No, "fünf" is fine.
        if i < 10:
             text = f"{i}" # "fünf"
             # Optionally "null fünf"? -> "Es ist 14 Uhr null fünf"
             generate_speech(f"null {i}", f"m_0{i}.wav")
        
        generate_speech(str(i), f"m_{i}.wav")

    print("Done. Files in sd_card_template/time/")

if __name__ == "__main__":
    main()
