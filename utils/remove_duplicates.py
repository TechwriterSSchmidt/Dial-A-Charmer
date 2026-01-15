import os

def remove_duplicates(filepath):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        
        original_count = len(lines)
        seen = set()
        unique_lines = []
        
        for line in lines:
            # Normalize for comparison (strip whitespace) but keep original formatting if needed
            # We assume one compliment per line.
            cleaned_line = line.strip()
            if not cleaned_line:
                continue # Skip empty lines
                
            if cleaned_line not in seen:
                seen.add(cleaned_line)
                unique_lines.append(line)
        
        new_count = len(unique_lines)
        removed_count = original_count - new_count
        
        with open(filepath, 'w', encoding='utf-8') as f:
            f.writelines(unique_lines)
            
        print(f"Processed {os.path.basename(filepath)}: Original {original_count}, New {new_count}, Removed {removed_count} duplicates.")
        
    except Exception as e:
        print(f"Error processing {filepath}: {e}")

if __name__ == "__main__":
    base_dir = r"c:\Users\scse\Documents\PlatformIO\The-Atomic-Charmer\utils\compliments\grouped"
    files = ["trump.txt", "badran.txt", "yoda.txt", "neutral.txt"]
    
    for file_name in files:
        full_path = os.path.join(base_dir, file_name)
        if os.path.exists(full_path):
            remove_duplicates(full_path)
        else:
            print(f"File not found: {full_path}")
