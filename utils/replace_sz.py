import os

def replace_sz_in_file(filepath):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        
        new_content = content.replace("ß", "ss")
        
        if content != new_content:
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(new_content)
            print(f"Updated {os.path.basename(filepath)}")
        else:
            print(f"No changes in {os.path.basename(filepath)}")
            
    except Exception as e:
        print(f"Error processing {filepath}: {e}")

if __name__ == "__main__":
    base_dir = os.path.join(os.path.dirname(__file__), 'compliments', 'grouped')
    files = ["trump.txt", "badran.txt", "yoda.txt", "neutral.txt"]
    
    for file_name in files:
        full_path = os.path.join(base_dir, file_name)
        if os.path.exists(full_path):
            replace_sz_in_file(full_path)
        else:
            print(f"File not found: {full_path}")
