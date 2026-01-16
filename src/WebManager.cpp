#include "WebManager.h"
#include "LedManager.h"
#include "PhonebookManager.h"
#include <ESPmDNS.h>
#include <Update.h>

WebManager webManager;

const char* htmlStyle = R"rawliteral(
<style>
body {
    font-family: 'Times New Roman', Times, serif;
    background-color: #080808; /* Deep Black */
    color: #f0e6d2; /* Cream/Champagne */
    margin: 0;
    padding: 20px;
    line-height: 1.5;
}
h2 {
    text-align: center;
    text-transform: uppercase;
    letter-spacing: 6px; /* Art Deco Spacing */
    color: #d4af37; /* Metallic Gold */
    border-bottom: 2px solid #d4af37;
    margin-bottom: 40px;
    padding-bottom: 15px;
    font-weight: normal;
}
.card {
    background: #111;
    border: 1px solid #222;
    border-top: 4px solid #d4af37; /* Gold Accent */
    padding: 30px;
    margin-bottom: 30px;
    box-shadow: 0 10px 30px rgba(0,0,0,0.8);
}
.card h3 {
    margin-top: 0;
    color: #d4af37;
    font-size: 1.5rem;
    text-transform: uppercase;
    letter-spacing: 3px;
    border-bottom: 1px solid #333;
    padding-bottom: 15px;
    font-weight: normal;
}
label {
    display: block;
    margin-top: 20px;
    font-size: 1.2rem;
    text-transform: uppercase;
    letter-spacing: 2px;
    color: #888;
}
input, select {
    width: 100%;
    padding: 12px;
    margin-top: 5px;
    background-color: #f0e6d2;
    border: 2px solid #333;
    color: #111;
    font-family: 'Times New Roman', Times, serif;
    font-size: 1.4rem;
    box-sizing: border-box;
    border-radius: 0; /* Sharp Edges (Bauhaus/Deco) */
}
input:focus, select:focus {
    outline: none;
    border-color: #d4af37;
    background-color: #fff;
}
button {
    width: 100%;
    padding: 18px;
    margin-top: 30px;
    background-color: #8b0000; /* Deep Red */
    color: #f0e6d2;
    border: 1px solid #a00000;
    text-transform: uppercase;
    letter-spacing: 4px;
    font-size: 1.5rem;
    cursor: pointer;
    transition: all 0.3s;
    font-family: 'Times New Roman', Times, serif;
}
button:hover {
    background-color: #b22222;
    color: #fff;
    border-color: #f00;
    box-shadow: 0 0 15px rgba(178, 34, 34, 0.4);
}
output {
    float: right;
    color: #d4af37;
    font-family: monospace;
    font-size: 1.2em;
}
a { color: #d4af37; text-decoration: none; border-bottom: 1px dotted #d4af37; transition: 0.3s; }
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
    _server.on("/advanced", [this](){ handleAdvanced(); }); // New
    _server.on("/phonebook", [this](){ handlePhonebook(); });
    _server.on("/api/phonebook", [this](){ handlePhonebookApi(); });
    _server.on("/api/preview", [this](){ handlePreviewApi(); }); // New
    
    // OTA Update
    _server.on("/update", HTTP_POST, [this](){
            _server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
            ESP.restart();
        }, [this](){
            HTTPUpload& upload = _server.upload();
            if (upload.status == UPLOAD_FILE_START) {
                Serial.printf("Update: %s\n", upload.filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { 
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) { 
                    Serial.printf("Update Success: %uB\n", upload.totalSize);
                } else {
                    Update.printError(Serial);
                }
            }
        });

    _server.on("/help", [this](){ handleHelp(); });
    _server.on("/save", HTTP_POST, [this](){ handleSave(); });
    _server.onNotFound([this](){ handleNotFound(); });
    _server.begin();
    
    // Auto-off AP Timer
    _apEndTime = millis() + 600000; // 10 Minutes
}

void WebManager::loop() {
    if (_apMode) {
        _dnsServer.processNextRequest();
        
        // Timeout Check
        if (millis() > _apEndTime) {
             Serial.println("AP Timeout -> Stopping Access Point");
             stopAp();
        }
    }
    _server.handleClient();
}

void WebManager::startAp() {
    if (_apMode) {
        resetApTimer(); // Just extend
        return;
    }
    _apMode = true;
    WiFi.mode(WIFI_AP_STA); // Keep STA up if possible? Or just AP?
    // User requested "Enable AP". Usually alongside existing connection if debugging?
    // Or fallback.
    // Let's assume AP_STA to not break STA.
    WiFi.softAP(CONF_AP_SSID); 
    _dnsServer.start(_dnsPort, "*", WiFi.softAPIP());
    resetApTimer();
    Serial.println("AP Started via Request");
}

void WebManager::stopAp() {
    if (!_apMode) return;
    WiFi.softAPdisconnect(true);
    _apMode = false;
    Serial.println("AP Stopped");
}

void WebManager::resetApTimer() {
    _apEndTime = millis() + 600000; 
}

void WebManager::handleRoot() {
    resetApTimer();
    _server.send(200, "text/html", getHtml());
}

void WebManager::handleAdvanced() {
    resetApTimer();
    _server.send(200, "text/html", getAdvancedHtml());
}

extern void playPreviewSound(String type, int index); // Defined in main.cpp

void WebManager::handlePreviewApi() {
    if (_server.hasArg("type") && _server.hasArg("id")) {
        String type = _server.arg("type");
        int id = _server.arg("id").toInt();
        resetApTimer();
        playPreviewSound(type, id);
        _server.send(200, "text/plain", "OK");
    } else {
        _server.send(400, "text/plain", "Missing Args");
    }
}

void WebManager::handleSave() {
    resetApTimer();
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
    
    // System Settings
    if (_server.hasArg("lang")) settings.setLanguage(_server.arg("lang"));
    if (_server.hasArg("tz")) settings.setTimezoneOffset(_server.arg("tz").toInt());
    if (_server.hasArg("gemini")) settings.setGeminiKey(_server.arg("gemini"));
    
    // Audio Settings
    if (_server.hasArg("vol")) settings.setVolume(_server.arg("vol").toInt());
    if (_server.hasArg("base_vol")) settings.setBaseVolume(_server.arg("base_vol").toInt());
    if (_server.hasArg("snooze")) settings.setSnoozeMinutes(_server.arg("snooze").toInt()); // Added
    if (_server.hasArg("ring")) settings.setRingtone(_server.arg("ring").toInt());
    if (_server.hasArg("dt")) settings.setDialTone(_server.arg("dt").toInt());
    
    // Checkbox handling (Browser sends nothing if unchecked)
    // Only update if we are in the form that has this checkbox
    if (_server.arg("form_id") == "advanced") {
        bool hd = _server.hasArg("hd"); 
        settings.setHalfDuplex(hd);
    }
    
    if (_server.arg("form_id") == "basic") {
        if (_server.hasArg("alm_h")) settings.setAlarmHour(_server.arg("alm_h").toInt());
        if (_server.hasArg("alm_m")) settings.setAlarmMinute(_server.arg("alm_m").toInt());
        
        // Days Bitmask
        int mask = 0;
        for(int i=0; i<7; i++) {
            if(_server.hasArg("day_" + String(i+1))) {
                mask |= (1 << i);
            }
        }
        settings.setAlarmDays(mask);
    }

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

    ledManager.reloadSettings(); // Apply new LED settings immediately

    String html = "<html><body><h1>Saved!</h1>";
    if (wifiChanged) {
        html += "<p>WiFi settings changed. Rebooting to apply...</p></body></html>";
        _server.send(200, "text/html", html);
        delay(1000);
        ESP.restart();
    } else {
        // Redirect back
        String loc = "/";
        if (_server.hasArg("redirect")) loc = _server.arg("redirect");
        
        _server.sendHeader("Location", loc, true);
        _server.send(302, "text/plain", "");
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
    String lang = settings.getLanguage();
    bool isDe = (lang == "de");

    // Translations
    String t_title = isDe ? "Dial-A-Charmer" : "Dial-A-Charmer";
    String t_audio = isDe ? "Audio Einstellungen" : "Audio Settings";
    String t_h_vol = isDe ? "Hörer Lautstärke" : "Handset Volume";
    String t_r_vol = isDe ? "Klingelton Lautstärke (Basis)" : "Ringer Volume (Base)";
    String t_ring = isDe ? "Klingelton" : "Ringtone";
    String t_dt = isDe ? "Wählton" : "Dial Tone";
    String t_led = isDe ? "LED Einstellungen" : "LED Settings";
    String t_day = isDe ? "Helligkeit (Tag)" : "Day Brightness";
    String t_night = isDe ? "Helligkeit (Nacht)" : "Night Brightness";
    String t_n_start = isDe ? "Nachtmodus Start (Std)" : "Night Start Hour";
    String t_n_end = isDe ? "Nachtmodus Ende (Std)" : "Night End Hour";
    String t_save = isDe ? "Speichern" : "Save Settings";
    String t_pb = isDe ? "Telefonbuch" : "Phonebook";
    String t_help = isDe ? "Hilfe" : "Usage Help";
    String t_lang = isDe ? "Sprache" : "Language";
    String t_adv = isDe ? "Erweiterte Einstellungen" : "Advanced Settings";

    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += htmlStyle;
    html += "<script>function prev(t,i){fetch('/api/preview?type='+t+'&id='+i);}</script>";
    html += "</head><body>";
    html += "<h2>" + t_title + "</h2>";
    html += "<form action='/save' method='POST'>";
    html += "<input type='hidden' name='form_id' value='basic'>";

    // Language Selector
    html += "<div class='card'><h3>" + t_lang + "</h3>";
    html += "<label>" + t_lang + "</label>";
    html += "<select name='lang'>";
    html += "<option value='de'" + String(isDe ? " selected" : "") + ">Deutsch</option>";
    html += "<option value='en'" + String(!isDe ? " selected" : "") + ">English</option>";
    html += "</select>";
    html += "</div>";
    
    html += "<div class='card'><h3>" + t_audio + "</h3>";
    
    html += "<label>" + t_h_vol + " (0-42) <output>" + String(settings.getVolume()) + "</output></label>";
    html += "<input type='range' name='vol' min='0' max='42' value='" + String(settings.getVolume()) + "' oninput='this.previousElementSibling.firstElementChild.value = this.value'>";
    
    html += "<label>" + t_r_vol + " (0-42) <output>" + String(settings.getBaseVolume()) + "</output></label>";
    html += "<input type='range' name='base_vol' min='0' max='42' value='" + String(settings.getBaseVolume()) + "' oninput='this.previousElementSibling.firstElementChild.value = this.value'>";

    // Snooze Time
    html += "<label>" + String(isDe ? "Snooze Dauer (Min)" : "Snooze Time (Min)") + " (0-20)</label>";
    html += "<input type='number' name='snooze' min='0' max='20' value='" + String(settings.getSnoozeMinutes()) + "'>";

    // Ringtone
    html += "<label>" + t_ring + "</label><select name='ring' onchange='prev(\"ring\",this.value)'>";
    int r = settings.getRingtone();
    for(int i=1; i<=5; i++) {
        html += "<option value='" + String(i) + "'" + (r==i?" selected":"") + ">Ringtone " + String(i) + " (" + (i==5?"Digital":"Mechanical") + ")</option>";
    }
    html += "</select>";
    
    // Dial Tone
    html += "<label>" + t_dt + "</label><select name='dt' onchange='prev(\"dt\",this.value)'>";
    int dt = settings.getDialTone();
    for(int i=1; i<=3; i++) {
        html += "<option value='" + String(i) + "'" + (dt==i?" selected":"") + ">Dial Tone " + String(i) + " (" + (i==1?"DE": (i==2?"US":"UK")) + ")</option>";
    }
    html += "</select>";
    html += "</div>";

    // Repeating Alarm
    html += "<div class='card'><h3>" + String(isDe ? "Wecker (Wiederholend)" : "Alarm (Repeating)") + "</h3>";
    html += "<label>" + String(isDe ? "Zeit (Std:Min)" : "Time (Hr:Min)") + "</label>";
    html += "<div style='display:flex; gap:10px;'>";
    html += "<input type='number' name='alm_h' min='0' max='23' value='" + String(settings.getAlarmHour()) + "' style='width:48%'>";
    html += "<input type='number' name='alm_m' min='0' max='59' value='" + String(settings.getAlarmMinute()) + "' style='width:48%'>";
    html += "</div>";
    
    html += "<label>" + String(isDe ? "Aktive Tage" : "Active Days") + "</label>";
    html += "<div style='display:grid; grid-template-columns: repeat(4, 1fr); gap:10px; margin-top:10px;'>";
    String dNames[] = { "Mo", "Di/Tu", "Mi/We", "Do/Th", "Fr", "Sa", "So/Su" };
    int days = settings.getAlarmDays();
    for(int i=0; i<7; i++) {
        bool checked = (days & (1 << i));
        html += "<label style='font-size:1rem; text-transform:none; margin-top:0;'><input type='checkbox' name='day_" + String(i+1) + "' value='1'" + (checked?" checked":"") + " style='width:auto; transform: scale(1.5); margin-right:10px;'>" + dNames[i] + "</label>";
    }
    html += "</div></div>";

    html += "<div class='card'><h3>" + t_led + "</h3>";
    
    int dayVal = map(settings.getLedDayBright(), 0, 255, 0, 42); 
    html += "<label>" + t_day + " (0-42) <output>" + String(dayVal) + "</output></label>";
    html += "<input type='range' name='led_day' min='0' max='42' value='" + String(dayVal) + "' oninput='this.previousElementSibling.firstElementChild.value = this.value'>";
    
    int nightVal = map(settings.getLedNightBright(), 0, 255, 0, 42);
    html += "<label>" + t_night + " (0-42) <output>" + String(nightVal) + "</output></label>";
    html += "<input type='range' name='led_night' min='0' max='42' value='" + String(nightVal) + "' oninput='this.previousElementSibling.firstElementChild.value = this.value'>";
    
    html += "<label>" + t_n_start + " (0-23)</label>";
    html += "<input type='number' name='night_start' min='0' max='23' value='" + String(settings.getNightStartHour()) + "'>";
    
    html += "<label>" + t_n_end + " (0-23)</label>";
    html += "<input type='number' name='night_end' min='0' max='23' value='" + String(settings.getNightEndHour()) + "'>";
    html += "</div>";

    html += "<button type='submit'>" + t_save + "</button>";
    html += "</form>";
    html += "<p style='text-align:center'>";
    html += "<a href='/phonebook' style='color:#ffc107; margin-right: 20px;'>" + t_pb + "</a>";
    html += "<a href='/advanced' style='color:#ffc107; margin-right: 20px;'>" + t_adv + "</a>";
    html += "<a href='/help' style='color:#ffc107'>" + t_help + "</a>";
    html += "</p>";
    html += "</body></html>";
    return html;
}

String WebManager::getAdvancedHtml() {
    String lang = settings.getLanguage();
    bool isDe = (lang == "de");

    String t_title = isDe ? "Erweiterte Einstellungen" : "Advanced Settings";
    String t_wifi = isDe ? "WLAN Einstellungen" : "WiFi Settings";
    String t_ssid = "SSID";
    String t_pass = isDe ? "Passwort" : "Password";
    String t_time = isDe ? "Zeit Einstellungen" : "Time Settings";
    String t_tz = isDe ? "Zeitzone" : "Timezone";
    String t_hd = isDe ? "Half-Duplex (Echo-Unterdrückung)" : "Half-Duplex (AEC)";
    String t_audio_adv = isDe ? "Audio Erweitert" : "Audio Advanced";
    String t_ai = isDe ? "KI Einstellungen" : "AI Settings";
    String t_key = isDe ? "Gemini API Schlüssel (Optional)" : "Gemini API Key (Optional)";
    String t_save = isDe ? "Speichern" : "Save Settings";
    String t_back = isDe ? "Zurück" : "Back";

    // Scan for networks
    int n = WiFi.scanNetworks();
    String ssidOptions = "";
    for (int i = 0; i < n; ++i) {
        ssidOptions += "<option value='" + WiFi.SSID(i) + "'>";
    }

    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += htmlStyle;
    html += "</head><body>";
    html += "<h2>" + t_title + "</h2>";
    html += "<form action='/save' method='POST'>";
    html += "<input type='hidden' name='redirect' value='/advanced'>";
    html += "<input type='hidden' name='form_id' value='advanced'>";
    
    html += "<div class='card'><h3>" + t_wifi + "</h3>";
    html += "<label>" + t_ssid + "</label>";
    html += "<input type='text' name='ssid' list='ssidList' value='" + settings.getWifiSSID() + "' placeholder='Select or type SSID'>";
    html += "<datalist id='ssidList'>" + ssidOptions + "</datalist>";
    
    html += "<label>" + t_pass + "</label><input type='password' name='pass' value='" + settings.getWifiPass() + "'>";
    html += "</div>";
    
    html += "<div class='card'><h3>" + t_time + "</h3>";
    html += "<label>" + t_tz + "</label>";
    html += "<select name='tz'>";
    int tz = settings.getTimezoneOffset();
    const char* labels[] = { "UTC -1 (Azores)", "UTC +0 (London, Dublin, Lisbon)", "UTC +1 (Zurich, Paris, Rome)", "UTC +2 (Athens, Helsinki, Kyiv)", "UTC +3 (Moscow, Istanbul)" };
    int offsets[] = { -1, 0, 1, 2, 3 };
    for(int i=0; i<5; i++) {
        html += "<option value='" + String(offsets[i]) + "'";
        if(tz == offsets[i]) html += " selected";
        html += ">" + String(labels[i]) + "</option>";
    }
    html += "</select>";
    html += "</div>";

    html += "<div class='card'><h3>" + t_audio_adv + "</h3>";
    // Half Duplex
    html += "<label style='display:flex;align-items:center;margin-top:20px;'><input type='checkbox' name='hd' value='1' style='width:30px;height:30px;margin-right:10px;'" + String(settings.getHalfDuplex() ? " checked" : "") + "> " + t_hd + "</label>";
    html += "</div>";

    html += "<div class='card'><h3>" + t_ai + "</h3>";
    html += "<label>" + t_key + "</label><input type='password' name='gemini' value='" + settings.getGeminiKey() + "'>";
    html += "<small>Leave empty to use SD card audio only.</small>";
    html += "</div>";
    
    html += "<button type='submit'>" + t_save + "</button>";
    html += "</form>";

    // OTA Update Form
    html += "<div class='card'><h3>Firmware Update (OTA)</h3>";
    html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
    html += "<input type='file' name='update' accept='.bin'>";
    html += "<button type='submit' style='background-color:#444; margin-top:10px;'>Start Update</button>";
    html += "</form></div>";
    
    html += "<p style='text-align:center'>";
    html += "<a href='/' style='color:#ffc107'>" + t_back + "</a>";
    html += "</p>";
    html += "</body></html>";
    return html;
}

void WebManager::handlePhonebook() {
    _server.send(200, "text/html", getPhonebookHtml());
}

void WebManager::handlePhonebookApi() {
    if (_server.method() == HTTP_GET) {
        // Return JSON
        File f = SPIFFS.open("/phonebook.json", "r");
        if (!f) {
            _server.send(500, "application/json", "{\"error\":\"File not found\"}");
            return;
        }
        _server.streamFile(f, "application/json");
        f.close();
    } 
    else if (_server.method() == HTTP_POST) {
        if (_server.hasArg("plain")) {
            String body = _server.arg("plain");
            // Validate JSON
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, body);
            if (error) {
                _server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }
            // Save via Manager
            phonebook.saveAll(doc.as<JsonObject>());
            _server.send(200, "application/json", "{\"status\":\"saved\"}");
        } else {
             _server.send(400, "application/json", "{\"error\":\"No Body\"}");
        }
    }
    else {
        _server.send(405, "text/plain", "Method Not Allowed");
    }
}

String WebManager::getPhonebookHtml() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += htmlStyle;
    html += R"rawliteral(
<style>
table { width: 100%; border-collapse: collapse; margin-top: 20px; color: #f0e6d2; }
th, td { border: 1px solid #333; padding: 10px; text-align: left; }
th { background-color: #222; color: #d4af37; }
input.tbl-in { width: 100%; margin: 0; padding: 5px; background: transparent; border: none; color: white; }
select.tbl-sel { width: 100%; margin: 0; padding: 5px; background: #222; border: none; color: white; }
.btn-small { padding: 5px 10px; margin: 0; font-size: 0.9rem; width: auto; display: inline-block; background-color: #333; }
.btn-delete { background-color: #800; }
</style>
<script>
async function load() {
    try {
        const res = await fetch('/api/phonebook');
        const data = await res.json();
        render(data);
    } catch(e) { alert('Load Error: ' + e); }
}

let currentData = {};

function render(data) {
    currentData = data; // Keep ref
    const tbody = document.getElementById('tbody');
    tbody.innerHTML = '';
    
    // Convert obj to array and Sort by key (number)
    const entries = Object.entries(data).sort((a,b) => parseInt(a[0]) - parseInt(b[0]));
    
    entries.forEach(([num, entry]) => {
        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td><input class="tbl-in" value="${num}" readonly></td>
            <td><input class="tbl-in" value="${entry.name}" onchange="update('${num}', 'name', this.value)"></td>
            <td>
                <select class="tbl-sel" onchange="update('${num}', 'type', this.value)">
                    <option value="FUNCTION" ${entry.type=='FUNCTION'?'selected':''}>Function</option>
                    <option value="AUDIO" ${entry.type=='AUDIO'?'selected':''}>Audio File</option>
                    <option value="TTS" ${entry.type=='TTS'?'selected':''}>AI Speaking</option>
                </select>
            </td>
            <td><input class="tbl-in" value="${entry.value}" onchange="update('${num}', 'value', this.value)"></td>
            <td><button class="btn-small btn-delete" onclick="del('${num}')">X</button></td>
        `;
        tbody.appendChild(tr);
    });
}
function addRow() {
    const num = prompt("Enter Dial Number (0-99):");
    if(!num) return;
    if(currentData[num]) { alert("Number exists!"); return; }
    
    currentData[num] = { name: "New Entry", type: "AUDIO", value: "/file.mp3", parameter: "" };
    render(currentData);
}

function update(num, field, val) {
    if(currentData[num]) currentData[num][field] = val;
}
function del(num) {
    if(confirm('Delete '+num+'?')) {
        delete currentData[num];
        render(currentData);
    }
}
async function save() {
    try {
        await fetch('/api/phonebook', {
            method: 'POST',
            body: JSON.stringify(currentData)
        });
        alert('Saved!');
    } catch(e) { alert('Save Error: ' + e); }
}
document.addEventListener('DOMContentLoaded', load);
</script>
)rawliteral";

    html += "</head><body>";
    html += "<h2>Phonebook Editor</h2>";
    html += "<div class='card'>";
    html += "<p>Manage Dialed Numbers. Use standard numbers (0-9) or shortcuts.</p>";
    html += "<div style='overflow-x:auto;'>";
    html += "<table><thead><tr><th width='10%'>#</th><th width='30%'>Name</th><th width='20%'>Type</th><th width='30%'>Value/File</th><th width='10%'>Act</th></tr></thead>";
    html += "<tbody id='tbody'></tbody></table>";
    html += "</div>";
    html += "<button onclick='addRow()' class='btn-small' style='margin-top:20px; background:#444;'>+ Add Entry</button>";
    html += "<button onclick='save()' style='margin-top:20px;'>Save Changes</button>";
    html += "</div>";
    
    html += "<p style='text-align:center'><a href='/'>Back to Settings</a></p>";
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
