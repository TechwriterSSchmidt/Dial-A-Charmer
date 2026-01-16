import wave
import math
import struct
import random
import os
import urllib.request
import urllib.parse
import ssl

# Configuration
SAMPLE_RATE = 44100
AMPLITUDE = 16000  # 16-bit PCM (max 32767)
TARGET_DIR = r"..\sd_card_template\system"

def ensure_dir(directory):
    if not os.path.exists(directory):
        os.makedirs(directory)

def generate_tts_mp3(text, filename, lang='de'):
    """Download TTS audio from Google Translate"""
    ensure_dir(TARGET_DIR)
    path = os.path.join(TARGET_DIR, filename)
    print(f"Downloading TTS: '{text}' -> {filename}...")
    
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
        print("Done.")
    except Exception as e:
        print(f"Failed to generate TTS: {e}")

def save_wav(filename, samples):
    ensure_dir(TARGET_DIR)
    path = os.path.join(TARGET_DIR, filename)
    print(f"Generating {path}...")
    
    with wave.open(path, 'w') as wav_file:
        # 1 Channel (Mono), 2 bytes (16-bit), Sample Rate
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(SAMPLE_RATE)
        
        # Pack samples
        packed_data = bytearray()
        for s in samples:
            # Clip
            s = max(min(int(s), 32767), -32768)
            packed_data.extend(struct.pack('<h', s))
            
        wav_file.writeframes(packed_data)
    print("Done.")

def generate_tone(frequency, duration_ms, wave_type='sine'):
    num_samples = int(SAMPLE_RATE * duration_ms / 1000.0)
    samples = []
    for i in range(num_samples):
        t = float(i) / SAMPLE_RATE
        if wave_type == 'sine':
            val = AMPLITUDE * math.sin(2 * math.pi * frequency * t)
        elif wave_type == 'square':
            val = AMPLITUDE if math.sin(2 * math.pi * frequency * t) > 0 else -AMPLITUDE
        elif wave_type == 'saw':
            # Simple saw approximation
            period = 1.0 / frequency
            phase = (t % period) / period
            val = AMPLITUDE * (2 * phase - 1)
        samples.append(val)
    return samples

def generate_silence(duration_ms):
    num_samples = int(SAMPLE_RATE * duration_ms / 1000.0)
    return [0] * num_samples

def make_computing_sound():
    # Retro "Thinking" Sound: Rapid sequence of random bleeps
    audio = []
    total_duration = 3000 # 3 seconds
    current_time = 0
    
    while current_time < total_duration:
        # Random duration for each blip (30ms to 80ms)
        dur = random.randint(30, 80)
        # Random frequency (800Hz to 2500Hz)
        freq = random.randint(800, 2500)
        
        # Random wave type for texture
        wtype = random.choice(['square', 'sine'])
        
        audio.extend(generate_tone(freq, dur, wtype))
        
        # Tiny silence between blips (5-20ms)
        pause = random.randint(5, 20)
        audio.extend(generate_silence(pause))
        
        current_time += (dur + pause)
        
    save_wav("computing.wav", audio)

def make_battery_low_sound():
    # Descending "Power Down" Slide
    audio = []
    duration = 1000 # 1 second
    num_samples = int(SAMPLE_RATE * duration / 1000.0)
    
    start_freq = 800.0
    end_freq = 100.0
    
    for i in range(num_samples):
        progress = float(i) / num_samples
        # Linear frequency slide
        current_freq = start_freq + (end_freq - start_freq) * progress
        # Wobble it a bit (Ambulance style LFO)
        lfo = 1.0 + 0.1 * math.sin(2 * math.pi * 10 * (float(i)/SAMPLE_RATE))
        
        t = float(i) / SAMPLE_RATE
        # Integral of frequency function is needed for phase, but for simple slide sin(2*pi*f*t) drifts.
        # Correct way is accumulating phase.
        
        # Simplified "good enough" approach for short sounds often works, 
        # but let's do phase accumulation for clean slide
        pass 
    
    # Re-doing slide with phase accumulator
    phase = 0.0
    audio = []
    
    # Part 1: "Uh-Oh" (High to Low slide)
    for i in range(num_samples):
        progress = float(i) / num_samples
        freq = start_freq * (1.0 - progress) + end_freq * progress
        
        phase += freq / SAMPLE_RATE
        val = AMPLITUDE * math.sin(2 * math.pi * phase)
        
        # Fade out volume
        vol_env = 1.0 - (progress * progress) # ease out
        
        audio.append(val * vol_env)
        
    # Append a final low thump
    audio.extend(generate_silence(200))
    audio.extend(generate_tone(80, 400, 'square'))
        
    save_wav("battery_low.wav", audio)

def make_system_beeps():
    # Standard UI Beep (Short, high)
    save_wav("beep.wav", generate_tone(1200, 50, 'sine'))
    
    # Error Tone (Low, abrasive)
    # Mix of two low frequencies for dissonance
    freq1 = 150
    freq2 = 180
    duration = 400
    num_samples = int(SAMPLE_RATE * duration / 1000.0)
    samples = []
    
    for i in range(num_samples):
        t = float(i) / SAMPLE_RATE
        # Sawtooth-ish approximation
        val1 = AMPLITUDE * 0.5 * (math.sin(2 * math.pi * freq1 * t) + 0.5 * math.sin(2 * math.pi * freq1 * 2 * t))
        val2 = AMPLITUDE * 0.5 * (math.sin(2 * math.pi * freq2 * t) + 0.5 * math.sin(2 * math.pi * freq2 * 2 * t))
        samples.append((val1 + val2) / 2)
        
    save_wav("error_tone.wav", samples)

if __name__ == "__main__":
    print("Generating classic retro UI sounds...")
    make_computing_sound()
    make_battery_low_sound()
    make_system_beeps()
    
    # Generate Voice Messages (Extra system messages)
    generate_tts_mp3("Warnung. Energiezellen kritisch.", "battery_crit.mp3")
    generate_tts_mp3("System bereit.", "system_ready.mp3")
    
    print("Files created in sd_card_template/system/")
