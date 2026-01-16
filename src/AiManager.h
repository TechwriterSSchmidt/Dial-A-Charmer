#ifndef AI_MANAGER_H
#define AI_MANAGER_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Settings.h"

// Google Translate TTS URL
// NOTE: This is an unofficial endpoint.
#define GTTS_URL "http://translate.google.com/translate_tts?ie=UTF-8&total=1&idx=0&textlen=32&client=tw-ob&q="

class AiManager {
public:
    String getCompliment(int number);
    
    String getTTSUrl(String text);
    
    bool hasApiKey();
    String callGemini(String prompt);

private:

};

extern AiManager ai;

#endif
