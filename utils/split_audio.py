import os
import subprocess
import shutil
import argparse

# INSTRUCTIONS:
# 1. Download ffmpeg (https://www.gyan.dev/ffmpeg/builds/ -> release-essentials)
# 2. Extract 'ffmpeg.exe' from the bin folder into THIS folder (where this script is).
# 3. Place your mp3 files in this folder (e.g. 'compliments.mp3', 'part2.mp3').
# 4. Run: python split_audio.py -o my_output_folder
#    The script will process ALL mp3 files in the current folder.
#    It automatically numbers output files consecutively (e.g. 1.mp3, 2.mp3...) 
#    without overwriting existing files in the output folder.

SILENCE_THRESH = "-25dB" 
MIN_SILENCE_DUR = "0.5"

def check_ffmpeg():
    # Check if ffmpeg.exe is in current folder or PATH
    if shutil.which("ffmpeg") or os.path.exists("ffmpeg.exe"):
        return True
    return False

def get_next_index(output_folder):
    if not os.path.exists(output_folder):
        return 1
    
    existing_files = [f for f in os.listdir(output_folder) if f.lower().endswith('.mp3')]
    max_idx = 0
    for f in existing_files:
        try:
            num = int(os.path.splitext(f)[0])
            if num > max_idx:
                max_idx = num
        except ValueError:
            pass
    return max_idx + 1

def process_file(filename, start_index, ffmpeg_exe, output_dir):
    print(f"\n--- Processing: {filename} ---")
    print("Analyzing silence to detect chunks...")
    
    cmd = [
        ffmpeg_exe,
        "-i", filename,
        "-af", f"silencedetect=noise={SILENCE_THRESH}:d={MIN_SILENCE_DUR}",
        "-f", "null",
        "-"
    ]
    
    result = subprocess.run(cmd, stderr=subprocess.PIPE, text=True)
    output = result.stderr

    starts = []
    ends = []
    
    for line in output.splitlines():
        if "silence_start:" in line:
            t = float(line.split("silence_start: ")[1].strip())
            starts.append(t)
        elif "silence_end:" in line:
             t = float(line.split("silence_end: ")[1].split("|")[0].strip())
             ends.append(t)
             
    segments = []
    current_start = 0.0
    
    for i in range(len(starts)):
        duration = starts[i] - current_start
        if duration > 0.5: 
            segments.append((current_start, duration))
        
        if i < len(ends):
            current_start = ends[i]
            
    print(f"Found {len(segments)} segments in {filename}.")
    
    current_idx = start_index
    for i, (start, dur) in enumerate(segments):
        out_name = os.path.join(output_dir, f"{current_idx}.mp3")
        
        # To fix "pop" noises: re-encode and apply short fade-in.
        chunk_cmd = [
            ffmpeg_exe,
            "-ss", str(start),
            "-t", str(dur),
            "-i", filename,
            "-af", "afade=t=in:ss=0:d=0.05", 
            "-y",
            out_name
        ]
        subprocess.run(chunk_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"Saved {out_name} (Start: {start:.2f}s, Dur: {dur:.2f}s)")
        current_idx += 1
        
    return current_idx

def main():
    parser = argparse.ArgumentParser(description="Split audio files into chunks based on silence.")
    parser.add_argument("-o", "--output", default="compliments_split", help="Output directory for split files")
    args = parser.parse_args()
    
    output_dir = args.output

    if not check_ffmpeg():
        print("Error: ffmpeg not found.")
        print("Please download ffmpeg and put 'ffmpeg.exe' in this folder.")
        return

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        
    ffmpeg_exe = "ffmpeg" if shutil.which("ffmpeg") else ".\\ffmpeg.exe"
    
    # Get all mp3 files in current directory
    mp3_files = [f for f in os.listdir('.') if f.lower().endswith('.mp3')]
    
    if not mp3_files:
        print("No .mp3 files found in this folder.")
        return

    current_index = get_next_index(output_dir)
    print(f"Output folder ready: {output_dir}. Starting numbering at {current_index}.mp3")

    for mp3 in mp3_files:
        current_index = process_file(mp3, current_index, ffmpeg_exe, output_dir)

    print(f"\nAll files processed! Output in '{output_dir}' folder.")

if __name__ == "__main__":
    main()
