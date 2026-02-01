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
    # Check system PATH (Standard on Linux)
    if shutil.which("ffmpeg") is not None:
        return "ffmpeg"
        
    # Check for local binary in script folder
    script_dir = os.path.dirname(os.path.abspath(__file__))
    local_bin = os.path.join(script_dir, "ffmpeg")
    if os.path.exists(local_bin):
        return local_bin
        
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
    """Processes a single audio file (WAV or MP3) and converts it to clean WAV segments."""
    filename = os.path.basename(input_file)
    name, ext = os.path.splitext(filename)
    ext_lower = ext.lower()
    
    if ext_lower not in ['.mp3', '.wav']:
        return # Ignore other files

    print(f"Analyzing: {filename} ...")
    
    splits = get_silence_splits(ffmpeg_cmd, input_file)
    
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # Force WAV Output Pattern: name_000.wav, name_001.wav
    out_pattern = os.path.join(output_dir, f"{name}_%03d.wav")
    
    cmd_split = [ffmpeg_cmd, "-y", "-i", input_file, "-f", "segment"]
    
    if splits:
        print(f"  -> {len(splits.split(',')) + 1} segments detected.")
        cmd_split.extend(["-segment_times", splits])
    else:
        print("  -> No silence found. Converting file to standard WAV.")
        # Fallback to simple conversion for single file
        out_single = os.path.join(output_dir, f"{name}_001.wav")
        cmd_convert = [
            ffmpeg_cmd, "-y", "-i", input_file,
            "-c:a", "pcm_s16le", "-ar", "44100", "-ac", "1",
            out_single
        ]
        subprocess.run(cmd_convert, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return

    # Standardize Output Format for Segments (PCM s16le, 44.1k, Mono)
    cmd_split.extend([
        "-c:a", "pcm_s16le", 
        "-ar", "44100", 
        "-ac", "1",
        "-reset_timestamps", "1", 
        out_pattern
    ])
    
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
        
    # Default: Scan the directory where the script is located
    script_dir = os.path.dirname(os.path.abspath(__file__))
    source_path = script_dir
    dest_dir = script_dir

    # Overwrite if arguments provided
    if len(sys.argv) >= 2:
        source_path = sys.argv[1]
    if len(sys.argv) >= 3:
        dest_dir = sys.argv[2]
    
    if not os.path.exists(source_path):
        print(f"Error: Source '{source_path}' does not exist.")
        sys.exit(1)

    print(f"Source: {source_path}")
    print(f"Target: {dest_dir}")

    # Logic: Handle Single File OR Directory
    if os.path.isfile(source_path):
        # Single File Mode
        if source_path.lower().endswith(('.mp3', '.wav')):
            process_file(ffmpeg_cmd, source_path, dest_dir)
        else:
            print("Ignored: File is not .mp3 or .wav")

    elif os.path.isdir(source_path):
        # Directory Mode
        files_found = 0
        for f in os.listdir(source_path):
            full_path = os.path.join(source_path, f)
            # Skip generated files (ending in _000.wav etc) to prevent recursion/double processing
            if re.search(r'_\d{3}\.wav$', f):
                continue

            if os.path.isfile(full_path) and f.lower().endswith(('.mp3', '.wav')):
                process_file(ffmpeg_cmd, full_path, dest_dir)
                files_found += 1
                
        if files_found == 0:
            print("No suitable .mp3 or .wav files found in source folder.")
    
    print("\nAll done.")

