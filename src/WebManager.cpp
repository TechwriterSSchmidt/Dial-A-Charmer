#include "WebManager.h"
#include "LedManager.h"
#include "PhonebookManager.h"
#include <ESPmDNS.h>
#include <Update.h>
#include <SD.h>
#include <vector>
#include <algorithm>

WebManager webManager;

// Helper to list files for dropdown
String getSdFileOptions(String folder, int currentSelection) {
    String options = "";
    File dir = SD.open(folder);
    if(!dir || !dir.isDirectory()) return "<option>No Folder " + folder + "</option>";

    std::vector<String> files;
    File file = dir.openNextFile();
    while(file){
        if(!file.isDirectory()) {
            String name = file.name();
            if (name.startsWith(".")) { file = dir.openNextFile(); continue; } // skip hidden
            files.push_back(name);
        }
        file = dir.openNextFile();
    }
    
    // Sort Alphabetically to ensure consistent Indexing
    std::sort(files.begin(), files.end());
    
    // Add "None" or "Default" if index 0 is special? 
    // Usually index 1 is first file.
    
    for(size_t i=0; i<files.size(); i++) {
        // IDs start at 1 usually in this system
        int id = i + 1;
        String sel = (id == currentSelection) ? " selected" : "";
        options += "<option value='" + String(id) + "'" + sel + ">" + files[i] + "</option>";
    }
    return options;
}

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

    _server.on("/", [this](){ handlePhonebook(); /* Was Root */ }); 
    _server.on("/settings", [this](){ handleRoot(); /* Old Root is now Settings */ }); // New
    _server.on("/advanced", [this](){ handleAdvanced(); }); 
    _server.on("/phonebook", [this](){ handlePhonebook(); }); // Keep alias
    _server.on("/api/phonebook", [this](){ handlePhonebookApi(); });
    _server.on("/api/preview", [this](){ handlePreviewApi(); });
    
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
        // Save 7 Alarms
        for(int i=0; i<7; i++) {
             String s = String(i);
             if(_server.hasArg("alm_h_"+s)) settings.setAlarmHour(i, _server.arg("alm_h_"+s).toInt());
             if(_server.hasArg("alm_m_"+s)) settings.setAlarmMinute(i, _server.arg("alm_m_"+s).toInt());
             settings.setAlarmEnabled(i, _server.hasArg("alm_en_"+s));
        }
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
    String t_h_vol = isDe ? "H&ouml;rer Lautst&auml;rke" : "Handset Volume";
    String t_r_vol = isDe ? "Klingelton Lautst&auml;rke (Basis)" : "Ringer Volume (Base)";
    String t_ring = isDe ? "Klingelton" : "Ringtone";
    String t_dt = isDe ? "W&auml;hlton" : "Dial Tone";
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

    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += htmlStyle;
    html += "<script>function sl(v){fetch('/save?lang='+v,{method:'POST'}).then(r=>location.reload());}</script>"; // Save Lang
    html += "</head><body>";
    html += "<h2>" + t_title + "</h2>";

    // Language Selector (Auto-Save)
    html += "<div class='card'><h3>" + t_lang + "</h3>";
    html += "<select onchange='sl(this.value)'>";
    html += "<option value='de'" + String(isDe ? " selected" : "") + ">Deutsch</option>";
    html += "<option value='en'" + String(!isDe ? " selected" : "") + ">English</option>";
    html += "</select>";
    html += "</div>";

    html += "<form action='/save' method='POST'>";
    html += "<input type='hidden' name='form_id' value='basic'>";
    html += "<input type='hidden' name='redirect' value='/settings'>"; // Redirect back to settings

    // Audio & LED removed from here
    
    // Repeating Alarm
    html += "<div class='card'><h3>" + String(isDe ? "T&auml;gliche Wecker" : "Daily Alarms") + "</h3>";
    html += "<div style='display:flex; justify-content:space-between; margin-bottom:10px; color:#888; font-size:0.8rem;'><span>Day</span><span>Active</span><span>Time</span></div>";
    String dNames[] = { "Mo", "Di/Tu", "Mi/We", "Do/Th", "Fr", "Sa", "So/Su" };
    
    for(int i=0; i<7; i++) {
         html += "<div style='display:flex; align-items:center; margin-bottom:10px; border-bottom:1px dotted #333; padding-bottom:10px;'>";
         // Label
         html += "<div style='width:50px; font-weight:bold; color:#d4af37'>" + dNames[i] + "</div>";
         
         // Enabled (Checkbox)
         bool en = settings.isAlarmEnabled(i);
         html += "<input type='checkbox' name='alm_en_" + String(i) + "' value='1'" + (en?" checked":"") + " style='width:30px; height:30px; margin:0 15px 0 0;'>";
         
         // Time Inputs
         html += "<input type='number' name='alm_h_" + String(i) + "' min='0' max='23' value='" + String(settings.getAlarmHour(i)) + "' style='width:70px; margin-right:5px;'>";
         html += "<span style='font-size:1.5rem; margin-top:-5px;'>:</span>";
         html += "<input type='number' name='alm_m_" + String(i) + "' min='0' max='59' value='" + String(settings.getAlarmMinute(i)) + "' style='width:70px; margin-left:5px;'>";
         
         html += "</div>";
    }
    html += "</div>";

    // LED Moved to Advanced

    html += "<button type='submit'>" + t_save + "</button>";
    html += "</form>";
    html += "<p style='text-align:center'>";
    html += "<a href='/' style='color:#ffc107; margin-right: 20px;'>" + t_pb + "</a>";
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
    String t_audio = isDe ? "Audio Einstellungen" : "Audio Settings";
    String t_h_vol = isDe ? "H&ouml;rer Lautst&auml;rke" : "Handset Volume";
    String t_r_vol = isDe ? "Klingelton Lautst&auml;rke (Basis)" : "Ringer Volume (Base)";
    String t_ring = isDe ? "Klingelton" : "Ringtone";
    String t_dt = isDe ? "W&auml;hlton" : "Dial Tone";
    String t_led = isDe ? "LED Einstellungen" : "LED Settings";
    String t_day = isDe ? "Helligkeit (Tag)" : "Day Brightness";
    String t_night = isDe ? "Helligkeit (Nacht)" : "Night Brightness";
    String t_n_start = isDe ? "Nachtmodus Start (Std)" : "Night Start Hour";
    String t_n_end = isDe ? "Nachtmodus Ende (Std)" : "Night End Hour";
    
    // Existing Advanced Strings...
    String t_wifi = isDe ? "WLAN Einstellungen" : "WiFi Settings";
    String t_ssid = "SSID";
    String t_pass = isDe ? "Passwort" : "Password";
    String t_time = isDe ? "Zeit Einstellungen" : "Time Settings";
    String t_tz = isDe ? "Zeitzone" : "Timezone";
    String t_hd = isDe ? "Half-Duplex (Echo-Unterdr&uuml;ckung)" : "Half-Duplex (AEC)";
    String t_audio_adv = isDe ? "Audio Erweitert" : "Audio Advanced";
    String t_ai = isDe ? "KI Einstellungen" : "AI Settings";
    String t_key = isDe ? "Gemini API Schl&uuml;ssel (Optional)" : "Gemini API Key (Optional)";
    String t_save = isDe ? "Speichern" : "Save Settings";
    String t_back = isDe ? "Zur&uuml;ck" : "Back";

    // Scan for networks
    int n = WiFi.scanNetworks();
    String ssidOptions = "";
    for (int i = 0; i < n; ++i) {
        ssidOptions += "<option value='" + WiFi.SSID(i) + "'>";
    }

    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += htmlStyle;
    html += "<script>function prev(t,i){fetch('/api/preview?type='+t+'&id='+i);}</script>";
    html += "</head><body>";
    html += "<h2>" + t_title + "</h2>";
    html += "<form action='/save' method='POST'>";
    html += "<input type='hidden' name='redirect' value='/advanced'>";
    html += "<input type='hidden' name='form_id' value='advanced'>";
    
    // --- MOVED AUDIO SECTIONS ---
    html += "<div class='card'><h3>" + t_audio + "</h3>";
    html += "<label>" + t_h_vol + " (0-42) <output>" + String(settings.getVolume()) + "</output></label>";
    html += "<input type='range' name='vol' min='0' max='42' value='" + String(settings.getVolume()) + "' oninput='this.previousElementSibling.firstElementChild.value = this.value'>";
    html += "<label>" + t_r_vol + " (0-42) <output>" + String(settings.getBaseVolume()) + "</output></label>";
    html += "<input type='range' name='base_vol' min='0' max='42' value='" + String(settings.getBaseVolume()) + "' oninput='this.previousElementSibling.firstElementChild.value = this.value'>";
    html += "<label>" + String(isDe ? "Snooze Dauer (Min)" : "Snooze Time (Min)") + " (0-20)</label>";
    html += "<input type='number' name='snooze' min='0' max='20' value='" + String(settings.getSnoozeMinutes()) + "'>";
    html += "<label>" + t_ring + "</label><select name='ring' onchange='prev(\"ring\",this.value)'>";
    html += getSdFileOptions("/ringtones", settings.getRingtone());
    html += "</select>";
    html += "<label>" + t_dt + "</label><select name='dt' onchange='prev(\"dt\",this.value)'>";
    html += getSdFileOptions("/system", settings.getDialTone()); 
    html += "</select>";
    html += "</div>";

    // --- MOVED LED SECTIONS ---
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
    html += "<a href='/settings' style='color:#ffc107'>" + t_back + "</a>";
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
    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    
    html += R"rawliteral(
<style>
body { 
    margin: 0; padding: 0;
    font-family: 'Courier New', Courier, monospace;
    background-color: #e8e8e0;
    background-image: radial-gradient(#d0d0c0 1px, transparent 1px);
    background-size: 20px 20px;
    height: 100vh;
    padding: 20px; 
}
.notepad {
    background: #fdfbf7;
    max-width: 900px;
    margin: 0 auto;
    min-height: 80vh;
    padding: 0;
    position: relative;
    box-shadow: 0 0 5px rgba(0,0,0,0.2);
    background-image: linear-gradient(#99ccff 1px, transparent 1px);
    background-size: 100% 3.0rem;
}
.notepad::before {
    content: ''; position: absolute; top:0; bottom:0; left: 4rem;
    border-left: 2px solid #ff9999; z-index: 10;
}
.header {
    background: transparent; padding: 2rem 0 1rem 5rem;
    border-bottom: 3px double #b00; margin-bottom: 0;
}
h2 {
    margin: 0; color: #b00; font-size: 2.5rem;
    font-family: 'Brush Script MT', cursive; transform: rotate(-2deg);
}
table { width: 100%; border-collapse: collapse; position: relative; z-index: 20; }
tr { height: 3.0rem; }
th {
    text-align: left; color: #b00; font-family: sans-serif; font-size: 0.8rem;
    padding-left: 10px; vertical-align: bottom; padding-bottom: 5px;
}
th.num-col { padding-left: 5rem; width: 3rem; }
td { vertical-align: bottom; padding: 0 10px; border: none; }
input, select {
    width: 100%; background: transparent; border: none;
    font-family: 'Courier New', Courier, monospace; font-weight: bold; font-size: 1.3rem;
    color: #000080; height: 2.5rem; outline: none;
}
.fab {
    position: fixed; bottom: 30px; right: 30px;
    width: 60px; height: 60px; background: #d4af37; border-radius: 50%;
    color: #fff; font-size: 30px; cursor: pointer; z-index: 100; border:none;
}
.btn-del {
    color: #a00; background: transparent; border: 1px solid #a00; border-radius: 50%;
    width: 25px; height: 25px; cursor: pointer;
}
.nav-back {
    position: absolute; top: 1rem; right: 2rem;
    text-decoration: none; font-weight: bold; color: #b00;
    font-family: sans-serif; border: 2px solid #b00; padding: 5px 10px;
}
</style>

<script>
async function load() { try { const res = await fetch('/api/phonebook'); render(await res.json()); } catch(e){} }
let currentData = {};
function render(data) {
    currentData = data;
    const tbody = document.getElementById('tbody');
    tbody.innerHTML = '';
    const entries = Object.entries(data).sort((a,b) => {
        let nA = parseInt(a[0]); let nB = parseInt(b[0]);
        if(!isNaN(nA) && !isNaN(nB)) return nA - nB;
        return a[0].localeCompare(b[0]);
    });
    entries.forEach(([num, entry]) => {
        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td style="padding-left: 4.5rem; text-align:right;"><input value="${num}" readonly style="text-align:right; font-family:sans-serif; color:#555;"></td>
            <td><input value="${entry.name}" onchange="update('${num}', 'name', this.value)"></td>
            <td style="width:120px;">
                <select onchange="update('${num}', 'type', this.value)">
                    <option value="FUNCTION" ${entry.type=='FUNCTION'?'selected':''}>Function</option>
                    <option value="AUDIO" ${entry.type=='AUDIO'?'selected':''}>Audio</option>
                    <option value="TTS" ${entry.type=='TTS'?'selected':''}>AI/TTS</option>
                </select>
            </td>
            <td><input value="${entry.value}" onchange="update('${num}', 'value', this.value)"></td>
            <td style="width:40px;"><button class="btn-del" onclick="del('${num}')">x</button></td>
        `;
        tbody.appendChild(tr);
    });
}
function update(n,f,v){ if(currentData[n]) currentData[n][f]=v; }
function del(n){ if(confirm('Del?')){ delete currentData[n]; render(currentData); } }
function addRow(){
    const n = prompt("Number:"); if(!n || currentData[n]) return;
    currentData[n] = { name: "New", type: "TTS", value: "Hello", parameter: "" };
    render(currentData);
}
async function save(){ await fetch('/api/phonebook', { method:'POST', body:JSON.stringify(currentData) }); alert('Saved!'); }
</script>
</head>
<body onload="load()">
    <div class="notepad">
        <a href="/settings" class="nav-back">Settings</a>
        <div class="header"><h2>Phone Directory</h2></div>
        <table>
            <thead><tr><th class="num-col">#</th><th>Name</th><th>Type</th><th>Details</th><th></th></tr></thead>
            <tbody id="tbody"></tbody>
        </table>
    </div>
    <button class="fab" onclick="save()" title="Save">💾</button>
    <button class="fab" style="bottom: 110px; background: #4caf50;" onclick="addRow()" title="Add">+</button>
</body></html>
)rawliteral";
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
    
    html += "<div class='card'><a href='/settings' class='btn' style='background:#444;color:#fff;text-align:center;text-decoration:none;display:block'>Back to Settings</a></div>";
    
    html += "</body></html>";
    return html;
}
