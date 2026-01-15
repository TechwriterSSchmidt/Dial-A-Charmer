
import os

source_file = os.path.join(os.path.dirname(__file__), 'compliments', 'compliments.txt')
output_dir = os.path.join(os.path.dirname(__file__), 'compliments', 'grouped')

# Ensure output directory exists
os.makedirs(output_dir, exist_ok=True)

# Define categories and keyword heuristics
# Note: Based on the file content Read, we can also use block splitting if the file is ordered.
# The user's file seems to be ordered:
# 1. Trump (Starts approx line 1)
# 2. Badran (Starts with "Hier kommt Jacqueline" around line 177)
# 3. Yoda (Starts "Stark in dir die Macht des Codes ist" around line 290)
# 4. Neutral/Nerd (The rest)

def group_compliments():
    with open(source_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    trump_lines = []
    badran_lines = []
    yoda_lines = []
    neutral_lines = []
    
    current_category = "trump" # Assume start is Trump based on inspection

    for line in lines:
        text = line.strip()
        if not text:
            continue
            
        # Markers for switching
        if "Hier kommt Jacqueline" in text:
            current_category = "badran"
            continue # Skip the marker line itself
            
        if "Stark in dir die Macht des Codes ist" in text:
            current_category = "yoda"
            # Keep this line, it's a compliment too
            yoda_lines.append(line)
            continue
            
        if "Dein Stack Overflow Reputation Score" in text:
            current_category = "neutral"
            neutral_lines.append(line)
            continue
            
        # Categorize
        if current_category == "trump":
            trump_lines.append(line)
        elif current_category == "badran":
            badran_lines.append(line)
        elif current_category == "yoda":
            yoda_lines.append(line)
        elif current_category == "neutral":
            neutral_lines.append(line)

    # Write to separte files
    with open(os.path.join(output_dir, "trump.txt"), 'w', encoding='utf-8') as f:
        f.writelines(trump_lines)
        
    with open(os.path.join(output_dir, "badran.txt"), 'w', encoding='utf-8') as f:
        f.writelines(badran_lines)
        
    with open(os.path.join(output_dir, "yoda.txt"), 'w', encoding='utf-8') as f:
        f.writelines(yoda_lines)
        
    with open(os.path.join(output_dir, "neutral.txt"), 'w', encoding='utf-8') as f:
        f.writelines(neutral_lines)

    print(f"Split complete: Trump({len(trump_lines)}), Badran({len(badran_lines)}), Yoda({len(yoda_lines)}), Neutral({len(neutral_lines)})")

if __name__ == "__main__":
    group_compliments()
