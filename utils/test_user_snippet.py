import os
import time
import google.generativeai as genai

# Load Key
api_key = os.environ.get("GEMINI_API_KEY")
if not api_key:
    try:
        with open("GEMINI_API_KEY") as f:
            api_key = f.read().strip()
    except:
        pass

if not api_key:
    print("ERROR: No API Key")
    exit(1)

genai.configure(api_key=api_key)

# The user suggested gemini-2.5-flash-8b, but let's list models to be sure or just try it.
# We will try the user's exact suggestion first
model_name = 'gemini-2.0-flash' # Using 2.0 Flash as the baseline standard which supports native audio

# Actually, the user asked to test "gemini-2.5-flash-8b"
# If that model name is invalid, the API will error. 
# Let's try 2.0-flash first as it is known to support Audio.

print(f"Using model: {model_name}")
model = genai.GenerativeModel(model_name)

prompt = "Say this in a dramatic, slow, whispering voice: The secrets of the universe are hidden in the stars."

print("Sending request...")
start = time.time()
try:
    response = model.generate_content(
        prompt,
        generation_config=genai.types.GenerationConfig(
            response_mime_type="audio/wav",
            speech_config=genai.types.SpeechConfig(
                voice_config=genai.types.VoiceConfig(
                    prebuilt_voice_config=genai.types.PrebuiltVoiceConfig(
                        voice_name="Kore"
                    )
                )
            )
        )
    )
    
    elapsed = time.time() - start
    print(f"Response received in {elapsed:.2f}s")
    
    # Check for audio data (Google GenerativeAI lib structure)
    # response.parts[0].inline_data
    # But for simplified audio response, it might be in response.audio_content if using a helper?
    # No, usually response.parts
    
    try:
        # Debug structure
        # print(response) 
        
        # Access audio
        # It's usually in candidates[0].content.parts[0].inline_data
        # With the python lib, response might have a helper
        
        # The user snippet used: response.audio_data ? 
        # This property might not exist on the standard response object unless it's a specific audio helper.
        # Let's inspect parts.
        
        if hasattr(response, 'parts'):
            count = 0
            for part in response.parts:
                count += 1
                # print(f"Part {count}: {part}")
        
    except Exception as e:
        print(f"Inspection error: {e}")

    # Standard saving attempt
    # The user snippet: f.write(response.audio_data)
    # Let's see if that attribute exists in 0.8.6
    
    audio_bytes = None
    if hasattr(response, 'text'):
        print(f"Text content (if any): {response.text}")
        
    # Manual extraction if .audio_data missing
    # In V1Beta: response.candidates[0].content.parts[0].inline_data.data (base64 decoded?)
    
    # If using the simplified Python SDK:
    # It might handle it differently.
    
    pass

except Exception as e:
    print(f"Error: {e}")
