#ifndef WEB_MANAGER_H
#define WEB_MANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "Settings.h"
#include "config.h"

class WebManager {
public:
    void begin();
    void loop();
    void startAp();
    void stopAp();

private:
    WebServer _server;
    DNSServer _dnsServer;
    const uint8_t _dnsPort = CONF_DNS_PORT;
    bool _apMode = false;
    unsigned long _apEndTime = 0; // Auto-off timer

    void handleRoot();
    void handleAdvanced(); // New
    void handleSave();
    void handleHelp();
    void handlePhonebook();
    void handlePhonebookApi();
    void handlePreviewApi();  
    void handleNotFound();
    
    // Helpers
    void resetApTimer();
    
    String getHtml();
    String getAdvancedHtml(); // New
    String getPhonebookHtml();
    String getHelpHtml();
};

extern WebManager webManager;

#endif
