import os
import re
import sys
from pydub import AudioSegment

# Configuration
MAPPING_FILE = "../tts_file_mapping.txt"
DEST_ROOT = "../sd_card_content"

def main():
    # Script expects execution from the utils directory.
    if not os.path.exists(MAPPING_FILE):
        print(f"Error: {MAPPING_FILE} not found. Please run this script from the 'utils' directory.")
        sys.exit(1)

    # 1. Load Mapping
    mapping = []
    print(f"Loading mapping from {MAPPING_FILE}...")
    with open(MAPPING_FILE, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line: continue
            # Format: "1: filename.wav -> text"
            # Use regex to be robust
            m = re.match(r"(\d+):\s+([^ ]+)\s+->", line)
            if m:
                index = int(m.group(1))
                filename = m.group(2)
                mapping.append((index, filename))
    
    # Sort mapping by index just in case
    mapping.sort(key=lambda x: x[0])
    
    print(f"Loaded {len(mapping)} targets.")

    # 2. Find Chunks
    # Look for files matching luvvoice*.wav pattern in current dir
    print("Scanning for audio chunks...")
    files = [f for f in os.listdir('.') if f.startswith('luvvoice') and f.endswith('.wav')]
    # Sort by the number at the end
    files.sort(key=lambda x: int(re.search(r"_(\d+)\.wav$", x).group(1)))
    
    print(f"Found {len(files)} chunk files.")
    
    if len(files) == 0:
        print("No audio chunks found. Aborting.")
        sys.exit(1)

    # 3. Process
    chunk_idx = 0
    
    for (line_idx, target_filename) in mapping:
        # Determine how many chunks for this line
        # Line 6 ("Bitte warten. Inhalte werden neu indexiert.") -> Likely split
        # Line 13 ("Batterie kritisch. System wird heruntergefahren.") -> Likely split
        
        num_chunks = 1
        if line_idx == 6:
            num_chunks = 2
            print(f"Processing Line {line_idx} ({target_filename}): Merging 2 chunks (detected split)")
        elif line_idx == 13:
            num_chunks = 2
            print(f"Processing Line {line_idx} ({target_filename}): Merging 2 chunks (detected split)")
        
        if chunk_idx + num_chunks > len(files):
            print(f"Error: Not enough chunks for Line {line_idx}. Needed index {chunk_idx + num_chunks}, max {len(files)}")
            break
            
        # Get source audio
        sources = files[chunk_idx : chunk_idx + num_chunks]
        
        # Determine Destination Path
        dest_rel = ""
        if target_filename.startswith("system/"):
             # "system/foo.wav" -> "sd_card_content/system/foo.wav"
             dest_rel = target_filename
        else:
             # "h_0.wav" -> "sd_card_content/time/de/h_0.wav"
             dest_rel = os.path.join("time/de", target_filename)
             
        dest_path = os.path.join(DEST_ROOT, dest_rel)
        dest_dir = os.path.dirname(dest_path)
        
        if not os.path.exists(dest_dir):
            os.makedirs(dest_dir)
            
        # Merge and Export
        try:
            combined = AudioSegment.empty()
            for src in sources:
                combined += AudioSegment.from_wav(src)
            
            combined.export(dest_path, format="wav")
            # print(f"  Saved to {dest_path}")
        except Exception as e:
            print(f"Error processing {target_filename}: {e}")
            
        chunk_idx += num_chunks
        
    print("Done. Please verify the output in sd_card_content/")

if __name__ == "__main__":
    main()
