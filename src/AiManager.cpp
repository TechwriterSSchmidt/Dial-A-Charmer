#include "AiManager.h"

AiManager ai;

bool AiManager::hasApiKey() {
    return settings.getGeminiKey().length() > 0;
}

String AiManager::getCompliment(int number) {
    if (!hasApiKey()) return "";

    String prompt = "";
    String lang = settings.getLanguage();
    String langPrompt = (lang == "de") ? " Deutsch." : " English.";

    switch(number) {
        case 1: prompt = "Generiere ein kurzes, nerdiges Kompliment für eine technische Redakteurin namens Sandra. Max 2 Sätze." + langPrompt; break;
        case 2: prompt = "Erzähle einen kurzen Witz über XML oder DITA." + langPrompt; break;
        case 3: prompt = "Gib mir eine kurze, inspirierende Weisheit aus der Science Fiction Welt." + langPrompt; break;
        case 4: prompt = "Beschreibe Sandra als Kapitänin eines Raumschiffs in 2 Sätzen." + langPrompt; break;
        case 5: prompt = "Generiere eine kurze Entschuldigung dafür, dass ich (der Computer) noch keinen Kaffee hatte." + langPrompt; break;
        default: prompt = "Sag etwas nettes zu Sandra. Kurz." + langPrompt; break;
    }

    return callGemini(prompt);
}

String AiManager::callGemini(String prompt) {
    String key = settings.getGeminiKey();
    if (key.length() == 0) return "";

    WiFiClientSecure client;
    client.setInsecure(); // Skip certificate validation
    
    HTTPClient http;
    String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-pro:generateContent?key=" + key;
    
    // JSON Payload with ArduinoJson v7
    JsonDocument doc;
    JsonObject content = doc["contents"].add<JsonObject>();
    JsonObject part = content["parts"].add<JsonObject>();
    part["text"] = prompt;
    
    String payload;
    serializeJson(doc, payload);
    
    Serial.println("Asking Gemini...");
    
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    int httpCode = http.POST(payload);
    String result = "";
    
    if (httpCode == 200) {
        String response = http.getString();
        // Parse Response
        JsonDocument respDoc;
        DeserializationError error = deserializeJson(respDoc, response);
        
        if (!error && respDoc.containsKey("candidates")) {
             const char* text = respDoc["candidates"][0]["content"]["parts"][0]["text"];
             if (text) result = String(text);
        }
    } else {
        Serial.printf("Gemini Error: %d\n", httpCode);
        Serial.println(http.getString());
    }
    
    http.end();
    
    // Cleanup text
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
    
    String lang = settings.getLanguage(); // "de" or "en"
    
    // Construct Google TTS URL
    return String(GTTS_URL) + encoded + "&tl=" + lang;
}
