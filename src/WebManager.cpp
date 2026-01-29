#include "WebManager.h"
#include <esp_task_wdt.h>
#include "LedManager.h"
#include "PhonebookManager.h"
#include "WebResources.h" // Setup Styles
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <SD.h>
#include <FS.h>
#include <LittleFS.h>
#include <vector>
#include <algorithm>

// Extern from main.cpp
extern void playSound(String filename, bool useSpeaker);
extern SemaphoreHandle_t sdMutex; // SD card mutex

WebManager webManager;

// Cached SD file lists to avoid SD contention during web requests
static std::vector<String> cachedRingtones;
static std::vector<String> cachedSystem;

static void cacheSdFileList(const String& folder, std::vector<String>& out) {
    out.clear();
    esp_task_wdt_reset();
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        Serial.println("[Web] SD mutex timeout while caching" + folder);
        return;
    }
    File dir = SD.open(folder);
    if(!dir || !dir.isDirectory()) {
        Serial.println("[Web] Missing folder: " + folder);
        xSemaphoreGive(sdMutex);
        return;
    }
    size_t wdtTick = 0;
    File file = dir.openNextFile();
    while(file){
        if(!file.isDirectory()) {
            String name = file.name();
            if (!name.startsWith(".")) {
                out.push_back(name);
            }
        }
        if ((++wdtTick % 50) == 0) esp_task_wdt_reset();
        file = dir.openNextFile();
    }
    std::sort(out.begin(), out.end());
    xSemaphoreGive(sdMutex);
}

static String buildOptions(const std::vector<String>& files, int currentSelection) {
    String options;
    for(size_t i=0; i<files.size(); i++) {
        int id = static_cast<int>(i) + 1;
        String sel = (id == currentSelection) ? " selected" : "";
        String displayName = files[i];
        int dotIndex = displayName.lastIndexOf('.');
        if (dotIndex > 0) displayName = displayName.substring(0, dotIndex);
        options += "<option value='" + String(id) + "'" + sel + ">" + displayName + "</option>";
        if ((i % 50) == 0) esp_task_wdt_reset();
    }
    if (files.empty()) options = "<option>No files</option>";
    return options;
}

// Helper to list files for dropdown (uses cached lists, no SD access during request)
String getSdFileOptions(String folder, int currentSelection) {
    esp_task_wdt_reset();
    if (folder == Path::RINGTONES) return buildOptions(cachedRingtones, currentSelection);
    if (folder == Path::SYSTEM)    return buildOptions(cachedSystem, currentSelection);
    return "<option>Unknown folder</option>";
}

String getFooterHtml(bool isDe, String activePage) {
    String t_home = "Home";
    String t_alarms = isDe ? "Wecker" : "Alarms";
    String t_pb = isDe ? "Telefonbuch" : "Phonebook";
    String t_conf = isDe ? "Konfiguration" : "Configuration";
    String t_help = isDe ? "Hilfe" : "Manual";

    // Standardized Footer with consistent styling
    String html = "<div style='text-align:center; padding-bottom: 20px; margin-top: 20px; border-top: 1px solid #333; padding-top: 20px;'>";
    String style = "color:#ffc107; text-decoration:underline; margin:0 10px; font-size:1.2rem; letter-spacing:1px; font-weight:normal; font-family: 'Pompiere', cursive, sans-serif;";

    html += "<a href='/' style='" + style + "'>" + t_home + "</a>";
    
    if (activePage != "settings")
        html += "<a href='/settings' style='" + style + "'>" + t_alarms + "</a>";
        
    if (activePage != "phonebook")
        html += "<a href='/phonebook' style='" + style + "'>" + t_pb + "</a>";
        
    if (activePage != "advanced")
        html += "<a href='/advanced' style='" + style + "'>" + t_conf + "</a>";
        
    if (activePage != "help")
        html += "<a href='/help' style='" + style + "'>" + t_help + "</a>";

    html += "</div>";
    return html;
}

void WebManager::begin() {
    // Init FileSystem for Web Assets
    if(!LittleFS.begin(true)){
        Serial.println("LittleFS Mount Failed");
    }

    String ssid = settings.getWifiSSID();
    String pass = settings.getWifiPass();
    bool connected = false;

    // Try to connect if SSID is set
    if (ssid != "") {
        WiFi.mode(WIFI_STA);
        WiFi.setHostname("dial-a-charmer");
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
                MDNS.addService("http", "tcp", 80);
                _mdnsStarted = true;
                _mdnsLastAttempt = millis();
            } else {
                _mdnsStarted = false;
                _mdnsLastAttempt = millis();
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

    // Cache SD file lists once to avoid SD contention during web requests
    cacheSdFileList(Path::RINGTONES, cachedRingtones);
    cacheSdFileList(Path::SYSTEM,    cachedSystem);

    auto sendSdFile = [](AsyncWebServerRequest* request, const char* path, const char* contentType){
        esp_task_wdt_reset();
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            request->send(503, "text/plain", "SD busy");
            return;
        }
        if (!SD.exists(path)) {
            xSemaphoreGive(sdMutex);
            request->send(404, "text/plain", "Not Found");
            return;
        }
        AsyncWebServerResponse* response = request->beginResponse(SD, path, contentType);
        response->addHeader("Cache-Control", "max-age=604800");
        request->send(response);
        xSemaphoreGive(sdMutex);
    };

    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request){ handleRoot(request); }); 
    _server.on("/phonebook", HTTP_GET, [this](AsyncWebServerRequest* request){ handlePhonebook(request); });
    _server.on("/settings", HTTP_GET, [this](AsyncWebServerRequest* request){ handleSettings(request); }); 
    _server.on("/advanced", HTTP_GET, [this](AsyncWebServerRequest* request){ handleAdvanced(request); });
    _server.on("/api/phonebook", HTTP_GET, [this](AsyncWebServerRequest* request){ handlePhonebookGet(request); });
    _server.on("/api/phonebook", HTTP_POST,
        [this](AsyncWebServerRequest* request){ /* response sent in body handler */ },
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total){ handlePhonebookPost(request, data, len, index, total); }
    );
    _server.on("/api/preview", HTTP_GET, [this](AsyncWebServerRequest* request){ handlePreviewApi(request); });
    _server.on("/help", HTTP_GET, [this](AsyncWebServerRequest* request){ handleHelp(request); });

    // Reindex Storage
    _server.on("/api/reindex", HTTP_ANY, [this](AsyncWebServerRequest* request){
        Serial.println("WebCMD: Reindex requested.");
        _reindexTriggered = true; // Set Flag
        request->send(202, "text/plain", "ACCEPTED"); // Immediate Response
    });

    _server.on("/update", HTTP_POST,
        [this](AsyncWebServerRequest* request){
            AsyncWebServerResponse* response;
            if (Update.hasError()) {
                response = request->beginResponse(500, "text/plain", "FAIL");
            } else {
                response = request->beginResponse(200, "text/html", "<html><head><meta http-equiv='refresh' content='10;url=/'><style>body{background:#111;color:#d4af37;font-family:sans-serif;text-align:center;padding:50px;}</style></head><body><h2>Update Successful</h2><p>Rebooting... Please wait.</p></body></html>");
            }
            response->addHeader("Connection", "close");
            request->send(response);
            request->onDisconnect([](){ ESP.restart(); });
        },
        [this](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final){
            if (index == 0) {
                Serial.printf("Update: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            }
            if (!Update.hasError()) {
                if (Update.write(data, len) != len) {
                    Update.printError(Serial);
                }
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("Update Success: %uB\n", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );

    _server.on("/save", HTTP_POST, [this](AsyncWebServerRequest* request){ handleSave(request); });
    _server.onNotFound([this](AsyncWebServerRequest* request){ handleNotFound(request); });

    // --- FONTS FROM SD CARD (With Caching) ---
    _server.on("/fonts/ZenTokyoZoo-Regular.ttf", HTTP_GET, [sendSdFile](AsyncWebServerRequest* request){
        sendSdFile(request, Path::FONT_MAIN, "font/ttf");
    });

    _server.on("/fonts/Pompiere-Regular.ttf", HTTP_GET, [sendSdFile](AsyncWebServerRequest* request){
        sendSdFile(request, Path::FONT_SEC, "font/ttf");
    });

    _server.on("/api/files", HTTP_GET, [this](AsyncWebServerRequest* request){ handleFileListApi(request); });
    
    // Prevent 404 logs for favicon
    _server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(204);
    });

    // Serve Static Files (Last resort for assets like .css, .js)
    _server.serveStatic("/", LittleFS, "/");
    
    _server.begin();
    
    // Auto-off AP Timer
    _apEndTime = millis() + 600000; // 10 Minutes
}

void WebManager::loop() {
    esp_task_wdt_reset();
    if (_apMode) {
        _dnsServer.processNextRequest();
        
        // Timeout Check
        if (millis() > _apEndTime) {
             Serial.println("AP Timeout -> Stopping Access Point");
             stopAp();
        }
    }
    // Keep mDNS alive / recover after WiFi reconnects
    if (WiFi.status() == WL_CONNECTED) {
        if (!_mdnsStarted && (millis() - _mdnsLastAttempt > _mdnsRetryMs)) {
            _mdnsLastAttempt = millis();
            if (MDNS.begin("dial-a-charmer")) {
                Serial.println("mDNS responder restarted.");
                MDNS.addService("http", "tcp", 80);
                _mdnsStarted = true;
            } else {
                Serial.println("mDNS start failed, will retry later.");
            }
        }
    } else {
        if (_mdnsStarted) {
            MDNS.end();
        }
        _mdnsStarted = false;
    }
    
    // --- WORKER LOOP ---
    if (_reindexTriggered) {
        processReindex(); 
        _reindexTriggered = false; 
    }
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

void WebManager::handleRoot(AsyncWebServerRequest* request) {
    esp_task_wdt_reset();
    resetApTimer();
    if (_apMode) {
        request->send(200, "text/html", getApSetupHtml());
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
        html += "<script>function sl(v){fetch('/save?lang='+v,{method:'POST'}).then(r=>location.reload());}</script>";
        html += "</head><body>";
        
        html += "<h2>" + t_title + "</h2>";
        html += "<div style='text-align:center; margin-top:-15px; margin-bottom:15px; color:#888; font-family: \"Pompiere\", cursive; font-size:1.2rem;'>" + String(FIRMWARE_VERSION) + "</div>";
        
        html += "<div class='card' style='text-align:center;'>";
        
        // Language Toggle
        String activeStyle = "color:#d4af37; font-weight:bold; text-decoration:underline; cursor:default;";
        String inactiveStyle = "color:#666; text-decoration:none; cursor:pointer;";
    
        html += "<div style='margin-bottom:15px; font-size:1rem; letter-spacing:1px;'>";
        html += "<span onclick='sl(\"de\")' style='" + String(isDe ? activeStyle : inactiveStyle) + "'>DE</span>";
        html += " <span style='color:#444; margin:0 5px;'>|</span> ";
        html += "<span onclick='sl(\"en\")' style='" + String(!isDe ? activeStyle : inactiveStyle) + "'>EN</span>";
        html += "</div>";

        html += "<p style='color:#888; font-style:italic; margin-bottom:25px; font-family: \"Pompiere\", cursive; font-size:1.3rem;'>" + t_subtitle + "</p>";
        
        // Navigation Buttons
        String btnStyle = "background-color:#1a1a1a; color:#d4af37; width:100%; border-radius:8px; padding:18px; font-size:1.5rem; margin-bottom:15px; border:1px solid #444; cursor:pointer; text-transform:uppercase; letter-spacing:2px; font-family: 'Pompiere', cursive, sans-serif; transition: all 0.2s;";
        
        html += "<button onclick=\"location.href='/settings'\" style='" + btnStyle + "' onmouseover=\"this.style.borderColor='#d4af37'\" onmouseout=\"this.style.borderColor='#444'\">" + t_alarms + "</button>";
        html += "<button onclick=\"location.href='/phonebook'\" style='" + btnStyle + "' onmouseover=\"this.style.borderColor='#d4af37'\" onmouseout=\"this.style.borderColor='#444'\">" + t_pb + "</button>";
        html += "<button onclick=\"location.href='/advanced'\" style='" + btnStyle + "' onmouseover=\"this.style.borderColor='#d4af37'\" onmouseout=\"this.style.borderColor='#444'\">" + t_config + "</button>";
        html += "<button onclick=\"location.href='/help'\" style='" + btnStyle + "' onmouseover=\"this.style.borderColor='#d4af37'\" onmouseout=\"this.style.borderColor='#444'\">" + t_help + "</button>";
        
        html += "</div>";

        // Version Footer
        html += "<div style='text-align:center; padding-top:20px; color:#444; font-size:0.8rem;'>" + String(FIRMWARE_VERSION) + "</div>";
        html += "</body></html>";

        request->send(200, "text/html", html);
    }
}

void WebManager::handleSettings(AsyncWebServerRequest* request) {
    esp_task_wdt_reset();
    resetApTimer();
    if (_apMode) { request->redirect("/"); return; }
    request->send(200, "text/html", getSettingsHtml());
}

void WebManager::handleAdvanced(AsyncWebServerRequest* request) {
    esp_task_wdt_reset();
    resetApTimer();
    if (_apMode) { request->redirect("/"); return; }
    request->send(200, "text/html", getAdvancedHtml());
}

extern void playPreviewSound(String type, int index); // Defined in main.cpp

void WebManager::handlePreviewApi(AsyncWebServerRequest* request) {
    if (request->hasArg("type") && request->hasArg("id")) {
        String type = request->arg("type");
        int id = request->arg("id").toInt();
        resetApTimer();
        playPreviewSound(type, id);
        request->send(200, "text/plain", "OK");
    } else {
        request->send(400, "text/plain", "Missing Args");
    }
}

void WebManager::handleSave(AsyncWebServerRequest* request) {
    esp_task_wdt_reset();
    resetApTimer();
    bool wifiChanged = false;

    if (request->hasArg("ssid")) {
        String newSSID = request->arg("ssid");
        // Only update if changed
        if (newSSID != settings.getWifiSSID()) {
            settings.setWifiSSID(newSSID);
            wifiChanged = true;
        }
    }
    if (request->hasArg("pass")) {
        String newPass = request->arg("pass");
        if (newPass != settings.getWifiPass()) {
             settings.setWifiPass(newPass);
             wifiChanged = true;
        }
    }
    
    // System Settings
    if (request->hasArg("lang")) settings.setLanguage(request->arg("lang"));
    if (request->hasArg("tz")) settings.setTimezoneOffset(request->arg("tz").toInt());
    if (request->hasArg("gemini")) settings.setGeminiKey(request->arg("gemini"));
    
    // Audio Settings
    if (request->hasArg("vol")) settings.setVolume(request->arg("vol").toInt());
    if (request->hasArg("base_vol")) settings.setBaseVolume(request->arg("base_vol").toInt());
    if (request->hasArg("snooze")) settings.setSnoozeMinutes(request->arg("snooze").toInt()); // Added
    if (request->hasArg("ring")) settings.setRingtone(request->arg("ring").toInt());
    if (request->hasArg("dt")) settings.setDialTone(request->arg("dt").toInt());
    
    // Checkbox handling (Browser sends nothing if unchecked)
    // Only update if we are in the form that has this checkbox
    if (request->arg("form_id") == "advanced") {
        bool hd = request->hasArg("hd"); 
        settings.setHalfDuplex(hd);
    }
    
    if (request->arg("form_id") == "basic") {
        // Save 7 Alarms
        for(int i=0; i<7; i++) {
             String s = String(i);
             if(request->hasArg("alm_h_"+s)) settings.setAlarmHour(i, request->arg("alm_h_"+s).toInt());
             if(request->hasArg("alm_m_"+s)) settings.setAlarmMinute(i, request->arg("alm_m_"+s).toInt());
             if(request->hasArg("alm_t_"+s)) settings.setAlarmTone(i, request->arg("alm_t_"+s).toInt());
             settings.setAlarmEnabled(i, request->hasArg("alm_en_"+s));
        }
    }

    // LED Settings
    if (request->hasArg("led_day")) {
        int val = request->arg("led_day").toInt();
        settings.setLedDayBright(map(val, 0, 42, 0, 255));
    }
    if (request->hasArg("led_night")) {
        int val = request->arg("led_night").toInt();
        settings.setLedNightBright(map(val, 0, 42, 0, 255));
    }
    if (request->hasArg("night_start")) settings.setNightStartHour(request->arg("night_start").toInt());
    if (request->hasArg("night_end")) settings.setNightEndHour(request->arg("night_end").toInt());

    ledManager.reloadSettings(); // Apply new LED settings immediately

    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#111;color:#d4af37;text-align:center;font-family:sans-serif;padding:30px;}</style></head><body>";
    if (wifiChanged) {
        html += "<h2>Settings Saved</h2>";
        html += "<p>The device is restarting to connect to your network.</p>";
        html += "<p>Please close this window, connect to your WiFi, and visit:</p>";
        html += "<h3><a href='http://dial-a-charmer.local' style='color:#fff;'>http://dial-a-charmer.local</a></h3>";
        html += "</body></html>";
        request->send(200, "text/html", html);
        delay(2000); // Give time to send response
        ESP.restart();
    } else {
        // Redirect back
        String loc = "/";
        if (request->hasArg("redirect")) loc = request->arg("redirect");
        
        // Append ?saved=1
        if (loc.indexOf('?') == -1) loc += "?saved=1";
        else loc += "&saved=1";
        request->redirect(loc);
    }
}

void WebManager::handleHelp(AsyncWebServerRequest* request) {
    request->send(200, "text/html", getHelpHtml());
}

void WebManager::handleNotFound(AsyncWebServerRequest* request) {
    if (_apMode) {
        // Redirect to captive portal
        request->redirect("http://192.168.4.1/");
    } else {
        request->send(404, "text/plain", "Not Found");
    }
}

String WebManager::getSettingsHtml() {
    esp_task_wdt_reset();
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

    html += "<form action='/save' method='POST'>";
    html += "<input type='hidden' name='form_id' value='basic'>";
    html += "<input type='hidden' name='redirect' value='/settings'>"; // Redirect back to settings

    // Repeating Alarm - Mobile Friendly Layout (Stacked)
    html += "<div class='card'><h3>" + String(isDe ? "T&auml;gliche Wecker" : "Daily Alarms") + "</h3>";
    
    // Using Flex/Grid logic with Divs instead of Table
    String dNamesDe[] = { "Mo", "Di", "Mi", "Do", "Fr", "Sa", "So" };
    String dNamesEn[] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
    
    esp_task_wdt_reset();
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
        html += getSdFileOptions(Path::RINGTONES, settings.getAlarmTone(i));
        esp_task_wdt_reset();
        html += "</select>";
        html += "</div>";
        
        html += "</div>"; // End Row 2
        html += "</div>"; // End Entry
    }
    html += "</div>";

    html += "<button type='submit' style='background-color:#8b0000; color:#f0e6d2; width:100%; border-radius:12px; padding:15px; font-size:1.5rem; letter-spacing:4px; margin-bottom:20px; font-family:\"Times New Roman\", serif; border:1px solid #a00000; cursor:pointer;'>" + String(isDe ? "SPEICHERN" : "SAVE") + "</button>";
    html += "</form>";
    
    esp_task_wdt_reset();
    html += getFooterHtml(isDe, "settings");
    
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

String WebManager::getAdvancedHtml() {
    esp_task_wdt_reset();
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
    esp_task_wdt_reset();
    int n = WiFi.scanNetworks();
    String ssidOptions = "";
    for (int i = 0; i < n; ++i) {
        ssidOptions += "<option value='" + WiFi.SSID(i) + "'>";
    }

    esp_task_wdt_reset();
    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += COMMON_CSS; // Use Shared Resource
    html += "<script>function prev(t,i){fetch('/api/preview?type='+t+'&id='+i);}</script>";
    // Toast Notification Script
    html += "<script>window.onload = function() { if(new URLSearchParams(window.location.search).has('saved')) { var t = document.createElement('div'); t.innerHTML = 'Saved!'; t.style.cssText = 'position:fixed;bottom:20px;left:50%;transform:translateX(-50%);background:#4caf50;color:white;padding:12px 24px;border-radius:4px;box-shadow:0 2px 5px rgba(0,0,0,0.3);z-index:9999;font-size:1.2rem;animation:fadein 0.5s, fadeout 0.5s 2.5s forwards;'; document.body.appendChild(t); setTimeout(()=>{t.remove()}, 3000); }}; </script>";
    html += "<style>@keyframes fadein{from{opacity:0;transform:translate(-50%,20px);}to{opacity:1;transform:translate(-50%,0);}} @keyframes fadeout{from{opacity:1;transform:translate(-50%,0);}to{opacity:0;transform:translate(-50%,20px);}}</style>";
    html += "</head><body>";
    html += "<h2>" + t_title + "</h2>";
    html += "<form action='/save' method='POST'>";
    html += "<input type='hidden' name='redirect' value='/advanced'>";
    html += "<input type='hidden' name='form_id' value='advanced'>";
    
    // --- GROUP 1: SYSTEM & NETWORK ---
    html += "<div class='card'><h3>System / Network</h3>";
    // WiFi
    html += "<label>" + t_ssid + "</label>";
    html += "<input type='text' name='ssid' list='ssidList' value='" + settings.getWifiSSID() + "' placeholder='SSID'>";
    html += "<datalist id='ssidList'>" + ssidOptions + "</datalist>";
    html += "<label>" + t_pass + "</label><input type='password' name='pass' value='" + settings.getWifiPass() + "'>";
    // Timezone
    html += "<label style='margin-top:15px;'>" + t_tz + "</label>";
    html += "<select name='tz'>";
    int tz = settings.getTimezoneOffset();
    const char* labels[] = { "UTC -1 (Azores)", "UTC +0 (London)", "UTC +1 (Europe/Berlin)", "UTC +2 (Athens)", "UTC +3 (Istanbul)" };
    int offsets[] = { -1, 0, 1, 2, 3 };
    for(int i=0; i<5; i++) {
        html += "<option value='" + String(offsets[i]) + "'";
        if(tz == offsets[i]) html += " selected";
        html += ">" + String(labels[i]) + "</option>";
    }
    html += "</select>";

    // WiFi Signal Strength
    long rssi = WiFi.RSSI();
    int bars = 0;
    if (rssi > -55) bars = 5;
    else if (rssi > -65) bars = 4;
    else if (rssi > -75) bars = 3;
    else if (rssi > -85) bars = 2;
    else if (rssi > -95) bars = 1;
    
    html += "<div style='margin-top:20px; display:flex; align-items:flex-end; gap:10px;'>";
    html += "<div style='flex:1; color:#aaa; font-size:0.9rem;'>Signal: " + String(rssi) + " dBm</div>";
    html += "<div style='display:flex; gap:3px; align-items:flex-end;'>";
    for(int i=0; i<5; i++) {
        String color = (i < bars) ? "#d4af37" : "#333";
        int h = 12 + (i * 6); // 12, 18, 24, 30, 36
        html += "<div style='width:6px; height:" + String(h) + "px; background-color:" + color + "; border-radius:1px;'></div>";
    }
    html += "</div></div>";

    html += "</div>";

    // --- GROUP 2: AUDIO CONFIG ---
    esp_task_wdt_reset();
    html += "<div class='card'><h3>Audio Configuration</h3>";
    html += "<label>" + t_h_vol + " (0-42) <output>" + String(settings.getVolume()) + "</output></label>";
    html += "<input type='range' name='vol' min='0' max='42' value='" + String(settings.getVolume()) + "' oninput='this.previousElementSibling.firstElementChild.value = this.value'>";
    html += "<label>" + t_r_vol + " (0-42) <output>" + String(settings.getBaseVolume()) + "</output></label>";
    html += "<input type='range' name='base_vol' min='0' max='42' value='" + String(settings.getBaseVolume()) + "' oninput='this.previousElementSibling.firstElementChild.value = this.value'>";
    html += "<label>" + String(isDe ? "Snooze Dauer (Min)" : "Snooze Time (Min)") + " (0-20)</label>";
    html += "<input type='number' name='snooze' min='0' max='20' value='" + String(settings.getSnoozeMinutes()) + "'>";
    
    // Tones
    html += "<div style='display:flex; gap:10px; margin-top:15px;'>";
    html += "<div style='flex:1;'><label style='font-size:1rem;'>" + t_ring + "</label><select name='ring' onchange='prev(\"ring\",this.value)'>" + getSdFileOptions(Path::RINGTONES, settings.getRingtone()) + "</select></div>";
    esp_task_wdt_reset();
    html += "<div style='flex:1;'><label style='font-size:1rem;'>" + t_dt + "</label><select name='dt' onchange='prev(\"dt\",this.value)'>" + getSdFileOptions(Path::SYSTEM, settings.getDialTone()) + "</select></div>";
    esp_task_wdt_reset();
    html += "</div>";
    
    // AEC Switch
    html += "<div style='display:flex;align-items:center;margin-top:10px;padding-top:10px;border-top:1px solid #333;'>";
    html += "<label class='switch' style='margin-right:15px;margin-top:0;'><input type='checkbox' name='hd' value='1'" + String(settings.getHalfDuplex() ? " checked" : "") + "><span class='slider'></span></label>";
    html += "<span style='font-size:1rem;color:#aaa;'>" + t_hd + "</span>";
    html += "</div>";
    html += "</div>";

    // --- GROUP 3: VISUALS (LED) ---
    html += "<div class='card'><h3>" + t_led + "</h3>";
    int dayVal = map(settings.getLedDayBright(), 0, 255, 0, 42); 
    html += "<label>" + t_day + " (0-42)</label>";
    html += "<input type='range' name='led_day' min='0' max='42' value='" + String(dayVal) + "'>";
    int nightVal = map(settings.getLedNightBright(), 0, 255, 0, 42);
    html += "<label>" + t_night + " (0-42)</label>";
    html += "<input type='range' name='led_night' min='0' max='42' value='" + String(nightVal) + "'>";
    html += "<div style='display:flex; gap:10px; margin-top:10px;'>";
    html += "<div style='flex:1;'><label style='font-size:1rem;'>" + t_n_start + "</label><input type='number' name='night_start' min='0' max='23' value='" + String(settings.getNightStartHour()) + "'></div>";
    html += "<div style='flex:1;'><label style='font-size:1rem;'>" + t_n_end + "</label><input type='number' name='night_end' min='0' max='23' value='" + String(settings.getNightEndHour()) + "'></div>";
    html += "</div>";
    html += "</div>";

    // --- GROUP 4: INTELLIGENCE (AI) ---
    esp_task_wdt_reset();
    html += "<div class='card'><h3>" + t_ai + "</h3>";
    html += "<label>" + t_key + "</label><input type='password' name='gemini' value='" + settings.getGeminiKey() + "'>";
    html += "<small style='color:#666;'>Leave empty to disable AI features.</small>";
    html += "</div>";
    
    html += "<button type='submit' style='background-color:#8b0000; color:#f0e6d2; width:100%; border-radius:12px; padding:15px; font-size:1.5rem; letter-spacing:4px; margin-bottom:20px; font-family:\"Times New Roman\", serif; border:1px solid #a00000; cursor:pointer;'>" + String(isDe ? "SPEICHERN" : "SAVE") + "</button>";
    html += "</form>";

    // --- STORAGE SECTION (Now Below Save) ---
    html += "<div class='card'><h3>" + t_storage + "</h3>";
    html += "<p>" + t_reindex_desc + " (1-3 Min)</p>";
    html += "<button type='button' onclick='if(confirm(\"Reindex?\")) { fetch(\"/api/reindex\").then(res => { alert(\"System Reindexing... LED will pulse blue. Wait for Ready Sound.\"); }); }' style='background-color:#cc4400;'>" + t_reindex + "</button>";
    html += "</div>";

    // OTA Update Form
    esp_task_wdt_reset();
    html += "<div class='card'><h3>Firmware Update</h3>";
    html += "<p style='margin-bottom:15px;'><a href='https://github.com/TechwriterSSchmidt/Dial-A-Charmer/releases/latest/download/firmware.bin' target='_blank' style='color:#d4af37; text-decoration:underline;'>Download latest firmware.bin</a></p>";
    html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
    html += "<input type='file' name='update' accept='.bin'>";
    html += "<button type='submit' style='background-color:#444; margin-top:10px;'>Start Update</button>";
    html += "</form></div>";
    
    esp_task_wdt_reset();
    html += getFooterHtml(isDe, "advanced");
    
    html += "</body></html>";
    return html;
}

void WebManager::handlePhonebook(AsyncWebServerRequest* request) {
    esp_task_wdt_reset();
    resetApTimer();
    if (_apMode) { request->redirect("/"); return; }
    request->send(200, "text/html", getPhonebookHtml());
}

void WebManager::handlePhonebookGet(AsyncWebServerRequest* request) {
    // Return JSON
    if (!LittleFS.exists(Path::PHONEBOOK)) {
        request->send(500, "application/json", "{\"error\":\"File not found\"}");
        return;
    }
    AsyncWebServerResponse* response = request->beginResponse(LittleFS, Path::PHONEBOOK, "application/json");
    request->send(response);
}

void WebManager::handlePhonebookPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Accumulate body in _tempObject
    String* body = reinterpret_cast<String*>(request->_tempObject);
    if (!body) {
        body = new String();
        body->reserve(total);
        request->_tempObject = body;
    }
    body->concat(reinterpret_cast<const char*>(data), len);

    if (index + len < total) return; // wait for full body

    // Final chunk: parse and respond
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, *body);
    if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        delete body;
        request->_tempObject = nullptr;
        return;
    }
    phonebook.saveAll(doc.as<JsonObject>());
    request->send(200, "application/json", "{\"status\":\"saved\"}");
    delete body;
    request->_tempObject = nullptr;
}

String WebManager::getPhonebookHtml() {
    esp_task_wdt_reset();
    String lang = settings.getLanguage();
    bool isDe = (lang == "de");
    // Translations...
    String t_audio_btn = isDe ? "Wecker" : "Alarms";
    String t_pb = isDe ? "Telefonbuch" : "Phonebook";
    String t_conf = isDe ? "Konfiguration" : "Configuration";
    String t_help = isDe ? "Hilfe" : "Help";
    String t_title = isDe ? "Telefonbuch" : "Phonebook";
    String t_save_title = isDe ? "Speichern" : "Save Entries";

    esp_task_wdt_reset();
    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    
    // Gatsby Style (Global) included, but we override for "Lined Paper" mode
    html += COMMON_CSS; // Use Shared Resource 
    html += "<style>";
    
    // Global Body (reset to dark/gold) is default from htmlStyle
    html += "body { background-color: #080808; color: #f0e6d2; margin: 0; padding: 20px; }";

    // Notepad Container
    // Background: Off-white (#fffef0).
    // Gradient 1 (Holes): Black circles (matching body bg) on the left.
    // Gradient 2 (Blue Line): Vertical at 60px.
    // Gradient 3 (Red Lines): Horizontal every 40px.
    html += ".notepad { width: 100%; max-width: 550px; margin: 20px auto; min-height: 600px; padding: 40px 20px 40px 0px; box-sizing: border-box; box-shadow: 0 10px 30px rgba(0,0,0,0.8); }";
    html += ".notepad { border-top-right-radius: 25px; border-bottom-right-radius: 25px; }";
    html += ".notepad { background-color: #fffef0; background-image: radial-gradient(circle at 20px 140px, #080808 10px, transparent 11px), radial-gradient(circle at 20px 440px, #080808 10px, transparent 11px), linear-gradient(90deg, transparent 59px, #aaccff 59px, #aaccff 61px, transparent 61px), linear-gradient(#ffaaaa 1px, transparent 1px); background-size: 40px 100%, 40px 100%, 100% 100%, 100% 40px; background-attachment: local; background-repeat: no-repeat, no-repeat, no-repeat, repeat; }";

    // Table Styling - Align with lines
    html += ".pb-table { width: 100%; border-collapse: collapse; margin-top: 0px; }";
    html += ".pb-table tr { height: 40px; background: transparent; border: none; }";
    html += ".pb-table td { padding: 0 10px; border: none; border-bottom: 1px solid transparent; vertical-align: bottom; height: 40px; line-height: 38px; }";
    
    // Header Row on Paper
    html += ".pb-head th { height: 40px; vertical-align: bottom; color: #cc0000; font-family: sans-serif; font-size: 1.2rem; border:none; line-height:40px; }";

    // Input styling - Handwritten on the line. Use same font as Name
    html += "input { width: 100%; background: transparent; border: none; color: #000; padding: 0; font-family: 'Brush Script MT', 'Bradley Hand', cursive; font-size: 1.5rem; font-weight: bold; text-align: left; outline: none; box-shadow: none; height: 100%; border-radius:0; }";
    html += "input::placeholder { color: #aaa; opacity: 0.5; }";
    html += "input:focus { background: rgba(255,255,0,0.1); }";
    
    // Name Cell - Handwritten
    html += ".name-cell { font-family: 'Brush Script MT', 'Bradley Hand', cursive; font-size: 1.3rem; color: #222; text-shadow: none; padding-left: 10px; line-height: 1.2; }";
    
    html += "</style>";

    html += R"rawliteral(
<script>
const systemItems = [
    { id:'p1', type:'FUNCTION', val:'COMPLIMENT_CAT', param:'1', defName:'Persona 1 (Trump)', defNum:'1' },
    { id:'p2', type:'FUNCTION', val:'COMPLIMENT_CAT', param:'2', defName:'Persona 2 (Badran)', defNum:'2' },
    { id:'p3', type:'FUNCTION', val:'COMPLIMENT_CAT', param:'3', defName:'Persona 3 (Yoda)', defNum:'3' },
    { id:'p4', type:'FUNCTION', val:'COMPLIMENT_CAT', param:'4', defName:'Persona 4 (Neutral)', defNum:'4' },
    { id:'p5', type:'FUNCTION', val:'COMPLIMENT_CAT', param:'5', defName:'Persona 5 (Fortune)', defNum:'5' },
    { id:'p6', type:'FUNCTION', val:'COMPLIMENT_MIX', param:'0', defName:'Random Mix (Surprise)', defNum:'6' },
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
            // Standard Match
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
            <td style="width: 45px; padding-left: 65px;">
                <input id="input_${item.id}" value="${currentKey}" placeholder="${item.defNum}" maxlength="3" type="tel">
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
            let type = item.type;
            let val = item.val;
            let param = item.param;

            // Preserve existing config if present
            for (const [k, e] of Object.entries(fullData)) {
                if (e.type === item.type && e.value === item.val && e.parameter === item.param) { 
                    name = e.name; break; 
                }
            }
            newData[newKey] = { name: name, type: type, value: val, parameter: param };
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
    html += "<th style='width:45px; padding-left:65px; text-align:left; font-size:1.5rem;'>&#9742;</th>"; // Phone Icon
    html += "<th style='text-align:left; padding-left:10px;'>Name</th>";
    html += "</tr></thead>";
    
    html += "<tbody id='tbody'></tbody></table>";
    html += "</div>"; // End Notepad

    // Standard Footer (Button + Links)
    html += "<div style='max-width:550px; margin:0 auto;'>";
    html += "<button onclick='save()' style='background-color:#8b0000; color:#f0e6d2; width:100%; border-radius:12px; padding:15px; font-size:1.5rem; letter-spacing:4px; margin-bottom:20px; font-family:\"Times New Roman\", serif; border:1px solid #a00000; cursor:pointer;'>" + String(isDe ? "SPEICHERN" : "SAVE") + "</button>";
    
    esp_task_wdt_reset();
    html += getFooterHtml(isDe, "phonebook");

    html += "</div>";
    
    html += "</body></html>";
    return html;
}

String WebManager::getHelpHtml() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += COMMON_CSS; // Use Shared Resource
    
    // Translation Logic
    String lang = settings.getLanguage();
    bool isDe = (lang == "de");
    
    // UI Labels
    String t_audio_btn = isDe ? "Wecker" : "Alarms";
    String t_pb = isDe ? "Telefonbuch" : "Phonebook";
    String t_conf = isDe ? "Konfiguration" : "Configuration";
    String t_manual = isDe ? "Anleitung" : "User Manual";

    html += "</head><body>";
    html += "<h2>" + t_manual + "</h2>";
    
    // SECTION 1: BASIC USAGE
    html += "<div class='card'><h3>1. " + String(isDe ? "Bedienung" : "Basic Operation") + "</h3>";
    html += "<ul>";
    html += "<li><b>" + String(isDe ? "H&ouml;rer abnehmen:" : "Lift Receiver:") + "</b> " + String(isDe ? "W&auml;hlton erklingt." : "You hear a Dial Tone.") + "</li>";
    html += "<li><b>" + String(isDe ? "W&auml;hlen (0-9):" : "Dial (0-9):") + "</b> " + String(isDe ? "W&auml;hle eine Nummer f&uuml;r Inhalte." : "Input numbers to start content.") + "</li>";
    html += "<li><b>Timeout:</b> " + String(isDe ? "Nach 5 Sekunden ohne Eingabe ert&ouml;nt das 'Besetzt'-Zeichen. H&ouml;rer auflegen zum Reset." : "After 5 seconds of inactivity, a 'Busy Tone' plays. Hang up to reset.") + "</li>";
    html += "</ul></div>";

    // SECTION 2: CODES & SHORTCUTS (Dynamic based on logic, but defaults listed)
    html += "<div class='card'><h3>2. " + String(isDe ? "Nummern & Codes" : "Numbers & Codes") + "</h3>";
    html += "<ul>";
    html += "<li><b>0:</b> " + String(isDe ? "Gemini AI" : "Gemini AI") + "</li>";
    html += "<li><b>1 - 5:</b> " + String(isDe ? "Personas / Charaktere" : "Personas / Characters") + "</li>";
    html += "<li><b>6:</b> " + String(isDe ? "Zufalls-Mix (&Uuml;berraschung)" : "Random Surprise Mix") + "</li>";
    html += "<li><b>8:</b> " + String(isDe ? "System Status (IP-Adresse)" : "System Status (IP Address)") + "</li>";
    html += "<li><b>9:</b> " + String(isDe ? "Sprach-Men&uuml;" : "Voice Menu") + "</li>";
    html += "<li><b>90:</b> " + String(isDe ? "Alle Wecker: AN / AUS" : "Toggle All Alarms: ON / OFF") + "</li>";
    html += "<li><b>91:</b> " + String(isDe ? "N&auml;chsten Wecker &uuml;berspringen" : "Skip Next Routine Alarm") + "</li>";
    html += "</ul></div>";

    // SECTION 3: ALARMS & TIMER
    html += "<div class='card'><h3>3. " + String(isDe ? "Wecker & Timer" : "Alarms & Timer") + "</h3>";
    html += "<ul>";
    html += "<li><b>Timer (Kurzzeitmesser):</b> " + String(isDe ? "H&ouml;rer AUFGELEGT lassen -> Zahl w&auml;hlen (z.B. 5 = 5 Minuten)." : "Keep Receiver ON HOOK -> Dial Number (e.g. 5 = 5 Minutes).") + "</li>";
    html += "<li><b>" + String(isDe ? "Wecker stellen:" : "Set Alarm:") + "</b> " + String(isDe ? "Extra-Taste HALTEN + 4 Ziffern w&auml;hlen (z.B. 0730 = 07:30 Uhr)." : "HOLD Extra Button + Dial 4 digits (e.g. 0730 = 07:30).") + "</li>";
    html += "<li><b>Timer/Alarm beenden:</b> " + String(isDe ? "H&ouml;rer abnehmen und wieder auflegen." : "Lift receiver and hang up.") + "</li>";
    html += "<li><b>Schlummermodus:</b> " + String(isDe ? "H&ouml;rer abnehmen und daneben legen (nicht auflegen!)." : "Lift receiver and leave it off-hook / put aside.") + "</li>";
    html += "</ul></div>";
    
    html += getFooterHtml(isDe, "help");
    
    html += "</body></html>";
    return html;
}

void WebManager::processReindex() {
    Serial.println("Worker: Starting Reindex...");
    
    // Visual Feedback
    ledManager.setMode(LedManager::CONNECTING);
    ledManager.update();

    // 1. Delete Playlists (guarded)
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        if(SD.exists(Path::PLAYLISTS)) {
            File root = SD.open(Path::PLAYLISTS);
            if (root && root.isDirectory()) {
                 File file = root.openNextFile();
                 size_t wdtTick = 0;
                 while(file){
                    String path = String(Path::PLAYLISTS) + "/" + String(file.name());
                    if(!file.isDirectory()) {
                        SD.remove(path);
                        Serial.printf("Deleted: %s\n", path.c_str());
                    }
                    if ((++wdtTick % 10) == 0) {
                        esp_task_wdt_reset();
                        delay(0);
                    }
                    file = root.openNextFile();
                 }
                 root.close();
                 SD.rmdir(Path::PLAYLISTS);
            }
        }
        xSemaphoreGive(sdMutex);
    } else {
        Serial.println("Reindex skipped: SD mutex busy");
    }

    Serial.println("Reindex Logic Complete. Rebooting in 1s...");
    delay(1000);
    ESP.restart();
}

void WebManager::handleFileListApi(AsyncWebServerRequest* request) {
    if(!request->hasArg("path")) {
        request->send(400, "application/json", "{\"error\":\"Missing path argument\"}");
        return;
    }

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        request->send(503, "application/json", "{\"error\":\"SD busy\"}");
        return;
    }

    String path = request->arg("path");
    if(!path.startsWith("/")) path = "/" + path;

    if(!SD.exists(path)) {
         xSemaphoreGive(sdMutex);
         request->send(404, "application/json", "{\"error\":\"Directory not found\"}");
         return;
    }
    
    File dir = SD.open(path);
    if(!dir.isDirectory()){
         xSemaphoreGive(sdMutex);
         request->send(400, "application/json", "{\"error\":\"Path is not a directory\"}");
         return;
    }

    std::vector<String> fileList;
    File file = dir.openNextFile();
    size_t wdtTick = 0;
    while(file){
        String fileName = String(file.name());
        if(!fileName.startsWith(".")) { 
            if(!file.isDirectory()) {
                 fileList.push_back(fileName);
            }
        }
        if ((++wdtTick % 25) == 0) {
            esp_task_wdt_reset();
            delay(0);
        }
        file = dir.openNextFile();
    }
    
    std::sort(fileList.begin(), fileList.end());

    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    size_t jsonTick = 0;
    for(const auto& f : fileList) {
        arr.add(f);
        if ((++jsonTick % 50) == 0) {
            esp_task_wdt_reset();
            delay(0);
        }
    }

    String response;
    serializeJson(doc, response);
    xSemaphoreGive(sdMutex);
    request->send(200, "application/json", response);
}
