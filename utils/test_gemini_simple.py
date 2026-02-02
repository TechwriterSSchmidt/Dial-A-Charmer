import os
import time
from google import genai
from pydub import AudioSegment

# API Key check
api_key = os.environ.get("GEMINI_API_KEY")
if not api_key:
    # Try reading from file if env var not set directly
    try:
        with open("GEMINI_API_KEY", "r") as f:
            api_key = f.read().strip()
            print("loaded key from file")
    except:
        print("ERROR: No API Key found in env or file")
        exit(1)

print(f"API Key found (length: {len(api_key)})")

try:
    client = genai.Client(api_key=api_key)
    print("Client initialized. Sending request...")
    
    start = time.time()
    response = client.models.generate_content(
        model='gemini-2.0-flash',
        contents="Dies ist ein kurzer Test für die Sprachausgabe.",
        config={
            'speech_config': {
                'voice_config': {
                    'prebuilt_voice_config': {
                        'voice_name': "Kore"
                    }
                }
            },
        }
    )
    duration = time.time() - start
    print(f"Request took {duration:.2f} seconds")

    if response.candidates and response.candidates[0].content.parts:
        part = response.candidates[0].content.parts[0]
        if part.inline_data:
            print(f"Audio data received! Mime: {part.inline_data.mime_type}, Size: {len(part.inline_data.data)} bytes")
            
            # Save raw
            with open("test_output.bin", "wb") as f:
                f.write(part.inline_data.data)
            
            # Try pydub (just to see if it processes)
            try:
                # Often it's MP3 or WAV packaged
                if "wav" in part.inline_data.mime_type:
                    fmt = "wav"
                else: 
                    fmt = "mp3" # Fallback assumption
                
                # We won't convert here, just confirming receipt is enough for debug
                print("Test successful.")
            except Exception as e:
                print(f"Pydub processing error: {e}")
        else:
            print("No inline data in response part.")
            print(response)
    else:
        print("No candidates provided.")
        print(response)

except Exception as e:
    print(f"CRITICAL ERROR: {e}")
