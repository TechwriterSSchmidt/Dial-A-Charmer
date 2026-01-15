#ifndef WEB_MANAGER_H
#define WEB_MANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "Settings.h"

class WebManager {
public:
    void begin();
    void loop();

private:
    WebServer _server;
    DNSServer _dnsServer;
    const byte _dnsPort = 53;
    bool _apMode = false;

    void handleRoot();
    void handleSave();
    void handleNotFound();
    
    String getHtml();
};

extern WebManager webManager;

#endif
