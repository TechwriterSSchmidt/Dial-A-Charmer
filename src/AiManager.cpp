#include "AiManager.h"

AiManager ai;

String AiManager::getCompliment(int number) {
    String key = settings.getGeminiKey();
    if (key.length() == 0) return ""; // No key

    String prompt = "";
    switch(number) {
        case 1: prompt = "Generiere ein kurzes, nerdiges Kompliment für eine technische Redakteurin namens Sandra. Max 2 Sätze. Deutsch."; break;
        case 2: prompt = "Erzähle einen kurzen Witz über XML oder DITA. Deutsch."; break;
        case 3: prompt = "Gib mir eine kurze, inspirierende Weisheit aus der Science Fiction Welt. Deutsch."; break;
        case 4: prompt = "Beschreibe Sandra als Kapitänin eines Raumschiffs in 2 Sätzen. Deutsch."; break;
        case 5: prompt = "Generiere eine kurze Entschuldigung dafür, dass ich (der Computer) noch keinen Kaffee hatte. Deutsch."; break;
        default: prompt = "Sag etwas nettes zu Sandra. Kurz. Deutsch."; break;
    }

    return callGemini(prompt);
}

String AiManager::callGemini(String prompt) {
    String key = settings.getGeminiKey();
    if (key.length() == 0) return "";

    WiFiClientSecure client;
    client.setInsecure(); // Skip certificate validation for simplicity/memory
    
    HTTPClient http;
    String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-pro:generateContent?key=" + key;
    
    // JSON Payload
    // { "contents": [{ "parts": [{"text": "PROMPT"}] }] }
    JSONVar body;
    JSONVar parts;
    parts[0]["text"] = prompt;
    body["contents"][0]["parts"] = parts;
    
    String payload = JSON.stringify(body);
    
    Serial.println("Asking Gemini...");
    
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    int httpCode = http.POST(payload);
    String result = "";
    
    if (httpCode == 200) {
        String response = http.getString();
        JSONVar myo = JSON.parse(response);
        if (JSON.typeof(myo) != "undefined") {
            if (myo.hasOwnProperty("candidates")) {
                JSONVar candidates = myo["candidates"];
                JSONVar content = candidates[0]["content"];
                JSONVar parts = content["parts"];
                result = (const char*)parts[0]["text"];
            }
        }
    } else {
        Serial.printf("Gemini Error: %d\n", httpCode);
        Serial.println(http.getString());
    }
    
    http.end();
    
    // Cleanup text (remove asterisks, markdown, newlines for TTS)
    result.replace("*", "");
    result.replace("\n", " ");
    result.trim();
    
    Serial.print("AI says: ");
    Serial.println(result);
    
    return result;
}

String AiManager::getTTSUrl(String text) {
    if (text.length() == 0) return "";
    
    // URL Encode
    String encoded = "";
    char c;
    char code0;
    char code1;
    for (int i = 0; i < text.length(); i++) {
        c = text.charAt(i);
        if (c == ' ') {
            encoded += '+';
        } else if (isalnum(c)) {
            encoded += c;
        } else {
            code1 = (c & 0xf) + '0';
            if ((c & 0xf) > 9) {
                code1 = (c & 0xf) - 10 + 'A';
            }
            c = (c >> 4) & 0xf;
            code0 = c + '0';
            if (c > 9) {
                code0 = c - 10 + 'A';
            }
            encoded += '%';
            encoded += code0;
            encoded += code1;
        }
    }
    
    // Construct Google TTS URL
    // &tl=de for German
    return String(GTTS_URL) + encoded + "&tl=de";
}
