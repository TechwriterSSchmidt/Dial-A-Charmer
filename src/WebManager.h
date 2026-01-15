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

private:
    WebServer _server;
    DNSServer _dnsServer;
    const byte _dnsPort = CONF_DNS_PORT;
    bool _apMode = false;

    void handleRoot();
    void handleSave();
    void handleHelp();
    void handleNotFound();
    
    String getHtml();
    String getHelpHtml();
};

extern WebManager webManager;

#endif
