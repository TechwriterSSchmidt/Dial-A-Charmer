import os
import time
from google import genai

# Load API Key
api_key = os.environ.get("GEMINI_API_KEY")
if not api_key:
    try:
        with open("GEMINI_API_KEY") as f:
            api_key = f.read().strip()
    except:
        pass

if not api_key:
    print("ERROR: No API Key found.")
    exit(1)

print(f"API Key loaded ({len(api_key)} chars).")

client = genai.Client(api_key=api_key)

prompt = "Say this in a dramatic voice: The test is successful."

print("\n--- TEST 1: Standard TTS (Voice Config) ---")
start = time.time()
try:
    response = client.models.generate_content(
        model='gemini-2.0-flash',
        contents=prompt,
        config={
            'speech_config': {
                'voice_config': {
                    'prebuilt_voice_config': {
                        'voice_name': "Kore"
                    }
                }
            }
        }
    )
    print(f"Time: {time.time()-start:.2f}s")
    if response.candidates and response.candidates[0].content.parts[0].inline_data:
        print("Audio received (TTS mode).")
    else:
        print("No audio in TTS mode.")
except Exception as e:
    print(f"FAILED: {e}")


print("\n--- TEST 2: Native Audio Generation (Mime Type) ---")
start = time.time()
try:
    response = client.models.generate_content(
        model='gemini-2.0-flash',
        contents=prompt,
        config={
            'response_mime_type': 'audio/wav'
        }
    )
    print(f"Time: {time.time()-start:.2f}s")
    if response.candidates and response.candidates[0].content.parts[0].inline_data:
        print("Audio received (Native mode).")
        with open("test_native.wav", "wb") as f:
            f.write(response.candidates[0].content.parts[0].inline_data.data)
    else:
        print("No audio in Native mode.")
except Exception as e:
    print(f"FAILED: {e}")
