import os
import subprocess
import re
import sys
import shutil

# --- CONFIGURATION ---
FFMPEG_BIN = "ffmpeg"    # Or path to ffmpeg.exe if not in PATH
SILENCE_DB = -25         # Threshold for silence detection (dB) - Increased for noisy recordings
MIN_SILENCE_DUR = 0.4    # Minimum silence duration to trigger split (seconds)

def check_ffmpeg():
    """Checks if ffmpeg is available."""
    # First check if local ffmpeg.exe exists in script dir
    script_dir = os.path.dirname(os.path.abspath(__file__))
    local_ffmpeg = os.path.join(script_dir, "ffmpeg.exe")
    if os.path.exists(local_ffmpeg):
        return local_ffmpeg
    
    # Check PATH
    if shutil.which("ffmpeg") is not None:
        return "ffmpeg"
        
    return None

def get_silence_splits(ffmpeg_cmd, file_path):
    """
    Analyzes the file and returns timestamps for splitting.
    Uses 'silencedetect' filter from FFmpeg.
    """
    # Use a highpass filter before silence detection to ignore low-frequency rumble/noise
    # This helps with noisy recordings where the "silence" isn't perfectly quiet.
    filter_chain = f"highpass=f=200,silencedetect=noise={SILENCE_DB}dB:d={MIN_SILENCE_DUR}"
    
    cmd = [
        ffmpeg_cmd, "-i", file_path, 
        "-af", filter_chain,
        "-f", "null", "-"
    ]
    
    # FFmpeg writes log output to stderr
    try:
        result = subprocess.run(cmd, stderr=subprocess.PIPE, text=True, check=True)
    except subprocess.CalledProcessError as e:
        print(f"Error analyzing file: {e}")
        return ""
    
    starts = []
    ends = []
    
    # Parse output
    for line in result.stderr.splitlines():
        if "silence_start" in line:
            match = re.search(r'silence_start: ([\d\.]+)', line)
            if match: starts.append(float(match.group(1)))
        elif "silence_end" in line:
            match = re.search(r'silence_end: ([\d\.]+)', line)
            if match: ends.append(float(match.group(1)))

    split_points = []
    
    # Cut towards the end of the silence to avoid previous sound tails
    count = min(len(starts), len(ends))
    for i in range(count):
        duration = ends[i] - starts[i]
        # Move cut point closer to the next sound (silence end) to avoid tails of previous sound.
        # But ensure we keep a small buffer before the next attack.
        # Logic: Cut at 80% of the silence duration, or at least 0.2s before end.
        
        buffer = min(0.2, duration * 0.25) # Don't go closer than 25% of gap or 0.2s
        cut_point = ends[i] - buffer
        
        split_points.append(f"{cut_point:.3f}")
        
    return ",".join(split_points)

def process_file(ffmpeg_cmd, input_file, output_dir):
    """Processes a single audio file (WAV or MP3)."""
    filename = os.path.basename(input_file)
    # Be careful when dealing with mixed cases, ffmpeg is usually case-insensitive on windows for inputs
    # but we want controlled outputs.
    name, ext = os.path.splitext(filename)
    ext_lower = ext.lower()
    
    if ext_lower not in ['.mp3', '.wav']:
        return # Ignore other files

    print(f"Analyzing: {filename} ...")
    
    splits = get_silence_splits(ffmpeg_cmd, input_file)
    
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # Output Pattern: name_000.wav, name_001.wav etc.
    out_pattern = os.path.join(output_dir, f"{name}_%03d{ext_lower}")
    
    cmd_split = [ffmpeg_cmd, "-y", "-i", input_file, "-f", "segment"]
    
    if splits:
        print(f"  -> {len(splits.split(',')) + 1} segments detected.")
        cmd_split.extend(["-segment_times", splits])
    else:
        print("  -> No silence found. Copying file 1:1 (converted).")
        # If no split, we still want to convert/copy with correct format
        # but segment muxer requires segmentation.
        # Fallback to simple conversion for single file
        out_single = os.path.join(output_dir, f"{name}_001{ext_lower}")
        cmd_convert = [ffmpeg_cmd, "-y", "-i", input_file]
        
        if ext_lower == '.wav':
             cmd_convert.extend(["-c:a", "pcm_s16le", "-ar", "44100", "-ac", "1"])
        else:
             cmd_convert.extend(["-c", "copy"])
             
        cmd_convert.append(out_single)
        subprocess.run(cmd_convert, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return

    # Format-Specific Settings for Segment Muxer
    if ext_lower == '.wav':
        # IMPORTANT for ESP32: WAVs must be encoded cleanly (PCM 16-bit, 44.1kHz, Mono)
        # '-c copy' can issues with WAV headers or wrong codecs (e.g. float), so we re-encode.
        cmd_split.extend([
            "-c:a", "pcm_s16le", 
            "-ar", "44100", 
            "-ac", "1"
        ])
    else:
        # For MP3 copy is usually fine (faster), 
        # but reset_timestamps is important for clean playback start
        cmd_split.extend(["-c", "copy"])
        
    cmd_split.extend(["-reset_timestamps", "1", out_pattern])
    
    # Execute
    subprocess.run(cmd_split, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"  -> Done. Files in '{output_dir}'")

if __name__ == "__main__":
    print("=== Audio Splitter Tool (WAV & MP3) ===")
    
    ffmpeg_cmd = check_ffmpeg()
    if not ffmpeg_cmd:
        print("ERROR: ffmpeg not found. Please install ffmpeg or place ffmpeg.exe in this folder.")
        sys.exit(1)
    
    print(f"Using FFmpeg: {ffmpeg_cmd}")
        
    if len(sys.argv) < 3:
        print("\nUsage: python split_audio.py <SOURCE_FOLDER> <TARGET_FOLDER>")
        print("Example:   python split_audio.py ./raw_recordings ./sd_card_template/persona_01")
        sys.exit(1)
        
    in_dir = sys.argv[1]
    out_dir = sys.argv[2]
    
    if not os.path.exists(in_dir):
        print(f"Source folder '{in_dir}' does not exist.")
        sys.exit(1)
        
    files_found = 0
    for f in os.listdir(in_dir):
        full_path = os.path.join(in_dir, f)
        if os.path.isfile(full_path) and f.lower().endswith(('.mp3', '.wav')):
            process_file(ffmpeg_cmd, full_path, out_dir)
            files_found += 1
            
    if files_found == 0:
        print("No .mp3 or .wav files found in source folder.")
    else:
        print("\nAll done.")

