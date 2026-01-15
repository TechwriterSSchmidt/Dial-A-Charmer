import os
import glob
import collections

def scan_chars():
    base_dir = os.path.join(os.path.dirname(__file__), 'compliments', 'grouped')
    allowed = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,!?:;()-_\"'\n\r"
    # Basic ASCII + common punctuation relative to code.
    # Note: Umlauts are NOT in this allowed list, so they should show up.
    
    chars_found = collections.Counter()
    
    for filepath in glob.glob(os.path.join(base_dir, "*.txt")):
        print(f"Scanning {os.path.basename(filepath)}...")
        with open(filepath, 'r', encoding='utf-8') as f:
            text = f.read()
            for c in text:
                if c not in allowed:
                    chars_found[c] += 1
                    
    print("\n--- Special Characters Found ---")
    for c, count in chars_found.most_common():
        print(f"Char: '{c}' (Hex: {ord(c):X}) - Count: {count}")

if __name__ == "__main__":
    scan_chars()
