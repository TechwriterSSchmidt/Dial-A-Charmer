#pragma once
#include <esp_err.h>
#include <esp_http_server.h>

class WebManager {
public:
    void begin();
    void loop(); // Process mDNS queries etc.
    void startAPMode();
    void startLogCapture();
    void setResetInfo(uint32_t boot_count, const char *reason, int reason_code);

private:
    httpd_handle_t server = NULL;
    bool _apMode = false;
    
    // Internal Setup
    void setupWifi();
    void setupMdns();
    void setupWebServer();
    void setupDnsServer(); // For Captive Portal

    // Handlers
    static esp_err_t root_handler(httpd_req_t *req);
    static esp_err_t style_handler(httpd_req_t *req);
    static esp_err_t font_handler(httpd_req_t *req);
    static esp_err_t api_handler(httpd_req_t *req); // Generic API dispatcher?
};

extern WebManager webManager;
