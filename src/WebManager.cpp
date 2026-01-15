#include "WebManager.h"

WebManager webManager;

const char* htmlStyle = R"rawliteral(
<style>
body{font-family:sans-serif;margin:0;padding:20px;background:#121212;color:#e0e0e0}
h2{text-align:center;color:#ffc107}
.card{background:#1e1e1e;padding:15px;border:1px solid #333;margin-bottom:15px;border-radius:5px}
input,select{width:100%;padding:10px;margin:5px 0;background:#333;color:#fff;border:1px solid #444;box-sizing:border-box}
button{width:100%;padding:12px;background:#ffc107;color:#000;border:none;font-weight:bold;cursor:pointer}
</style>
)rawliteral";

void WebManager::begin() {
    String ssid = settings.getWifiSSID();
    String pass = settings.getWifiPass();
    
    WiFi.mode(WIFI_AP_STA);
    
    if (ssid != "") {
        WiFi.begin(ssid.c_str(), pass.c_str());
        Serial.print("Connecting to WiFi");
        int tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries < 20) {
            delay(500);
            Serial.print(".");
            tries++;
        }
        Serial.println();
    }

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.softAP("AtomicCharmer");
        _apMode = true;
        _dnsServer.start(_dnsPort, "*", WiFi.softAPIP());
        Serial.println("AP Mode: AtomicCharmer (192.168.4.1)");
    } else {
        Serial.print("Connected! IP: ");
        Serial.println(WiFi.localIP());
        _apMode = false;
    }

    _server.on("/", [this](){ handleRoot(); });
    _server.on("/save", HTTP_POST, [this](){ handleSave(); });
    _server.onNotFound([this](){ handleNotFound(); });
    _server.begin();
}

void WebManager::loop() {
    if (_apMode) {
        _dnsServer.processNextRequest();
    }
    _server.handleClient();
}

void WebManager::handleRoot() {
    _server.send(200, "text/html", getHtml());
}

void WebManager::handleSave() {
    if (_server.hasArg("ssid")) settings.setWifiSSID(_server.arg("ssid"));
    if (_server.hasArg("pass")) settings.setWifiPass(_server.arg("pass"));
    if (_server.hasArg("tz")) settings.setTimezoneOffset(_server.arg("tz").toInt());
    
    String html = "<html><body><h1>Saved! Rebooting...</h1></body></html>";
    _server.send(200, "text/html", html);
    delay(1000);
    ESP.restart();
}

void WebManager::handleNotFound() {
    if (_apMode) {
        _server.sendHeader("Location", "http://192.168.4.1/", true); // Captive Portal redirect
        _server.send(302, "text/plain", "");
    } else {
        _server.send(404, "text/plain", "Not Found");
    }
}

String WebManager::getHtml() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += htmlStyle;
    html += "</head><body>";
    html += "<h2>Atomic Charmer Setup</h2>";
    html += "<form action='/save' method='POST'>";
    
    html += "<div class='card'><h3>WiFi Settings</h3>";
    html += "<label>SSID</label><input type='text' name='ssid' value='" + settings.getWifiSSID() + "'>";
    html += "<label>Password</label><input type='password' name='pass' value='" + settings.getWifiPass() + "'>";
    html += "</div>";
    
    html += "<div class='card'><h3>Time Settings</h3>";
    html += "<label>Timezone Offset (Hours)</label><input type='number' name='tz' value='" + String(settings.getTimezoneOffset()) + "'>";
    html += "</div>";
    
    html += "<button type='submit'>Save & Reboot</button>";
    html += "</form></body></html>";
    return html;
}
