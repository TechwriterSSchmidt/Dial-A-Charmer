import sys
import os
sys.path.append(os.path.join(os.getcwd(), 'utils', 'TrumpSpeak'))
from utils.text.symbols import phonemes as model_phonemes
from gruut import sentences

text = "Hello world. This is a test."
print(f"Original: {text}")

# Generate phonemes with gruut
for sent in sentences(text, lang="en-us"):
    for word in sent:
        if word.phonemes:
            print(f"Word: {word.text}, Phonemes: {word.phonemes}")

# Check coverage
flattened_phonemes = []
for sent in sentences(text, lang="en-us"):
    for word in sent:
        if word.phonemes:
            flattened_phonemes.extend(word.phonemes)

model_phonemes_set = set(model_phonemes)
print("\nChecking compatibility...")
for p in flattened_phonemes:
    if p not in model_phonemes_set:
        print(f"Warning: Phoneme '{p}' from gruut is not in model symbols!")
    else:
        # print(f"OK: {p}")
        pass
