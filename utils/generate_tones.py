import wave
import math
import struct
import os
import random

SAMPLE_RATE = 44100
AMPLITUDE = 16000 # Max 32767

def generate_tone(filename, frequency, duration_sec, pulse_on_ms=0, pulse_off_ms=0):
    print(f"Generating {filename} ({frequency}Hz)...")
    
    num_samples = int(SAMPLE_RATE * duration_sec)
    
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(1) # Mono
        wav_file.setsampwidth(2) # 16-bit
        wav_file.setframerate(SAMPLE_RATE)
        
        data = []
        
        # Period for pulsing
        pulse_period_samples = 0
        on_samples = 0
        if pulse_on_ms > 0:
            pulse_period_samples = int(SAMPLE_RATE * (pulse_on_ms + pulse_off_ms) / 1000.0)
            on_samples = int(SAMPLE_RATE * pulse_on_ms / 1000.0)
            
        for i in range(num_samples):
            # Pulsing Logic
            is_silent = False
            if pulse_period_samples > 0:
                cycle_pos = i % pulse_period_samples
                if cycle_pos >= on_samples:
                    is_silent = True
            
            if is_silent:
                sample = 0
            else:
                # Sine Wave
                # standard sine
                val = math.sin(2.0 * math.pi * frequency * i / SAMPLE_RATE)
                
                # "Authentic Wobbling" mentioned in ROTARI
                # Mix small amount of 50Hz or similar? 
                # ROTARI says "authentic wobbling and background noise"
                # Let's add slight 50Hz hum (mains hum)
                hum = 0.05 * math.sin(2.0 * math.pi * 50 * i / SAMPLE_RATE)
                
                # And minimal noise?
                noise = 0.02 * (random.random() - 0.5)
                
                sample = int((val + hum + noise) * AMPLITUDE)
                # Clip
                if sample > 32767: sample = 32767
                if sample < -32768: sample = -32768
                
            data.append(struct.pack('<h', sample))
            
        wav_file.writeframes(b''.join(data))

if __name__ == "__main__":
    # Determine project root relative to this script
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    out_dir = os.path.join(project_root, "sd_card_template", "system")
    
    if not os.path.exists(out_dir):
        os.makedirs(out_dir)

    # 1. Dial Tone (Continuous 425Hz) - 10 Seconds
    generate_tone(os.path.join(out_dir, "dial_tone.wav"), 425, 10)
    
    # 2. Busy Tone (425Hz, 480ms ON, 480ms OFF) - 10 Seconds
    generate_tone(os.path.join(out_dir, "busy_tone.wav"), 425, 10, 480, 480)
    
    # 3. Error / Off-Hook Warning (Fast Busy: 200ms/200ms)
    generate_tone(os.path.join(out_dir, "error_tone.wav"), 425, 5, 200, 200)

    # 4. Success Beep / Timer Set (1000Hz, single beep 200ms)
    generate_tone(os.path.join(out_dir, "beep.wav"), 1000, 0.2)



