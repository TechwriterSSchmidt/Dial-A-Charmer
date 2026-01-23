import json
import random
import re
import os

JSON_FILE = "quotes.json"
TXT_FILE = "../fortune_examples.txt"
ADD_COUNT = 300 # Add 300 from the new file

def process_text(text):
    # Replace punctuation with comma
    text = re.sub(r'[.!?:;]', ',', text)
    # Remove newlines
    text = text.replace('\n', ' ')
    # Compress spaces
    return " ".join(text.split())

def main():
    # 1. Load existing quotes
    existing_quotes = []
    if os.path.exists(TXT_FILE):
        with open(TXT_FILE, "r", encoding="utf-8") as f:
            existing_quotes = [line.strip() for line in f if line.strip()]
    
    print(f"Loaded {len(existing_quotes)} existing quotes.")

    # 2. Load new JSON quotes
    new_quotes = []
    try:
        # Try Loading with fallback encoding
        try:
             with open(JSON_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
        except UnicodeDecodeError:
             print("UTF-8 failed, trying latin-1...")
             with open(JSON_FILE, "r", encoding="latin-1") as f:
                data = json.load(f)
            
        print(f"Loaded {len(data)} quotes from JSON.")
        
        # Shuffle data to pick random ones
        random.shuffle(data)
        
        for item in data[:ADD_COUNT]:
            q_text = item.get("quoteText", "").strip()
            q_author = item.get("quoteAuthor", "").strip()
            
            if not q_text: continue
            
            full_str = f"{q_text} {q_author}" if q_author else q_text
            processed = process_text(full_str)
            new_quotes.append(processed)
            
    except Exception as e:
        print(f"Error reading JSON: {e}")
        return

    # 3. Combine
    combined = existing_quotes + new_quotes
    random.shuffle(combined)
    
    # 4. Save
    with open(TXT_FILE, "w", encoding="utf-8") as f:
        for q in combined:
            f.write(q + "\n")
            
    print(f"Successfully merged. Total quotes: {len(combined)}")

if __name__ == "__main__":
    main()
