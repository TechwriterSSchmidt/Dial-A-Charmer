#include "WebManager.h"
#include "LedStatus.h"
#include <ESPmDNS.h>

WebManager webManager;

const char* htmlStyle = R"rawliteral(
<style>
body {
    font-family: 'Times New Roman', Times, serif;
    margin: 0;
    padding: 20px;
    background-color: #2b1d0e; /* Bakelite Brown */
    color: #e8dcca; /* Cream/Ivory */
    background-image: radial-gradient(#3a2817 15%, transparent 16%), radial-gradient(#3a2817 15%, transparent 16%);
    background-size: 20px 20px;
    background-position: 0 0, 10px 10px;
}
h2 {
    text-align: center;
    color: #d4af37; /* Brass/Gold */
    border-bottom: 3px double #d4af37;
    padding-bottom: 10px;
    text-transform: uppercase;
    text-shadow: 2px 2px 0px #000;
}
.card {
    background: #1a1108;
    padding: 20px;
    border: 2px solid #5c4033;
    margin-bottom: 25px;
    border-radius: 10px;
    box-shadow: 5px 5px 15px rgba(0,0,0,0.5);
    position: relative;
}
.card::before {
    content: '';
    position: absolute;
    top: 5px; left: 5px; right: 5px; bottom: 5px;
    border: 1px dashed #5c4033;
    border-radius: 5px;
    pointer-events: none;
}
.card h3 {
    margin-top: 0;
    color: #d4af37;
    border-bottom: 1px solid #5c4033;
    padding-bottom: 5px;
    font-size: 1.4em;
}
input, select {
    width: 100%;
    padding: 12px;
    margin: 8px 0;
    background: #e8dcca;
    color: #2b1d0e;
    border: 2px solid #5c4033;
    box-sizing: border-box;
    font-family: 'Times New Roman', Times, serif;
    font-size: 1.1em;
    border-radius: 3px;
    box-shadow: inset 2px 2px 5px rgba(0,0,0,0.2);
}
input:focus {
    outline: none;
    background: #fff;
    border-color: #d4af37;
}
label {
    display: block;
    margin-top: 15px;
    font-weight: bold;
    color: #a89f91;
    text-transform: uppercase;
    font-size: 0.9em;
    letter-spacing: 1px;
}
output {
    display: inline-block;
    float: right;
    color: #d4af37;
    font-weight: bold;
}
button {
    width: 100%;
    padding: 15px;
    margin-top: 20px;
    background: #800000; /* Dark Red Button */
    color: #e8dcca;
    border: 4px solid #1a0f00;   
    font-weight: bold;
    font-family: 'Times New Roman', Times, serif;
    font-size: 1.5em;
    cursor: pointer;
    text-transform: uppercase;
    border-radius: 50px; /* Round Button */
    box-shadow: 0 5px 0 #3d0000, 0 10px 10px rgba(0,0,0,0.4);
    transition: all 0.1s;
}
button:active {
    box-shadow: 0 2px 0 #3d0000, 0 5px 5px rgba(0,0,0,0.4);
    transform: translateY(3px);
}
button:hover {
    background: #a00000;
}
a { color: #d4af37; text-decoration: none; border-bottom: 1px dotted #d4af37; }
a:hover { color: #fff; border-bottom: 1px solid #fff; }
</style>
)rawliteral";

void WebManager::begin() {
    String ssid = settings.getWifiSSID();
    String pass = settings.getWifiPass();
    bool connected = false;

    // Try to connect if SSID is set
    if (ssid != "") {
        WiFi.mode(WIFI_STA);
        Serial.printf("Connecting to WiFi SSID: '%s'", ssid.c_str());
        
        WiFi.begin(ssid.c_str(), pass.c_str());
        
        int tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries < 20) {
            delay(500);
            Serial.print(".");
            tries++;
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            _apMode = false;
            Serial.print("Connected! IP: ");
            Serial.println(WiFi.localIP());
            if (MDNS.begin("dial-a-charmer")) {
                Serial.println("mDNS responder started. You can access it via http://dial-a-charmer.local");
            }
        } else {
            Serial.println("WiFi Connection Failed.");
        }
    }

    // Fallback to AP if not connected
    if (!connected) {
        _apMode = true;
        WiFi.mode(WIFI_AP);
        WiFi.softAP(CONF_AP_SSID); 
        _dnsServer.start(_dnsPort, "*", WiFi.softAPIP());
        Serial.print("AP Started (Fallback): ");
        Serial.println(CONF_AP_SSID);
        Serial.print("AP IP: ");
        Serial.println(WiFi.softAPIP());
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
    bool wifiChanged = false;

    if (_server.hasArg("ssid")) {
        String newSSID = _server.arg("ssid");
        // Only update if changed
        if (newSSID != settings.getWifiSSID()) {
            settings.setWifiSSID(newSSID);
            wifiChanged = true;
        }
    }
    if (_server.hasArg("pass")) {
        String newPass = _server.arg("pass");
        if (newPass != settings.getWifiPass()) {
             settings.setWifiPass(newPass);
             wifiChanged = true;
        }
    }
    
    if (_server.hasArg("tz")) settings.setTimezoneOffset(_server.arg("tz").toInt());
    if (_server.hasArg("gemini")) settings.setGeminiKey(_server.arg("gemini"));
    if (_server.hasArg("vol")) settings.setVolume(_server.arg("vol").toInt());
    
    // LED Settings
    if (_server.hasArg("led_day")) {
        int val = _server.arg("led_day").toInt();
        settings.setLedDayBright(map(val, 0, 42, 0, 255));
    }
    if (_server.hasArg("led_night")) {
        int val = _server.arg("led_night").toInt();
        settings.setLedNightBright(map(val, 0, 42, 0, 255));
    }
    if (_server.hasArg("night_start")) settings.setNightStartHour(_server.arg("night_start").toInt());
    if (_server.hasArg("night_end")) settings.setNightEndHour(_server.arg("night_end").toInt());

    statusLed.reloadSettings(); // Apply new LED settings immediately

    String html = "<html><body><h1>Saved!</h1>";
    if (wifiChanged) {
        html += "<p>WiFi settings changed. Rebooting to apply...</p></body></html>";
        _server.send(200, "text/html", html);
        delay(1000);
        ESP.restart();
    } else {
        html += "<p>Settings applied.</p><p><a href='/'>Back</a></p></body></html>";
        _server.send(200, "text/html", html);
    }
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
    // Scan for networks
    int n = WiFi.scanNetworks();
    String ssidOptions = "";
    for (int i = 0; i < n; ++i) {
        ssidOptions += "<option value='" + WiFi.SSID(i) + "'>";
    }

    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += htmlStyle;
    html += "</head><body>";
    html += "<h2>Dial-A-Charmer Setup</h2>";
    html += "<form action='/save' method='POST'>";
    
    html += "<div class='card'><h3>WiFi Settings</h3>";
    html += "<label>SSID</label>";
    html += "<input type='text' name='ssid' list='ssidList' value='" + settings.getWifiSSID() + "' placeholder='Select or type SSID'>";
    html += "<datalist id='ssidList'>" + ssidOptions + "</datalist>";
    
    html += "<label>Password</label><input type='password' name='pass' value='" + settings.getWifiPass() + "'>";
    html += "</div>";
    
    html += "<div class='card'><h3>Time Settings</h3>";
    html += "<label>Timezone (Europe)</label>";
    html += "<select name='tz'>";
    int tz = settings.getTimezoneOffset();
    const char* labels[] = { "UTC -1 (Azores)", "UTC +0 (London, Dublin, Lisbon)", "UTC +1 (Zürich, Paris, Rome)", "UTC +2 (Athens, Helsinki, Kyiv)", "UTC +3 (Moscow, Istanbul)" };
    int offsets[] = { -1, 0, 1, 2, 3 };
    for(int i=0; i<5; i++) {
        html += "<option value='" + String(offsets[i]) + "'";
        if(tz == offsets[i]) html += " selected";
        html += ">" + String(labels[i]) + "</option>";
    }
    html += "</select>";
    html += "</div>";

    html += "<div class='card'><h3>Audio Settings</h3>";
    html += "<label>Volume (0-42) <output>" + String(settings.getVolume()) + "</output></label>";
    html += "<input type='range' name='vol' min='0' max='42' value='" + String(settings.getVolume()) + "' oninput='this.previousElementSibling.firstElementChild.value = this.value'>";
    html += "</div>";

    html += "<div class='card'><h3>LED Settings</h3>";
    
    int dayVal = map(settings.getLedDayBright(), 0, 255, 0, 42); 
    html += "<label>Day Brightness (0-42) <output>" + String(dayVal) + "</output></label>";
    html += "<input type='range' name='led_day' min='0' max='42' value='" + String(dayVal) + "' oninput='this.previousElementSibling.firstElementChild.value = this.value'>";
    
    int nightVal = map(settings.getLedNightBright(), 0, 255, 0, 42);
    html += "<label>Night Brightness (0-42) <output>" + String(nightVal) + "</output></label>";
    html += "<input type='range' name='led_night' min='0' max='42' value='" + String(nightVal) + "' oninput='this.previousElementSibling.firstElementChild.value = this.value'>";
    
    html += "<label>Night Start Hour (0-23)</label>";
    html += "<input type='number' name='night_start' min='0' max='23' value='" + String(settings.getNightStartHour()) + "'>";
    
    html += "<label>Night End Hour (0-23)</label>";
    html += "<input type='number' name='night_end' min='0' max='23' value='" + String(settings.getNightEndHour()) + "'>";
    html += "</div>";

    html += "<div class='card'><h3>AI Settings</h3>";
    html += "<label>Gemini API Key (Optional)</label><input type='password' name='gemini' value='" + settings.getGeminiKey() + "'>";
    html += "<small>Leave empty to use SD card audio only.</small>";
    html += "</div>";
    
    html += "<button type='submit'>Save Settings</button>";
    html += "</form>";
    html += "<p style='text-align:center'><a href='/help' style='color:#ffc107'>Usage Help</a></p>";
    html += "</body></html>";
    return html;
}

String WebManager::getHelpHtml() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += htmlStyle;
    html += "</head><body>";
    html += "<h2>User Manual</h2>";
    
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
