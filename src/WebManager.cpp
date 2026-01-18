#include "WebManager.h"
#include <esp_task_wdt.h>
#include "LedManager.h"
#include "PhonebookManager.h"
#include "WebResources.h" // Setup Styles
#include <ESPmDNS.h>
#include <Update.h>
#include <SD.h>
#include <FS.h>
#include <SPIFFS.h>
#include <vector>
#include <algorithm>

// Extern from main.cpp
extern void playSound(String filename, bool useSpeaker);

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
        
        // Remove extension for display
        String displayName = files[i];
        int dotIndex = displayName.lastIndexOf('.');
        if (dotIndex > 0) displayName = displayName.substring(0, dotIndex);
        
        options += "<option value='" + String(id) + "'" + sel + ">" + displayName + "</option>";
    }
    return options;
}

// MOVED TO WebResources.h: const char* htmlStyle

void WebManager::begin() {
    // Init FileSystem for Web Assets
    if(!SPIFFS.begin(true)){
        Serial.println("SPIFFS Mount Failed");
    }

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
            // Keep WDT happy during potential 10sec block
            esp_task_wdt_reset();
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
    _server.on("/phonebook", [this](){ handlePhonebook(); });
    _server.on("/settings", [this](){ handleSettings(); }); 
    _server.on("/advanced", [this](){ handleAdvanced(); });
    _server.on("/api/phonebook", [this](){ handlePhonebookApi(); });
    _server.on("/api/preview", [this](){ handlePreviewApi(); });
    _server.on("/help", [this](){ handleHelp(); }); // Moved UP

    // Reindex Storage
    _server.on("/api/reindex", [this](){
        Serial.println("Reindex requested via WebUI...");
        
        // Signal Start: Blue Pulse (CONNECTING)
        ledManager.setMode(LedManager::CONNECTING);
        ledManager.update();
        _server.send(200, "text/plain", "Reindexing started. Please wait for signal.");

        // Do the work
        File dir = SD.open("/playlists");
        std::vector<String> files;
        if (dir && dir.isDirectory()) {
            File f = dir.openNextFile();
            while(f) {
                String n = String(f.name());
                if (n.endsWith(".m3u") || n.endsWith(".idx")) {
                    if(n.startsWith("/")) files.push_back(n);
                    else files.push_back("/playlists/" + n);
                }
                f = dir.openNextFile();
            }
            dir.close();
        }
        for(const auto& p : files) {
            SD.remove(p);
            Serial.print("Removed cached playlist: "); Serial.println(p);
        }

        // Wait a bit to simulate/ensure processing
        delay(2000); 

        // Sound Feedback on Base Speaker
        playSound("/system/system_ready.mp3", true); 
        
        // We do NOT reboot immediately here, as the user might want to hear the sound.
        // But the prompt said "Reboot" in text. 
        // Actually, the user requirement says: "display via LED reindexing" and "play system ready when finished". 
        // Usually reindexing happens on next boot if files are missing.
        // So we MUST reboot to trigger `buildPlaylist()`.
        // We will play sound, wait for it, then reboot.
        
        delay(4000); // Wait for sound
        ESP.restart();
    });

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

    // Removed /help handler here as it was moved up

    _server.on("/save", HTTP_POST, [this](){ handleSave(); });
    _server.onNotFound([this](){ handleNotFound(); });

    // --- FONTS FROM SD CARD ---
    _server.on("/fonts/ZenTokyoZoo-Regular.ttf", [this](){
        if(SD.exists("/system/fonts/ZenTokyoZoo-Regular.ttf")){
            File f = SD.open("/system/fonts/ZenTokyoZoo-Regular.ttf", "r");
            _server.streamFile(f, "font/ttf");
            f.close();
        } else {
            _server.send(404, "text/plain", "Font Missing");
        }
    });

    _server.on("/fonts/Pompiere-Regular.ttf", [this](){
        if(SD.exists("/system/fonts/Pompiere-Regular.ttf")){
            File f = SD.open("/system/fonts/Pompiere-Regular.ttf", "r");
            _server.streamFile(f, "font/ttf");
            f.close();
        } else {
            _server.send(404, "text/plain", "Font Missing");
        }
    });
    
    // Serve Static Files (Last resort for assets like .css, .js)
    _server.serveStatic("/", SPIFFS, "/");
    
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
    if (_apMode) {
        _server.send(200, "text/html", getApSetupHtml());
    } else {
        // --- DASHBOARD (HOME) ---
        String lang = settings.getLanguage();
        bool isDe = (lang == "de");
        
        String t_title = "Dial-A-Charmer";
        String t_subtitle = isDe ? "H&ouml;rer abheben zum W&auml;hlen" : "Lift receiver to dial";
        String t_alarms = isDe ? "Wecker (Alarms)" : "Alarms";
        String t_pb = isDe ? "Telefonbuch" : "Phonebook";
        String t_config = isDe ? "Konfiguration" : "Configuration";
        String t_help = isDe ? "Hilfe / Manual" : "Help / Manual";

        String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
        html += COMMON_CSS; 
        html += "</head><body>";
        
        html += "<h2>" + t_title + "</h2>";
        
        html += "<div class='card' style='text-align:center;'>";
        html += "<p style='color:#888; font-style:italic; margin-bottom:25px; font-family: \"Pompiere\", cursive; font-size:1.3rem;'>" + t_subtitle + "</p>";
        
        // Navigation Buttons
        String btnStyle = "background-color:#1a1a1a; color:#d4af37; width:100%; border-radius:8px; padding:18px; font-size:1.5rem; margin-bottom:15px; border:1px solid #444; cursor:pointer; text-transform:uppercase; letter-spacing:2px; font-family: 'Zen Tokyo Zoo', cursive; transition: all 0.2s;";
        
        html += "<button onclick=\"location.href='/settings'\" style='" + btnStyle + "' onmouseover=\"this.style.borderColor='#d4af37'\" onmouseout=\"this.style.borderColor='#444'\">" + t_alarms + "</button>";
        html += "<button onclick=\"location.href='/phonebook'\" style='" + btnStyle + "' onmouseover=\"this.style.borderColor='#d4af37'\" onmouseout=\"this.style.borderColor='#444'\">" + t_pb + "</button>";
        html += "<button onclick=\"location.href='/advanced'\" style='" + btnStyle + "' onmouseover=\"this.style.borderColor='#d4af37'\" onmouseout=\"this.style.borderColor='#444'\">" + t_config + "</button>";
        html += "<button onclick=\"location.href='/help'\" style='" + btnStyle + "' onmouseover=\"this.style.borderColor='#d4af37'\" onmouseout=\"this.style.borderColor='#444'\">" + t_help + "</button>";
        
        html += "</div>";

        // Version Footer
        html += "<div style='text-align:center; padding-top:20px; color:#444; font-size:0.8rem;'>v0.7.1-beta</div>";
        html += "</body></html>";

        _server.send(200, "text/html", html);
    }
}

void WebManager::handleSettings() {
    resetApTimer();
    if (_apMode) { _server.sendHeader("Location", "/", true); _server.send(302, "text/plain", ""); return; }
    _server.send(200, "text/html", getSettingsHtml());
}

void WebManager::handleAdvanced() {
    resetApTimer();
    if (_apMode) { _server.sendHeader("Location", "/", true); _server.send(302, "text/plain", ""); return; }
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
             if(_server.hasArg("alm_t_"+s)) settings.setAlarmTone(i, _server.arg("alm_t_"+s).toInt());
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

    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#111;color:#d4af37;text-align:center;font-family:sans-serif;padding:30px;}</style></head><body>";
    if (wifiChanged) {
        html += "<h2>Settings Saved</h2>";
        html += "<p>The device is restarting to connect to your network.</p>";
        html += "<p>Please close this window, connect to your WiFi, and visit:</p>";
        html += "<h3><a href='http://dial-a-charmer.local' style='color:#fff;'>http://dial-a-charmer.local</a></h3>";
        html += "</body></html>";
        _server.send(200, "text/html", html);
        delay(2000); // Give time to send response
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

String WebManager::getSettingsHtml() {
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
    String t_audio_btn = isDe ? "Wecker" : "Alarms"; // "Wecker" requested for Dashboard/Footer
    String t_conf = isDe ? "Konfiguration" : "Configuration";
    String t_help = isDe ? "Hilfe" : "Help";
    
    // Additional Footer strings if needed to match requested "Wecker, Telefonbuch, Konfiguration, Hilfe"
    // The previous code had "Alarms & Audio" on the dashboard button, but footer was "Audio".
    // User wants consistency. "Wecker" (Alarms) seems to be the preferred term for the "Audio/Alarm" page now.

    String t_lang = isDe ? "Sprache" : "Language";
    String t_adv = isDe ? "Erweiterte Einstellungen" : "Advanced Settings";

    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += COMMON_CSS; // Use Shared Resource
    html += "<script>function sl(v){fetch('/save?lang='+v,{method:'POST'}).then(r=>location.reload());}</script>"; // Save Lang
    html += "</head><body>";
    html += "<h2>" + t_title + "</h2>";

    // Language Selector Removed

    html += "<form action='/save' method='POST'>";
    html += "<input type='hidden' name='form_id' value='basic'>";
    html += "<input type='hidden' name='redirect' value='/settings'>"; // Redirect back to settings

    // Repeating Alarm - Mobile Friendly Layout (Stacked)
    html += "<div class='card'><h3>" + String(isDe ? "T&auml;gliche Wecker" : "Daily Alarms") + "</h3>";
    
    // Using Flex/Grid logic with Divs instead of Table
    String dNamesDe[] = { "Mo", "Di", "Mi", "Do", "Fr", "Sa", "So" };
    String dNamesEn[] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
    
    for(int i=0; i<7; i++) {
        bool en = settings.isAlarmEnabled(i);
        String dayName = isDe ? dNamesDe[i] : dNamesEn[i];
        
        // Container for one alarm entry
        html += "<div style='border-bottom: 1px solid #333; padding: 15px 0;'>";
        
        // Row 1: Day (Left) & Active Switch (Right)
        html += "<div style='display:flex; justify-content:space-between; align-items:center; margin-bottom:10px;'>";
        // Day Label
        html += "<span style='color:#d4af37; font-weight:bold; font-size:1.4rem;'>" + dayName + "</span>";
        // Switch
        html += "<label class='switch'><input type='checkbox' name='alm_en_" + String(i) + "' value='1'" + (en?" checked":"") + "><span class='slider'></span></label>";
        html += "</div>";
        
        // Row 2: Time & Tone
        html += "<div style='display:flex; justify-content:space-between; align-items:center;'>";
        
        // Time Inputs
        html += "<div style='display:flex; align-items:center;'>";
        html += "<input type='number' name='alm_h_" + String(i) + "' min='0' max='23' value='" + String(settings.getAlarmHour(i)) + "' style='width:60px; text-align:center;'>";
        html += "<span style='font-size:1.5rem; margin:0 5px; color:#888;'>:</span>";
        html += "<input type='number' name='alm_m_" + String(i) + "' min='0' max='59' value='" + String(settings.getAlarmMinute(i)) + "' style='width:60px; text-align:center;'>";
        html += "</div>";
        
        // Tone Select
        html += "<div style='flex-grow:1; margin-left:15px;'>";
        html += "<select name='alm_t_" + String(i) + "' style='width:100%;'>";
        html += getSdFileOptions("/ringtones", settings.getAlarmTone(i));
        html += "</select>";
        html += "</div>";
        
        html += "</div>"; // End Row 2
        html += "</div>"; // End Entry
    }
    html += "</div>";

    // LED Moved to Advanced

    html += "<button type='submit' style='background-color:#8b0000; color:#f0e6d2; width:100%; border-radius:12px; padding:15px; font-size:1.5rem; letter-spacing:4px; margin-bottom:20px; font-family:\"Times New Roman\", serif; border:1px solid #a00000; cursor:pointer;'>" + String(isDe ? "SPEICHERN" : "SAVE") + "</button>";
    html += "</form>";
    
    html += "<div style='text-align:center; padding-bottom: 20px;'>";
    html += "<a href='/' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>Home</a>";
    // Wecker removed
    html += "<a href='/phonebook' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>Telefonbuch</a>";
    html += "<a href='/advanced' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>Konfiguration</a>";
    html += "<a href='/help' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>Hilfe</a>";
    html += "</div>";
    
    html += "</body></html>";
    return html;
}

String WebManager::getApSetupHtml() {
    // Basic Style for Setup
    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += AP_SETUP_CSS; // Use Shared Resource
    html += "</head><body>";
    
    html += "<h2>WiFi Setup</h2>";
    html += "<div class='card'>";
    html += "<form action='/save' method='POST'>";
    
    // Scan
    int n = WiFi.scanNetworks();
    html += "<label>Select Network</label>";
    html += "<input type='text' name='ssid' list='ssidList' placeholder='SSID' required>";
    html += "<datalist id='ssidList'>";
    for (int i = 0; i < n; ++i) {
        html += "<option value='" + WiFi.SSID(i) + "'>";
    }
    html += "</datalist>";
    
    html += "<label>Password</label>";
    html += "<input type='password' name='pass' placeholder='Password'>";
    
    html += "<button type='submit'>Save & Connect</button>";
    html += "</form>";
    html += "</div>";
    
    html += "<p>Enter credentials for your local network.<br>The device will restart.</p>";
    html += "<a href='/save?redirect=/'>(Skip / Reload)</a>";
    
    html += "</body></html>";
    return html;
}

// WebManager::getHtml() removed (Replaced by SPA index.html)

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
    String t_pb = isDe ? "Telefonbuch" : "Phonebook";
    String t_help = isDe ? "Hilfe" : "Help"; // "Usage Help" -> "Help"
    String t_audio_btn = isDe ? "Wecker" : "Alarms"; // New
    String t_conf = isDe ? "Konfiguration" : "Configuration"; // New
    
    // New Storage Strings
    String t_storage = isDe ? "Speicher Wartung" : "Storage Maintenance";
    String t_reindex = isDe ? "Index neu aufbauen" : "Reindex Storage";
    String t_reindex_desc = isDe ? "L&ouml;scht alle Playlists und scannt die SD-Karte erneut. (Neustart)" : "Clears all playlists and rescans the SD card. (Reboot)";

    // Scan for networks
    int n = WiFi.scanNetworks();
    String ssidOptions = "";
    for (int i = 0; i < n; ++i) {
        ssidOptions += "<option value='" + WiFi.SSID(i) + "'>";
    }

    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += COMMON_CSS; // Use Shared Resource
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
    html += "<div style='display:flex;align-items:center;margin-top:20px;'>";
    html += "<label class='switch' style='margin-right:15px;'><input type='checkbox' name='hd' value='1'" + String(settings.getHalfDuplex() ? " checked" : "") + "><span class='slider'></span></label>";
    html += "<span style='font-size:1.2rem;color:#888;text-transform:uppercase;letter-spacing:2px;'>" + t_hd + "</span>";
    html += "</div>";
    html += "</div>";

    html += "<div class='card'><h3>" + t_ai + "</h3>";
    html += "<label>" + t_key + "</label><input type='password' name='gemini' value='" + settings.getGeminiKey() + "'>";
    html += "<small>Leave empty to use SD card audio only.</small>";
    html += "</div>";
    
    html += "<button type='submit' style='background-color:#8b0000; color:#f0e6d2; width:100%; border-radius:12px; padding:15px; font-size:1.5rem; letter-spacing:4px; margin-bottom:20px; font-family:\"Times New Roman\", serif; border:1px solid #a00000; cursor:pointer;'>" + String(isDe ? "SPEICHERN" : "SAVE") + "</button>";
    html += "</form>";

    // --- STORAGE SECTION (Now Below Save) ---
    html += "<div class='card'><h3>" + t_storage + "</h3>";
    html += "<p>" + t_reindex_desc + " (1-3 Min)</p>";
    html += "<button type='button' onclick='if(confirm(\"Reindex?\")) { fetch(\"/api/reindex\").then(res => { alert(\"System Reindexing... LED will pulse blue. Wait for Ready Sound.\"); }); }' style='background-color:#cc4400;'>" + t_reindex + "</button>";
    html += "</div>";

    // OTA Update Form
    html += "<div class='card'><h3>Firmware Update (OTA)</h3>";
    html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
    html += "<input type='file' name='update' accept='.bin'>";
    html += "<button type='submit' style='background-color:#444; margin-top:10px;'>Start Update</button>";
    html += "</form></div>";
    
    html += "<div style='text-align:center; padding-bottom: 20px;'>";
    html += "<a href='/' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>Home</a>";
    html += "<a href='/settings' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>Wecker</a>";
    html += "<a href='/phonebook' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>Telefonbuch</a>";
    // Konfiguration removed
    html += "<a href='/help' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>Hilfe</a>";
    html += "</div>";
    
    html += "</body></html>";
    return html;
}

void WebManager::handlePhonebook() {
    resetApTimer();
    if (_apMode) { _server.sendHeader("Location", "/", true); _server.send(302, "text/plain", ""); return; }
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
    String lang = settings.getLanguage();
    bool isDe = (lang == "de");
    // Translations...
    String t_audio_btn = isDe ? "Wecker" : "Alarms";
    String t_pb = isDe ? "Telefonbuch" : "Phonebook";
    String t_conf = isDe ? "Konfiguration" : "Configuration";
    String t_help = isDe ? "Hilfe" : "Help";
    String t_title = isDe ? "Telefonbuch" : "Phonebook";
    String t_save_title = isDe ? "Speichern" : "Save Entries";

    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    
    // Gatsby Style (Global) included, but we override for "Lined Paper" mode
    html += COMMON_CSS; // Use Shared Resource 
    html += "<style>";
    
    // Global Body (reset to dark/gold) is default from htmlStyle
    html += "body { background-color: #080808; color: #f0e6d2; margin: 0; padding: 20px; }";

    // Notepad Container
    // Background: Off-white (#fffef0).
    // Gradient 1: Vertical Blue Line at 60px.
    // Gradient 2: Horizontal Red Lines every 40px.
    html += ".notepad { width: 100%; max-width: 550px; margin: 20px auto; min-height: 600px; padding: 40px 20px 40px 0px; box-sizing: border-box; box-shadow: 0 10px 30px rgba(0,0,0,0.8); }";
    html += ".notepad { background-color: #fffef0; background-image: linear-gradient(90deg, transparent 59px, #aaccff 59px, #aaccff 61px, transparent 61px), linear-gradient(#ffaaaa 1px, transparent 1px); background-size: 100% 100%, 100% 40px; background-attachment: local; }";

    // Table Styling - Align with lines
    html += ".pb-table { width: 100%; border-collapse: collapse; margin-top: 0px; }";
    html += ".pb-table tr { height: 40px; background: transparent; border: none; }";
    html += ".pb-table td { padding: 0 10px; border: none; border-bottom: 1px solid transparent; vertical-align: bottom; height: 40px; line-height: 38px; }";
    
    // Header Row on Paper
    html += ".pb-head th { height: 40px; vertical-align: bottom; color: #cc0000; font-family: sans-serif; font-size: 1.2rem; border:none; line-height:40px; }";

    // Input styling - Handwritten on the line
    html += "input { width: 100%; background: transparent; border: none; color: #000; padding: 0; font-family: 'Courier New', Courier, monospace; font-size: 1.5rem; font-weight: bold; text-align: left; outline: none; box-shadow: none; height: 100%; border-radius:0; }";
    html += "input::placeholder { color: #aaa; opacity: 0.5; }";
    html += "input:focus { background: rgba(255,255,0,0.1); }";
    
    // Name Cell - Handwritten
    html += ".name-cell { font-family: 'Brush Script MT', 'Bradley Hand', cursive; font-size: 1.3rem; color: #222; text-shadow: none; padding-left: 15px; line-height: 1.2; }";
    
    html += "</style>";

    html += R"rawliteral(
<script>
const systemItems = [
    { id:'p1', type:'FUNCTION', val:'COMPLIMENT_CAT', param:'1', defName:'Persona 1 (Trump)', defNum:'1' },
    { id:'p2', type:'FUNCTION', val:'COMPLIMENT_CAT', param:'2', defName:'Persona 2 (Badran)', defNum:'2' },
    { id:'p3', type:'FUNCTION', val:'COMPLIMENT_CAT', param:'3', defName:'Persona 3 (Yoda)', defNum:'3' },
    { id:'p4', type:'FUNCTION', val:'COMPLIMENT_CAT', param:'4', defName:'Persona 4 (Neutral)', defNum:'4' },
    { id:'p5', type:'FUNCTION', val:'COMPLIMENT_MIX', param:'0', defName:'Random Mix (Surprise)', defNum:'5' },
    { id:'menu', type:'FUNCTION', val:'VOICE_MENU', param:'', defName:'Operator Menu', defNum:'9' },
    { id:'tog', type:'FUNCTION', val:'TOGGLE_ALARMS', param:'', defName:'Toggle Alarms', defNum:'90' },
    { id:'skip', type:'FUNCTION', val:'SKIP_NEXT_ALARM', param:'', defName:'Skip Next Alarm', defNum:'91' }
];
let fullData = {};
async function load() { try { const res = await fetch('/api/phonebook'); fullData = await res.json(); render(); } catch(e) { console.error(e); } }
function render() {
    const tbody = document.getElementById('tbody');
    tbody.innerHTML = '';
    systemItems.forEach(item => {
        let currentKey = "";
        let currentName = item.defName;
        // Search for existing entry
        for (const [key, entry] of Object.entries(fullData)) {
             if (entry.type === item.type && entry.value === item.val && entry.parameter === item.param) {
                 currentKey = key;
                 if (entry.name && entry.name !== "Unknown") currentName = entry.name;
                 break;
             }
        }
        
        // Use simplified name for default Personas if key matches default
        // Actually, request says: "Persona 1" OR "Filename", not both.
        // currentName already holds the override if present, or defName if not.
        
        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td style="width: 140px; padding-left: 70px;">
                <input id="input_${item.id}" value="${currentKey}" placeholder="${item.defNum}">
            </td>
            <td class="name-cell">
                ${currentName}
            </td>`;
        tbody.appendChild(tr);
    });
}
async function save() {
    let newData = {};
    for (const [key, entry] of Object.entries(fullData)) {
        let isSystem = false;
        for (const item of systemItems) {
            if (entry.type === item.type && entry.value === item.val && entry.parameter === item.param) { isSystem = true; break; }
        }
        if (!isSystem) newData[key] = entry; 
    }
    systemItems.forEach(item => {
        const input = document.getElementById('input_' + item.id);
        const newKey = input.value.trim();
        if (newKey && newKey.length > 0) {
            let name = item.defName;
            // check if we have a renamed entry in fullData for this system item?
            // current render logic pulls name from fullData.
            // If we save, we want to preserve that name?
            // Currently UI does not allow renaming the Name, only the Key.
            // So we must preserve existing Name if found.
            for (const [k, e] of Object.entries(fullData)) {
                 if (e.type === item.type && e.value === item.val && e.parameter === item.param) { name = e.name; break; }
            }
            newData[newKey] = { name: name, type: item.type, value: item.val, parameter: item.param };
        }
    });
    await fetch('/api/phonebook', { method:'POST', body:JSON.stringify(newData) });
    alert(document.documentElement.lang === 'de' ? 'Gespeichert!' : 'Saved!');
    location.reload();
}
</script>
</head>
<body onload="load()">
)rawliteral";

    // Title ABOVE Paper
    html += "<h2 style='text-align:center; color:#d4af37; text-transform:uppercase; letter-spacing:4px; margin-bottom:10px;'>" + t_title + "</h2>";

    html += "<div class='notepad'>";
    
    // Table with Headers
    html += "<table class='pb-table'>";
    html += "<thead><tr class='pb-head'>";
    // Icons/Labels
    html += "<th style='width:140px; padding-left:70px; text-align:left; font-size:1.5rem;'>&#9742;</th>"; // Phone Icon
    html += "<th style='text-align:left; padding-left:15px;'>Name</th>";
    html += "</tr></thead>";
    
    html += "<tbody id='tbody'></tbody></table>";
    html += "</div>"; // End Notepad

    // Standard Footer (Button + Links)
    html += "<div style='max-width:550px; margin:0 auto;'>";
    html += "<button onclick='save()' style='background-color:#8b0000; color:#f0e6d2; width:100%; border-radius:12px; padding:15px; font-size:1.5rem; letter-spacing:4px; margin-bottom:20px; font-family:\"Times New Roman\", serif; border:1px solid #a00000; cursor:pointer;'>" + String(isDe ? "SPEICHERN" : "SAVE") + "</button>";
    html += "<div style='text-align:center; padding-bottom: 20px;'>";
    html += "<a href='/' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>Home</a>";
    html += "<a href='/settings' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>Wecker</a>";
    // Telefonbuch removed
    html += "<a href='/advanced' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>Konfiguration</a>";
    html += "<a href='/help' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>Hilfe</a>";
    html += "</div></div>";
    
    html += "</body></html>";
    return html;
}

String WebManager::getHelpHtml() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += COMMON_CSS; // Use Shared Resource
    
    // Add translation logic for Help Footer
    String lang = settings.getLanguage();
    bool isDe = (lang == "de");
    String t_audio_btn = isDe ? "Wecker" : "Alarms";
    String t_pb = isDe ? "Telefonbuch" : "Phonebook";
    String t_conf = isDe ? "Konfiguration" : "Configuration";
    // Help is current page
    
    html += "</head><body>";
    html += "<h2>User Manual</h2>";
    
    html += "<div class='card'><h3>1. Time & Alarm</h3>";
    html += "<ul><li><b>Receiver Down:</b> Dial a number (1-60) to set a Timer in minutes.</li>";
    html += "<li><b>Receiver Up:</b> Dial 0 to hear the current time.</li>";
    html += "<li><b>Stop Alarm:</b> Lift the receiver or tap the hook switch.</li>";
    html += "<li><b>Cancel Timer:</b> Lift the receiver (Voice Confirmed).</li>";
    html += "<li><b>Delete Manual Alarm:</b> Hold Button + Lift Receiver.</li></ul></div>";
    
    html += "<div class='card'><h3>2. Compliments (AI)</h3>";
    html += "<ul><li>Lift receiver and dial:</li>";
    html += "<li><b>0:</b> Random Surprise</li>";
    html += "<li><b>1:</b> Persona 1 (Trump)</li>";
    html += "<li><b>2:</b> Persona 2 (Badran)</li>";
    html += "<li><b>3:</b> Persona 3 (Yoda)</li>";
    html += "<li><b>4:</b> Persona 4 (Neutral)</li></ul></div>";
    
    html += "<div class='card'><h3>3. Settings</h3>";
    html += "<ul><li><b>Change Ringtone:</b> Hold the 'Extra Button' and dial 1-5.</li>";
    html += "<li><b>Web Config:</b> Connect to 'DialACharmer' WiFi.</li></ul></div>";
    
    // Updated Footer (consistent style, no self-link to Help)
    html += "<div style='text-align:center; padding-bottom: 20px; margin-top:20px;'>";
    html += "<a href='/' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>Home</a>";
    html += "<a href='/settings' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>" + t_audio_btn + "</a>";
    html += "<a href='/phonebook' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>" + t_pb + "</a>";
    html += "<a href='/advanced' style='color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1rem; letter-spacing:1px;'>" + t_conf + "</a>";
    html += "</div>";
    
    html += "</body></html>";
    return html;
}
