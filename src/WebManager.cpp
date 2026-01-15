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
        WiFi.softAP(CONF_AP_SSID);
        _apMode = true;
        _dnsServer.start(_dnsPort, "*", WiFi.softAPIP());
        Serial.print("AP Mode: ");
        Serial.print(CONF_AP_SSID);
        Serial.println(" (192.168.4.1)");
    } else {
        Serial.print("Connected! IP: ");
        Serial.println(WiFi.localIP());
        _apMode = false;
    }

    _server.on("/", [this](){ handleRoot(); });
    _server.on("/help", [this](){ handleHelp(); });
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
    if (_server.hasArg("gemini")) settings.setGeminiKey(_server.arg("gemini"));
    if (_server.hasArg("vol")) settings.setVolume(_server.arg("vol").toInt());
    
    String html = "<html><body><h1>Saved! Rebooting...</h1></body></html>";
    _server.send(200, "text/html", html);
    delay(1000);
    ESP.restart();
}

void WebManager::handleHelp() {
    _server.send(200, "text/html", getHelpHtml());
}

void WebManager::handleNotFound() {
    if (_apMode) {
        // Redirect to captive portal
        _server.sendHeader("Location", "http://192.168.4.1/", true);
        _server.send(302, "text/plain", "");
    } else {
        _server.send(404, "text/plain", "Not Found");
    }
}

String WebManager::getHtml() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += htmlStyle;
    html += "</head><body>";
    html += "<h2>Dial-A-Charmer Setup</h2>";
    html += "<form action='/save' method='POST'>";
    
    html += "<div class='card'><h3>WiFi Settings</h3>";
    html += "<label>SSID</label><input type='text' name='ssid' value='" + settings.getWifiSSID() + "'>";
    html += "<label>Password</label><input type='password' name='pass' value='" + settings.getWifiPass() + "'>";
    html += "</div>";
    
    html += "<div class='card'><h3>Time Settings</h3>";
    html += "<label>Timezone Offset (Hours)</label><input type='number' name='tz' value='" + String(settings.getTimezoneOffset()) + "'>";
    html += "</div>";

    html += "<div class='card'><h3>Audio Settings</h3>";
    html += "<label>Volume (0-42)</label>";
    html += "<input type='range' name='vol' min='0' max='42' value='" + String(settings.getVolume()) + "' oninput='this.nextElementSibling.value = this.value'>";
    html += "<output>" + String(settings.getVolume()) + "</output>";
    html += "</div>";

    html += "<div class='card'><h3>AI Settings</h3>";";
    html += "<label>Gemini API Key (Optional)</label><input type='password' name='gemini' value='" + settings.getGeminiKey() + "'>";
    html += "<small>Leave empty to use SD card audio only.</small>";
    html += "</div>";
    
    html += "<button type='submit'>Save & Reboot</button>";
    html += "</form>";
    html += "<p style='text-align:center'><a href='/help' style='color:#ffc107'>Usage Help</a></p>";
    html += "</body></html>";
    return html;
}

String WebManager::getHelpHtml() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += htmlStyle;
    html += "</head><body>";
    html += "<h2>User Manual ☎️</h2>";
    
    html += "<div class='card'><h3>1. Time & Alarm</h3>";
    html += "<ul><li><b>Receiver Down:</b> Dial a number (1-9) to set a Timer in minutes.</li>";
    html += "<li><b>Receiver Up:</b> Dial 0 to hear the current time.</li>";
    html += "<li><b>Stop Alarm:</b> Lift the receiver or tap the hook switch.</li></ul></div>";
    
    html += "<div class='card'><h3>2. Compliments (AI)</h3>";
    html += "<ul><li>Lift receiver and dial:</li>";
    html += "<li><b>1:</b> Direct Compliment</li>";
    html += "<li><b>2:</b> Nerd Joke</li>";
    html += "<li><b>3:</b> Sci-Fi Wisdom</li>";
    html += "<li><b>4:</b> Captain Persona</li></ul></div>";
    
    html += "<div class='card'><h3>3. Settings</h3>";
    html += "<ul><li><b>Change Ringtone:</b> Hold the 'Extra Button' and dial 1-5.</li>";
    html += "<li><b>Web Config:</b> Connect to 'DialACharmer' WiFi.</li></ul></div>";
    
    html += "<div class='card'><a href='/' class='btn' style='background:#444;color:#fff;text-align:center;text-decoration:none;display:block'>Back to Settings</a></div>";
    
    html += "</body></html>";
    return html;
}
