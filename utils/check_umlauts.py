import os
import glob

def check_encoding_issues(filepath):
    print(f"--- Checking {os.path.basename(filepath)} ---")
    try:
        # Read as binary to see raw bytes
        with open(filepath, 'rb') as f:
            raw = f.read()
            
        try:
            text = raw.decode('utf-8')
        except UnicodeDecodeError:
            print("  ! UTF-8 Decode Error: File is not valid UTF-8.")
            return

        # Check for common Mojibake patterns (Double-encoded UTF-8)
        # Ã¼ (C3 BC) -> C3 83 C2 BC if double encoded
        # or Ã¼ appearing as text characters
        
        found_issues = []
        lines = text.split('\n')
        for i, line in enumerate(lines):
            # Check for Replacement Character
            if '\ufffd' in line:
                found_issues.append((i+1, "REPLACEMENT CHAR", line))
            
            # Check for Mojibake starts
            if 'Ã' in line:
                found_issues.append((i+1, "POSSIBLE MOJIBAKE (Ã)", line))
            
            # Check for digraphs that might be missed umlauts (heuristic)
            lower = line.lower()
            if 'ae' in lower or 'oe' in lower or 'ue' in lower:
                # Filter out likely false positives
                # "blue", "queue", "true", "value", "statue", "brave", "query"
                # "daeron", "fëanor", "phoenix"
                
                # Check specific words?
                words = lower.split()
                for w in words:
                    clean_w = w.strip(".,!?\"'")
                    if "ue" in clean_w:
                        if clean_w not in ["blue", "queue", "true", "statue", "bellevue", "value", "query", "issue", "influence", "continue", "clue", "due", "revenue", "quest", "guest", "league", "rogue", "vague", "plague", "dialogue", "unique", "technique", "antique", "boutique", "mosque", "checkup"]:
                             # German candidates? "fuer", "ueber", "muehe"
                             if clean_w in ["fuer", "ueber", "muessen", "kueche", "gruen", "fuenf", "tuer", "natuerlich", "zurueck", "glueck", "stueck"]:
                                 found_issues.append((i+1, f"DIGRAPH {clean_w}", line))
                    
                    if "ae" in clean_w:
                        if clean_w not in ["michael", "israel", "raphael", "aerospace", "daemon", "caesar"]:
                            if clean_w in ["waere", "haette", "spaet", "naechste", "maedchen", "kaese", "jaeger", "aehnlich"]:
                                found_issues.append((i+1, f"DIGRAPH {clean_w}", line))

                    if "oe" in clean_w:
                        if clean_w not in ["phoenix", "joe", "does", "goes", "toes", "shoe", "canoe"]:
                             if clean_w in ["koennen", "moegen", "boese", "schjoen", "oeffnen", "hoeren", "loesung"]:
                                found_issues.append((i+1, f"DIGRAPH {clean_w}", line))

        if not found_issues:
            print("  No obvious issues found.")
        else:
            for ln, type, txt in found_issues:
                print(f"  Line {ln} [{type}]: {txt.strip()}")
                
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    base_dir = os.path.join(os.path.dirname(__file__), 'compliments', 'grouped')
    for f in glob.glob(os.path.join(base_dir, "*.txt")):
        check_encoding_issues(f)
