import urllib.request
import re
import random
import os
import html
import json
import socket

# Configuration
OUTPUT_FILE = "fortune_examples.txt"
TARGET_ADDITIONAL_COUNT = 750

# Sources
URL_WITZE_JSON = "https://raw.githubusercontent.com/tschlumpf/deutsche-Witze/main/witze.json"
URL_FLACHWITZE = "https://raw.githubusercontent.com/derphilipp/Flachwitze/main/README.md"
URL_DAD_JOKES = "https://official-joke-api.appspot.com/jokes/random/300"
URL_DWYL_QUOTES = "https://raw.githubusercontent.com/dwyl/quotes/main/quotes.json"

def clean_text(text):
    """
    Replaces punctuation with commas, removes newlines, cleans spaces, removes dashes.
    """
    if not text: return ""
    text = text.replace('\n', ' ').replace('\r', '')
    # Check for dashes acting as separators or lists
    text = text.replace('—', ' ').replace('–', ' ').replace('-', ' ')
    
    # Replace punctuation with comma
    text = re.sub(r'[.!?:;]', ',', text)
    
    # Cleanup multiple commas and spaces
    text = re.sub(r',+', ',', text)       # multiple commas -> single comma
    text = re.sub(r'\s+,', ',', text)     # space before comma -> comma
    text = re.sub(r',\s*', ', ', text)    # comma + optional space -> comma + space
    text = re.sub(r'\s+', ' ', text)      # multiple spaces -> single space
    
    return text.strip().strip(',') # Strip leading/trailing whitespaces and commas

def get_nietzsche_quotes():
    """Returns a list of manually curated Nietzsche quotes."""
    print("Adding Nietzsche Quotes...")
    # 30 Famous Quotes
    raw = [
        "Was mich nicht umbringt macht mich stärker",
        "Wer mit Ungeheuern kämpft mag zusehn dass er nicht dabei zum Ungeheuer wird",
        "Wenn du lange in einen Abgrund blickst blickt der Abgrund auch in dich hinein",
        "Gott ist tot",
        "Man muss noch Chaos in sich haben um einen tanzenden Stern gebären zu können",
        "Ohne Musik wäre das Leben ein Irrtum",
        "Der Weg zur Hölle ist mit guten Vorsätzen gepflastert",
        "Es gibt keine Wahrheit nur Interpretationen",
        "Die Liebe ist ein Zustand in dem der Mensch die Dinge meistens so sieht wie sie nicht sind",
        "Wer ein Wofür im Leben hat der erträgt fast jedes Wie",
        "Niemand kann dir die Brücke bauen auf der gerade du über den Fluss des Lebens schreiten musst",
        "Alles was tief ist liebt die Maske",
        "Das Weib ist das zweite Verbrechen Gottes",
        "Im Gebirge der Wahrheit klettert man nie umsonst",
        "Glaube bedeutet nicht wissen zu wollen was wahr ist",
        "Der Übermensch ist der Sinn der Erde",
        "Man verdirbt einen Jüngling am sichersten wenn man ihn anleitet den Gleichdenkenden höher zu achten als den Andersdenkenden",
        "Kunst ist das eigentliche Metaphysicum des Lebens",
        "Nicht durch Zorn sondern durch Lachen tötet man",
        "Wer das Hohe will muss auch das Tiefe wollen",
        "Die Schlange welche sich nicht häuten kann geht zugrunde",
        "Wo Liebe wächst gedeiht Leben",
        "Frei ist wer in seinen eigenen Fesseln tanzen kann",
        "Hoffnung ist in Wahrheit das übelste der Übel weil sie die Qual der Menschen verlängert",
        "Der Vorteil eines schlechten Gedächtnisses ist dass man dieselben guten Dinge mehrmals zum ersten Mal genießt",
        "Traue keinem Gedanken der nicht im Freien geboren ist und bei dem nicht die Muskeln auch ein Fest feiern",
        "Ich beschwöre euch meine Brüder bleibt der Erde treu",
        "Jede tiefe Seele braucht eine Maske",
        "Man soll von dem weggehen wo man am meisten geliebt wird",
        "Wer von seinem Tag nicht zwei Drittel für sich selbst hat ist ein Sklave"
    ]
    # Format: Quote, Friedrich Nietzsche
    return ["DE: " + clean_text(q) + ", Friedrich Nietzsche" for q in raw]

def fetch_url(url):
    try:
        with urllib.request.urlopen(url, timeout=3) as response:
            return response.read().decode('utf-8')
    except Exception as e:
        print(f"  Warning: Could not fetch {url} ({e})")
        return ""

def fetch_witze():
    print("Fetching Witze...")
    jokes = []
    
    # JSON
    data_str = fetch_url(URL_WITZE_JSON)
    if data_str:
        try:
            data = json.loads(data_str)
            if isinstance(data, list):
                for item in data:
                    txt = ""
                    if isinstance(item, dict):
                        txt = item.get('text') or item.get('joke') or item.get('inhalt')
                    elif isinstance(item, str):
                        txt = item
                    
                    if txt and len(txt) < 150: # Short jokes only
                        jokes.append("DE: " + clean_text(txt))
        except: pass

    # 2. Markdown (Flachwitze)
    md_str = fetch_url(URL_FLACHWITZE)
    if md_str:
        for line in md_str.splitlines():
            line = line.strip()
            if line.startswith("- "):
                txt = line[2:]
                if len(txt) < 100:
                    jokes.append("DE: " + clean_text(txt))
        
    print(f"  Found {len(jokes)} jokes.")
    return jokes

def fetch_dad_jokes():
    print("Fetching Dad Jokes...")
    jokes = []

    data_str = fetch_url(URL_DAD_JOKES)
    if data_str:
        try:
            data = json.loads(data_str)
            if isinstance(data, list):
                for item in data:
                    if not isinstance(item, dict):
                        continue
                    setup = item.get("setup", "")
                    punchline = item.get("punchline", "")
                    text = (setup + " " + punchline).strip()
                    if text and len(text) < 180:
                        jokes.append("EN: " + clean_text(text))
        except Exception as e:
            print(f"  Warning: Could not parse Dad Jokes ({e})")

    print(f"  Found {len(jokes)} dad jokes.")
    return jokes

def fetch_dwyl_quotes():
    print("Fetching DWYL Quotes...")
    quotes = []

    data_str = fetch_url(URL_DWYL_QUOTES)
    if data_str:
        try:
            data = json.loads(data_str)
            if isinstance(data, list):
                # Sample to avoid exploding the file size (Source has ~16k quotes)
                # Taking 400 random quotes
                if len(data) > 500:
                    data = random.sample(data, 500)

                for item in data:
                    if not isinstance(item, dict):
                        continue
                    text = item.get("text", "")
                    author = item.get("author", "Unknown")
                    if text and len(text) < 250:
                        quotes.append(f"EN: {clean_text(text)}, {author}")
        except Exception as e:
            print(f"  Warning: Could not parse DWYL Quotes ({e})")

    print(f"  Found {len(quotes)} DWYL quotes.")
    return quotes

def main():
    # Load Existing
    existing = []
    
    # 1. Fetch Sources
    jokes = fetch_witze()
    dad_jokes = fetch_dad_jokes()
    nietzsche = get_nietzsche_quotes() 
    dwyl_quotes = fetch_dwyl_quotes()

    # 2. Combine
    # Balanced source mix is required.
    
    combined = []
    combined.extend(jokes)
    combined.extend(dad_jokes)
    combined.extend(dwyl_quotes)
    combined.extend(nietzsche) 
    
    # Shuffle    
    random.shuffle(combined)
    
    # 3. Write
    count = 0
    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        for line in combined:
            if len(line) > 5: # Skip empty trash
                f.write(line + "\n")
                count += 1
            
    print(f"Total entries written: {count}")
    print(f"Saved to {OUTPUT_FILE}")

if __name__ == "__main__":
    main()
