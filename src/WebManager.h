#ifndef WEB_MANAGER_H
#define WEB_MANAGER_H

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "Settings.h"
#include "config.h"
#include "Constants.h"

class WebManager {
public:
    void begin();
    void loop();
    void startAp();
    void stopAp();

private:
    AsyncWebServer _server{80};
    DNSServer _dnsServer;
    
    // --- Async Reindex Flags ---
    bool _reindexTriggered = false;
    void processReindex(); 
    
    const uint8_t _dnsPort = CONF_DNS_PORT;
    bool _apMode = false;
    bool _mdnsStarted = false;
    unsigned long _mdnsLastAttempt = 0;
    const unsigned long _mdnsRetryMs = 10000;
    unsigned long _apEndTime = 0; // Auto-off timer

    void handleRoot(AsyncWebServerRequest* request);
    void handleSettings(AsyncWebServerRequest* request); // New: Dedicated Settings/Alarm Page
    void handleAdvanced(AsyncWebServerRequest* request); // New
    void handleSave(AsyncWebServerRequest* request);
    void handleHelp(AsyncWebServerRequest* request);
    void handlePhonebook(AsyncWebServerRequest* request);
    void handlePhonebookGet(AsyncWebServerRequest* request);
    void handlePhonebookPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
    void handleFileListApi(AsyncWebServerRequest* request); // New: JSON File Browser
    void handlePreviewApi(AsyncWebServerRequest* request);  
    void handleNotFound(AsyncWebServerRequest* request);
    
    // Helpers
    void resetApTimer();
    
    // String getHtml(); // Removed (SPA Request)
    String getApSetupHtml(); // New: Dedicated AP Setup Page
    String getSettingsHtml(); // New
    String getAdvancedHtml(); // New
    String getPhonebookHtml();
    String getHelpHtml();
};

extern WebManager webManager;

#endif
