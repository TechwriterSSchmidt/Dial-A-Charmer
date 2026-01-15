import wave
import math
import struct
import os
import random
import subprocess
import shutil

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

def generate_startup_sound(output_path, ffmpeg_path="ffmpeg.exe"):
    print(f"Generating Startup Sound (Windows-ish Pad)...")
    
    # Windows 95-ish "Ambient Swell" (E-flat Major)
    # Frequencies: Eb3, Bb3, Eb4, G4, Bb4, Eb5
    chord_freqs = [155.56, 233.08, 311.13, 392.00, 466.16, 622.25]
    duration = 6.0 # seconds
    
    wav_filename = output_path.replace(".mp3", ".wav")
    
    num_samples = int(SAMPLE_RATE * duration)
    data = []
    
    for i in range(num_samples):
        # Time progression (0.0 to 1.0)
        t = i / num_samples
        
        # Envelope: Slow attack (2s), Sustain, Slow Decay (2s)
        env = 1.0
        if t < 0.33: # Attack
            env = t / 0.33
        elif t > 0.66: # Decay
            env = (1.0 - t) / 0.34
            
        # Mix oscillators
        mixed_sample = 0.0
        for freq in chord_freqs:
            # Add slight detune for "width"
            osc_val = math.sin(2.0 * math.pi * freq * i / SAMPLE_RATE)
            mixed_sample += osc_val
        
        # Normalize (divide by num of oscillators)
        mixed_sample = mixed_sample / len(chord_freqs)
        
        # Apply Envelope and Volume
        final_sample = int(mixed_sample * AMPLITUDE * env)
        data.append(struct.pack('<h', final_sample))

    # Write WAV
    with wave.open(wav_filename, 'w') as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(SAMPLE_RATE)
        wav_file.writeframes(b''.join(data))
        
    # Convert to MP3 using local ffmpeg
    if os.path.exists(wav_filename):
        # Look for ffmpeg in current dir
        if not os.path.exists(ffmpeg_path) and shutil.which("ffmpeg"):
            ffmpeg_cmd = "ffmpeg"
        else:
            ffmpeg_cmd = os.path.abspath(ffmpeg_path)
            
        print(f"Converting {wav_filename} to {output_path} using {ffmpeg_cmd}...")
        try:
            # Overwrite if exists (-y)
            subprocess.run([ffmpeg_cmd, "-y", "-i", wav_filename, output_path], 
                         check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print("Conversion successful.")
            os.remove(wav_filename) # Cleanup WAV
        except Exception as e:
            print(f"Error converting to MP3: {e}. Keeping WAV file.")
            # Rename WAV to MP3 variable to at least have the file? 
            # No, user needs .mp3. If ffmpeg fails, we leave .wav and warn.

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

    # 5. Startup Sound (MP3)
    startup_path = os.path.join(project_root, "sd_card_template", "startup.mp3")
    ffmpeg_exe = os.path.join(script_dir, "ffmpeg.exe")
    generate_startup_sound(startup_path, ffmpeg_exe)



