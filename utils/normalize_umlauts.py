import os

def normalize_text(filepath):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
            
        original_content = content
        
        # Transliterate Umlauts
        content = content.replace("ä", "ae")
        content = content.replace("ö", "oe")
        content = content.replace("ü", "ue")
        content = content.replace("Ä", "Ae")
        content = content.replace("Ö", "Oe")
        content = content.replace("Ü", "Ue")
        
        # Fix other special chars
        content = content.replace("–", "-") # En-dash to hyphen
        content = content.replace("é", "e") # e.g. Pokemon
        
        if content != original_content:
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(content)
            print(f"Normalized {os.path.basename(filepath)}")
        else:
            print(f"No changes needed for {os.path.basename(filepath)}")

    except Exception as e:
        print(f"Error processing {filepath}: {e}")

if __name__ == "__main__":
    base_dir = r"c:\Users\scse\Documents\PlatformIO\The-Atomic-Charmer\utils\compliments\grouped"
    files = ["trump.txt", "badran.txt", "yoda.txt", "neutral.txt"]
    
    for file_name in files:
        full_path = os.path.join(base_dir, file_name)
        if os.path.exists(full_path):
            normalize_text(full_path)
