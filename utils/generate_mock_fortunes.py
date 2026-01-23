import random

OUTPUT_FILE = "fortune_examples.txt"
TARGET_COUNT = 500

# 1. Hardcoded Nietzsche Quotes (German & English Mix for variety, or just German if preferred? 
# User project seems mixed language, but 'fortune.txt' implies English usually, but user asked for "Nietzsche Aussprüche".
# I will use mostly German as the project context seems German ("meine Mailadresse", "das Skript...").
NIETZSCHE_QUOTES = [
    "Was mich nicht umbringt, macht mich stärker",
    "Wer mit Ungeheuern kämpft, mag zusehn, dass er nicht dabei zum Ungeheuer wird",
    "Wenn du lange in einen Abgrund blickst, blickt der Abgrund auch in dich hinein",
    "Gott ist tot",
    "Man muss noch Chaos in sich haben, um einen tanzenden Stern gebären zu können",
    "Ohne Musik wäre das Leben ein Irrtum",
    "Der Weg zur Hölle ist mit guten Vorsätzen gepflastert",
    "Es gibt keine Wahrheit, nur Interpretationen",
    "Die Liebe ist ein Zustand, in dem der Mensch die Dinge meistens so sieht, wie sie nicht sind",
    "Wer ein Wofür im Leben hat, der erträgt fast jedes Wie",
    "Niemand kann dir die Brücke bauen, auf der gerade du über den Fluss des Lebens schreiten musst",
    "Alles, was tief ist, liebt die Maske",
    "Das Weib ist das zweite Verbrechen Gottes",
    "Im Gebirge der Wahrheit klettert man nie umsonst",
    "Glaube bedeutet, nicht wissen zu wollen, was wahr ist",
    "Der Übermensch ist der Sinn der Erde",
    "Man verdirbt einen Jüngling am sichersten, wenn man ihn anleitet, den Gleichdenkenden höher zu achten als den Andersdenkenden",
    "Kunst ist das eigentliche Metaphysicum des Lebens",
    "Nicht durch Zorn, sondern durch Lachen tötet man",
    "Wer das Hohe will, muss auch das Tiefe wollen",
    "Die Schlange, welche sich nicht häuten kann, geht zugrunde",
    "Wo Liebe wächst, gedeiht Leben",
    "Frei ist, wer in seinen eigenen Fesseln tanzen kann",
    "Hoffnung ist in Wahrheit das übelste der Übel, weil sie die Qual der Menschen verlängert",
    "Der Vorteil eines schlechten Gedächtnisses ist, dass man dieselben guten Dinge mehrmals zum ersten Mal genießt",
    "Traue keinem Gedanken, der nicht im Freien geboren ist und bei dem nicht die Muskeln auch ein Fest feiern",
    "Ich beschwöre euch, meine Brüder, bleibt der Erde treu",
    "Jede tiefe Seele braucht eine Maske",
    "Man soll von dem weggehen, wo man am meisten geliebt wird",
    "Wer von seinem Tag nicht zwei Drittel für sich selbst hat, ist ein Sklave",
]

# 2. Fortune Templates for generation
VERBS = ["findest", "entdeckst", "siehst", "bekommst", "verlierst", "suchst", "erhältst", "gewinnst"]
ADJECTIVES = ["glückliche", "seltsame", "überraschende", "wichtige", "dunkle", "helle", "mysteriöse", "strahlende"]
NOUNS = ["Liebe", "Zukunft", "Wahrheit", "Gelegenheit", "Begegnung", "Reise", "Nachricht", "Hoffnung", "Freude", "Weisheit"]
TIME = ["bald", "morgen", "in Kürze", "wenn du es am wenigsten erwartest", "zu später Stunde", "am nächsten Morgen"]

def process_text(text):
    # Replace punctuation with comma
    text = text.replace('.', ',').replace('!', ',').replace('?', ',').replace(':', ',').replace(';', ',')
    # Remove newlines
    text = text.replace('\n', ' ')
    # Compress spaces
    return " ".join(text.split())

def generate_fortune():
    template = random.choice([
        f"Du {random.choice(VERBS)} eine {random.choice(ADJECTIVES)} {random.choice(NOUNS)} {random.choice(TIME)}",
        f"Achte auf eine {random.choice(ADJECTIVES)} {random.choice(NOUNS)}, sie wird dein Leben verändern",
        f"Das Glück liegt in einer {random.choice(ADJECTIVES)} {random.choice(NOUNS)} verborgen",
        f"Sei bereit für eine {random.choice(ADJECTIVES)} {random.choice(NOUNS)} {random.choice(TIME)}",
        f"Eine {random.choice(ADJECTIVES)} {random.choice(NOUNS)} wartet auf dich",
    ])
    return template

def main():
    quotes = []
    
    # Add Nietzsche quotes (repeated slightly to fill if needed, but we mix them)
    # We'll generate 500 lines total.
    
    for _ in range(TARGET_COUNT):
        # 30% chance for Nietzsche, 70% random fortune
        if random.random() < 0.3:
            raw_text = random.choice(NIETZSCHE_QUOTES) + " Friedrich Nietzsche"
        else:
            raw_text = generate_fortune()
            
        quotes.append(process_text(raw_text))
        
    # Write to file
    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        for q in quotes:
            f.write(q + "\n")
            
    print(f"Generated {TARGET_COUNT} quotes in {OUTPUT_FILE}")

if __name__ == "__main__":
    main()
