// Dial-A-Charmer Single Page Application using Vanilla JS
// Replicates the original SSR design but runs client-side.

const state = {
    lang: 'de',
    settings: {}, // will hold wifi, volume etc
    loading: true,
    phonebook: {},
    logLines: [],
    logTimer: null
};

// --- Debugging ---
function onInitStart() {
    const app = document.getElementById('app');
    if (app) app.innerHTML += "<br><small>Init JS...</small>";
}
// ---

const TEXT = {

    de: {
        title: "Dial-A-Charmer",
        alarm_title: "Wecker",
        subtitle: "Hörer abheben und Wählen",
        alarms: "Wecker",
        pb: "Telefonbuch",
        config: "Konfiguration",
        help: "Hilfe / Manual",
        home: "Home",
        save: "Speichern",
        active: "Aktiv", // New
        fade: "Ansteigend", // New
        msg: "Mit Spruch",
        snooze: "Schlummerzeit", // New
        alarm: "Wecker",
        datetime: "Datum & Zeit",
        timezone: "Zeitzone",
        tz_save: "Zone speichern",
        setup: "Einrichtung",
        name_header: "Name",
        loading_alarms: "Wecker werden geladen...",
        minutes_short: "min",
        wifi_network: "WLAN Netzwerk",
        ip_address: "IP-Adresse",
        scan: "Scannen",
        volume: "Lautstärke",
        base_speaker_timer: "Lautsprecher und Timer",
        handset: "Telefonhörer",
        timer_sound: "Timer-Ton",
        day_night_settings: "Tag- & Nachtmodus",
        led_enabled: "Signallampe Aktiv",
        night_base_volume: "Nachtlautstärke (Basis)",
        day_brightness: "Tagmodus Helligkeit",
        night_brightness: "Nachtmodus Helligkeit",
        day_start: "Tagmodus Start",
        night_start: "Nachtmodus Start",
        no_net: "Kein Netz",
        scanning_wifi: "WLAN wird gesucht...",
        please_wait: "Bitte warten",
        scan_error: "Scan Fehler",
        retry: "Erneut versuchen",
        connect_to: "Verbinde mit",
        password: "Passwort",
        connect: "Verbinden",
        cancel: "Abbrechen",
        saved: "Gespeichert!",
        connecting_to: "Das Gerät verbindet sich mit",
        ap_disappear: "Der Access Point verschwindet.",
        switch_back: "Bitte wechsle zurück zu",
        find_device_at: "Gerät erreichbar unter:",
        network_error: "Netzwerkfehler",
        check_console: "Konsole prüfen",
        select_network: "Bitte lokales Netzwerk auswählen:",
        saving_connecting: "Speichern und verbinden..."
    },
    en: {
        title: "Dial-A-Charmer",
        alarm_title: "Alarms",
        subtitle: "Lift receiver and dial",
        alarms: "Alarms",
        pb: "Phonebook",
        config: "Configuration",
        help: "Help / Manual",
        home: "Home",
        save: "Save",
        active: "Active", // New
        fade: "Rising Volume", // New
        msg: "With message",
        snooze: "Snooze Time", // New
        alarm: "Alarm",
        datetime: "Date & Time",
        timezone: "Timezone",
        tz_save: "Save Zone",
        setup: "Setup",
        name_header: "Name",
        loading_alarms: "Loading alarms...",
        minutes_short: "min",
        wifi_network: "WiFi Network",
        ip_address: "IP Address",
        scan: "Scan",
        volume: "Volume",
        base_speaker_timer: "Base Speaker and Timer",
        handset: "Handset",
        timer_sound: "Timer Sound",
        day_night_settings: "Day & Night Mode",
        led_enabled: "Signal Lamp Enabled",
        night_base_volume: "Night Base Volume",
        day_brightness: "Day Brightness",
        night_brightness: "Night Brightness",
        day_start: "Day Start",
        night_start: "Night Start",
        no_net: "No Net",
        scanning_wifi: "Scanning WiFi Networks...",
        please_wait: "Please wait",
        scan_error: "Scan Error",
        retry: "Retry",
        connect_to: "Connect to",
        password: "Password",
        connect: "Connect",
        cancel: "Cancel",
        saved: "Saved!",
        connecting_to: "The device is now connecting to",
        ap_disappear: "The Access Point will disappear.",
        switch_back: "Please switch your phone/PC back to",
        find_device_at: "Please find the device at:",
        network_error: "Network Error",
        check_console: "Check Console",
        select_network: "Select your local network:",
        saving_connecting: "Saving and Connecting..."
    }
};

const TIMEZONES = [
    { name: "Europe/Berlin", val: "CET-1CEST,M3.5.0,M10.5.0/3" },
    { name: "Europe/London", val: "GMT0BST,M3.5.0/1,M10.5.0" },
    { name: "Europe/Paris", val: "CET-1CEST,M3.5.0,M10.5.0/3" },
    { name: "US/Eastern (New York)", val: "EST5EDT,M3.2.0,M11.1.0" },
    { name: "US/Pacific (California)", val: "PST8PDT,M3.2.0,M11.1.0" },
    { name: "US/Central", val: "CST6CDT,M3.2.0,M11.1.0" },
    { name: "UTC", val: "UTC0" },
    { name: "Asia/Tokyo", val: "JST-9" },
    { name: "Australia/Sydney", val: "AEST-10AEDT,M10.1.0,M4.1.0/3" }
];

function renderHourOptions(selectedHour) {
    let opts = '';
    for (let h = 0; h < 24; h++) {
        const sel = (h === selectedHour) ? 'selected' : '';
        const label = `${String(h).padStart(2, '0')}:00`;
        opts += `<option value="${h}" ${sel}>${label}</option>`;
    }
    return opts;
}

const API = {
    getSettings: () => fetch('/api/settings').then(r => r.json()),
    saveSettings: (data) => fetch('/api/settings', { method: 'POST', body: JSON.stringify(data) }),
    scanWifi: () => fetch('/api/wifi/scan').then(r => r.json()),
    getRingtones: () => fetch('/api/ringtones').then(r => r.json()),
    getLogs: () => fetch('/api/logs').then(r => r.json()),
    getPhonebook: () => fetch('/api/phonebook').then(r => r.json()),
    getTime: () => fetch('/api/time').then(r => r.json())
};

// Update alarm clock time display
function updateAlarmTime() {
    const timeEl = document.getElementById('alarm-current-time');
    if (timeEl && state.page === '/alarm') {
        API.getTime()
            .then(data => {
                if (data.time) timeEl.textContent = data.time;
            })
            .catch(() => {});
    }
}

async function init() {
    onInitStart();
    
    // Start alarm time polling
    setInterval(updateAlarmTime, 1000);
    try {
        console.log("Fetching Settings...");

        // Check Status First (to see if we are in AP mode)
        const statusResp = await fetch('/api/status');
        const status = await statusResp.json();
        
        const [settings, ringtones] = await Promise.all([
            API.getSettings(),
            API.getRingtones()
        ]);
        
        state.settings = settings;
        state.ringtones = ringtones || [];
        state.lang = settings.lang || 'de';
        
        // Logic: Force Setup if AP mode OR no SSID config
        // Also check hostname (Captive Portal usually uses IP or .local)
        // If we connect to 192.168.4.1, we are definitely in AP provisioning mode.
        
        const isApMode = (status.mode === 'ap');
        const isCaptiveIP = (window.location.hostname === '192.168.4.1');
        
        if (isApMode || isCaptiveIP || !state.settings.wifi_ssid) {
             if (window.location.pathname !== '/setup') {
                 console.log("Redirecting to Setup (AP Mode or Missing Config)");
                 window.history.replaceState({}, "", "/setup");
             }
        }
        
        render();
    } catch (e) {
        console.error(e);
        document.getElementById('app').innerHTML = `<h1 style='color:red;'>${t('network_error')}</h1><p>${e.message}</p><p>${t('check_console')}</p>`;
    }
}

function t(key) {
    return TEXT[state.lang][key] || key;
}

function render() {
    const path = window.location.pathname;
    state.page = path;
    const app = document.getElementById('app');
    
    // Header Logic
    let pageTitle = "Dial-A-Charmer"; // Default Title
    if (path === '/alarm') pageTitle = t('alarm_title');
    else if (path === '/configuration') pageTitle = t('config');
    else if (path === '/phonebook') pageTitle = t('pb');
    else if (path === '/setup') pageTitle = t('setup');

    // Header / Title (Dynamic + Version)
    let html = `
        <div style="text-align:center;">
        <h2>${pageTitle}<br><span style="color:#888; font-family: 'Plaisir', serif; font-size: 0.7rem; display:block; margin-top:5px;">v1.9 RSA-Edition</span></h2>
        </div>
    `;

    // Routing
    if (path === '/' || path === '/index.html') {
        html += renderHome();
    } else if (path === '/phonebook') {
        html += renderPhonebook(); 
    } else if (path === '/alarm') {
        html += renderSettings();
    } else if (path === '/configuration') {
        html += renderAdvanced();
    } else if (path === '/setup') {
        html += renderSetup();
        // Trigger scan automatically if not done
        if (!state.wifiList && !state.scanning) {
            state.scanning = true;
            API.scanWifi().then(list => {
                state.wifiList = list;
                state.scanning = false;
                render();
            }).catch(e => {
                console.error(e);
                state.scanning = false;
                state.wifiError = e.message;
                render();
            });
        }
    } else {
        html += renderHome();
    }
    
    // Footer
    html += renderFooter(path);
    
    app.innerHTML = html;

    if (path === '/alarm') {
        updateAlarmTime();
    }

    if (path === '/configuration') {
        startLogPolling();
    } else {
        stopLogPolling();
    }
    
    // Post-render Logic (Listeners)
    if (path === '/phonebook' && state.phonebook && Object.keys(state.phonebook).length === 0) {
        API.getPhonebook().then(pb => {
            state.phonebook = pb || {};
            render(); // Re-render with data
        });
    }
}

function updateCrt(lines) {
    const crt = document.getElementById('crt-log');
    if (!crt) return;
    crt.textContent = (lines && lines.length) ? lines.join('\n') : 'READY>_';
}

function startLogPolling() {
    if (state.logTimer) return;
    const fetchLogs = () => {
        API.getLogs().then(data => {
            if (!data || !Array.isArray(data.lines)) return;
            state.logLines = data.lines.slice(-10);
            updateCrt(state.logLines);
        }).catch(() => {
            updateCrt(state.logLines);
        });
    };
    fetchLogs();
    state.logTimer = setInterval(fetchLogs, 1500);
}

function logLampSettings(label, patch) {
    const parts = Object.keys(patch).map(key => `${key}=${patch[key]}`);
    const line = `LAMP ${label}: ${parts.join(', ')}`;
    state.logLines = [...state.logLines.slice(-9), line];
    updateCrt(state.logLines);
}

function stopLogPolling() {
    if (!state.logTimer) return;
    clearInterval(state.logTimer);
    state.logTimer = null;
}

function renderHome() {
    const activeStyle = "color:#d4af37; font-weight:bold; text-decoration:underline; cursor:default;";
    const inactiveStyle = "color:#666; font-weight:bold; text-decoration:none; cursor:pointer;";
    const isDe = state.lang === 'de';
    
    return `
    <div class="card" style="text-align:center;">
        <div style="margin-bottom:15px; font-size:1rem; letter-spacing:1px;">
            <span onclick="setLang('de')" style="${isDe ? activeStyle : inactiveStyle}">DE</span>
            <span style="color:#444; margin:0 5px;">|</span>
            <span onclick="setLang('en')" style="${!isDe ? activeStyle : inactiveStyle}">EN</span>
        </div>
        <p style="color:#d4af37; font-style:italic; margin-bottom:25px; font-family: 'Plaisir', serif; font-size:1.0rem;">
            ${t('subtitle')}
        </p>
        
        ${renderBtn(t('alarms'), '/alarm')}
        ${renderBtn(t('pb'), '/phonebook')}
        ${renderBtn(t('config'), '/configuration')}
        
    </div>
    `;
}

function renderBtn(label, href) {
    const style = "background-color:#1a1a1a; color:#d4af37; width:100%; border-radius:8px; padding:18px; font-size:1.5rem; margin-bottom:15px; border:1px solid #444; cursor:pointer; text-transform:uppercase; letter-spacing:2px; font-family: 'Plaisir', serif, sans-serif; transition: all 0.2s;";
    return `<button onclick="nav('${href}')" style="${style}" onmouseover="this.style.borderColor='#d4af37'" onmouseout="this.style.borderColor='#444'">${label}</button>`;
}

function renderFooter(activePage) {
    const links = [
        {k: 'home', h: '/'},
        {k: 'alarms', h: '/alarm'},
        {k: 'pb', h: '/phonebook'},
        {k: 'config', h: '/configuration'}
    ];
    
    const style = "color:#ffc107; text-decoration:underline; margin:0 6px; font-size:0.9rem; letter-spacing:0.5px; font-weight:normal; font-family: 'Plaisir', serif, sans-serif;";
    
    let html = "<div style='text-align:center; padding-bottom: 20px; margin-top: 20px; border-top: 1px solid #333; padding-top: 20px;'>";
    links.forEach(l => {
        if (l.h !== activePage) {
            html += `<a href="javascript:void(0)" onclick="return nav('${l.h}')" style="${style}">${t(l.k)}</a>`;
        }
    });
    html += "</div>";
    return html;
}

// Helpers
window.setLang = (l) => {
    state.lang = l;
    state.settings.lang = l;
    API.saveSettings({lang: l}).then(() => {
        return API.getPhonebook().then(pb => {
            state.phonebook = pb || {};
            render();
        });
    });
};

window.nav = (h) => {
    window.history.pushState({}, "", h);
    render();
    return false;
};

window.onpopstate = render;

// --- SPECIFIC PAGES ---
function renderPhonebook() {
    const isDe = state.lang === 'de';
    const fullData = state.phonebook || {};

    const systemItems = [
        { id: 'p1', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '1', defName: 'Persona 1 (Default)', defNum: '1' },
        { id: 'p2', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '2', defName: 'Persona 2 (Joke)', defNum: '2' },
        { id: 'p3', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '3', defName: 'Persona 3 (SciFi)', defNum: '3' },
        { id: 'p4', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '4', defName: 'Persona 4 (Captain)', defNum: '4' },
        { id: 'p5', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '5', defName: 'Persona 5', defNum: '5' },
        { id: 'p6', type: 'FUNCTION', val: 'COMPLIMENT_MIX', param: '0', defName: 'Random Mix (Surprise)', defNum: '11' },

        { id: 'time', type: 'FUNCTION', val: 'ANNOUNCE_TIME', param: '', defName: isDe ? 'Zeitauskunft' : 'Time Announcement', defNum: '110' },
        { id: 'menu', type: 'FUNCTION', val: 'VOICE_MENU', param: '', defName: isDe ? 'Sprachmenue' : 'Voice Admin Menu', defNum: '0' },
        { id: 'reboot', type: 'FUNCTION', val: 'REBOOT', param: '', defName: isDe ? 'System Neustart' : 'System Reboot', defNum: '999' }
    ];

    let rows = "";
    systemItems.forEach(item => {
        let currentKey = "";
        let currentName = item.defName;

        for (const [key, entry] of Object.entries(fullData)) {
            const entryParam = entry.parameter || "";
            const itemParam = item.param || "";
            if (entry.type === item.type && entry.value === item.val && entryParam === itemParam) {
                currentKey = key;
                if (entry.name && entry.name !== "Unknown") currentName = entry.name;
                break;
            }
        }

        const criticalClass = item.defNum === '999' ? 'pb-critical' : '';

        const displayKey = currentKey || item.defNum;
        rows += `
            <tr class="${criticalClass}">
                <td class="pb-num-cell">
                    <div class="pb-num-text">${displayKey}</div>
                </td>
                <td class="pb-name-cell">${currentName}</td>
            </tr>
        `;

        if (item.id === 'menu') {
            const sub = isDe
                ? [
                    ['1', 'Nächster Wecker'],
                    ['2', 'Nachtmodus'],
                    ['3', 'Telefonbuch ansagen'],
                    ['4', 'Systemstatus']
                ]
                : [
                    ['1', 'Next Alarm'],
                    ['2', 'Night Mode'],
                    ['3', 'Phonebook Export'],
                    ['4', 'System Check']
                ];

            sub.forEach(([num, text]) => {
                rows += `
                    <tr class="pb-sub">
                        <td class="pb-num-cell">${num}</td>
                        <td class="pb-name-cell pb-sub-text">${text}</td>
                    </tr>
                `;
            });
        }
    });

    return `
        <div class="pb-wrapper">
            <div class="pb-notepad">
                <table class="pb-table">
                    <thead>
                        <tr class="pb-head">
                            <th class="pb-head-num"><span class="pb-phone-icon">&#128222;</span></th>
                            <th class="pb-head-name">${t('name_header')}</th>
                        </tr>
                    </thead>
                    <tbody>
                        ${rows}
                    </tbody>
                </table>
            </div>
        </div>
    `;
}


function renderSettings() {
    const DAYS_DE = ["Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"];
    const DAYS_EN = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"];
    
    const isDe = state.lang === 'de';
    const days = isDe ? DAYS_DE : DAYS_EN;
    const dayFallbackPrefix = isDe ? 'Tag' : 'Day';
    // Ensure alarms exist (backend should send them, but safe fallback)
    const alarms = state.settings.alarms || []; 
    
    // Display order: Mon(1) ... Sun(0)
    const displayOrder = [1, 2, 3, 4, 5, 6, 0];
    const ringtones = state.ringtones || [];

    let rows = "";
    if (alarms.length === 0) {
        rows = `<p style='text-align:center;'>${t('loading_alarms')}</p>`;
    } else {
        displayOrder.forEach(dayIndex => {
            const a = alarms.find(x => x.d === dayIndex);
            if (!a) return;

            const dayFallbackNum = (a.d === 0) ? 7 : a.d;
            const dayName = days[a.d] || (dayFallbackPrefix + " " + dayFallbackNum);
            const hh = String(a.h).padStart(2,'0');
            const mm = String(a.m).padStart(2,'0');
            const timeVal = `${hh}:${mm}`;
            const checked = a.en ? "checked" : "";
            const rampChecked = a.rmp ? "checked" : ""; // Rising volume
            const msgChecked = a.msg ? "checked" : ""; // With message
            const currentSound = a.snd || "digital_alarm.wav";
            
            let soundOpts = "";
            ringtones.forEach(r => {
                const sel = (r === currentSound) ? "selected" : "";
                soundOpts += `<option value="${r}" ${sel}>${r}</option>`;
            });
            if (ringtones.length === 0) soundOpts = `<option>${currentSound}</option>`;

            rows += `
            <div class="alarm-card" style="flex-direction: column; align-items: flex-start;">
                <!-- Row 1: Day Name -->
                <div style="width:100%; margin-bottom: 8px; border-bottom: 1px solid #444; padding-bottom: 4px;">
                    <div class="alarm-day" style="font-family: 'Plaisir', serif;">${dayName}</div>
                </div>

                <!-- Row 2: Active (Left) - Time (Right) -->
                <div style="display:flex; justify-content:space-between; width:100%; align-items:center; margin-bottom: 3px;">
                    <div style="display:flex; align-items:center;">
                        <label class="switch" title="${t('active')}">
                            <input type="checkbox" id="check-${a.d}" ${checked}>
                            <span class="slider"></span>
                        </label>
                        <span class="alarm-label">${t('active')}</span>
                    </div>
                    <input type="time" value="${timeVal}" id="time-${a.d}" class="alarm-time-input">
                </div>
                
                <!-- Row 3: Fade (Left) - Sound (Right) -->
                <div style="width: 100%; display: flex; justify-content: space-between; align-items: center;">
                    <div style="display:flex; align-items:center;">
                        <!-- Scaled removed to match size -->
                        <label class="switch" title="${t('fade')}">
                            <input type="checkbox" id="ramp-${a.d}" ${rampChecked}>
                            <span class="slider"></span>
                        </label>
                        <span class="alarm-label">${t('fade')}</span>
                    </div>
                    
                    <select id="snd-${a.d}" onchange="previewTone(this.value)" class="alarm-sound-select">
                        ${soundOpts}
                    </select>
                </div>

                <!-- Row 4: With Message -->
                <div style="width: 100%; display: flex; justify-content: space-between; align-items: center; margin-top: 3px;">
                     <div style="display:flex; align-items:center;">
                        <label class="switch" title="${t('msg')}">
                            <input type="checkbox" id="msg-${a.d}" ${msgChecked}>
                            <span class="slider"></span>
                        </label>
                        <span class="alarm-label">${t('msg')}</span>
                    </div>
                </div>

            </div>
            `;
        });

        // Snooze Selection
        const currentSnooze = state.settings.snooze_min || 5;
        let snoozeOpts = "";
        for (let i=1; i<=20; i++) {
            const sel = (i === currentSnooze) ? "selected" : "";
            snoozeOpts += `<option value="${i}" ${sel}>${i} ${t('minutes_short')}</option>`;
        }

        rows += `
        <div style="margin-top:20px; text-align:center; padding-top:10px; border-top:1px solid #444;">
            <label style="color:#d4af37; font-family: 'Plaisir', serif; margin-right:10px;">${t('snooze')}:</label>
            <select id="snooze-time" style="padding:5px; border-radius:4px; border:1px solid #666; background-color:#222; color:#f0e6d2; font-weight:bold; text-align:center; text-align-last:center;">
                ${snoozeOpts}
            </select>
        </div>
        <div style="text-align:center; margin-top:1rem;"><button onclick="saveAlarms()">${t('save')}</button></div>
        `;
    }

    return `
        <div class="card">
            <div class="alarm-header">
                <div class="alarm-current-time" id="alarm-current-time">--:--:--</div>
            </div>
            ${rows}
        </div>
    `;
}

const PREVIEW_DEBOUNCE_MS = 400;
let previewDebounceTimer = null;
let previewPendingFile = "";

window.previewTone = (filename) => {
    if (!filename) return;

    previewPendingFile = filename;
    if (previewDebounceTimer) {
        clearTimeout(previewDebounceTimer);
    }

    previewDebounceTimer = setTimeout(() => {
        const file = previewPendingFile;
        previewPendingFile = "";
        previewDebounceTimer = null;

        // Play on device via API
        fetch(`/api/preview?file=${encodeURIComponent(file)}`)
            .catch(e => console.log("Preview request failed: " + e));
    }, PREVIEW_DEBOUNCE_MS);
};

window.saveAlarms = () => {
    const newAlarms = [];
    for(let i=0; i<7; i++) {
        const timeInput = document.getElementById(`time-${i}`);
        const checkInput = document.getElementById(`check-${i}`);
        const rampInput = document.getElementById(`ramp-${i}`);
        const msgInput = document.getElementById(`msg-${i}`);
        const sndInput = document.getElementById(`snd-${i}`);
        
        if(timeInput && checkInput) {
            const timeStr = timeInput.value; // "HH:MM"
            const en = checkInput.checked;
            const rmp = rampInput ? rampInput.checked : false;
            const msg = msgInput ? msgInput.checked : false;
            const snd = sndInput ? sndInput.value : "digital_alarm.wav";
            const [h, m] = timeStr.split(':').map(Number);
            newAlarms.push({d: i, h: h, m: m, en: en, rmp: rmp, msg: msg, snd: snd});
        }
    }
    
    const snoozeInput = document.getElementById('snooze-time');
    const snoozeVal = snoozeInput ? parseInt(snoozeInput.value) : 5;

    // Optimistic update
    state.settings.alarms = newAlarms;
    state.settings.snooze_min = snoozeVal;
    
    document.getElementById('app').innerHTML = `<div style="text-align:center; padding:50px; color:#d4af37;"><h3>${t('save')}...</h3></div>`;
    
    API.saveSettings({alarms: newAlarms, snooze_min: snoozeVal}).then(() => {
        render(); // Redraw
    });
}

function renderAdvanced() {
     const currentTime = state.settings.current_time || "--";
     const currentTz = state.settings.timezone || "";
    const ledEnabled = (state.settings.led_enabled === undefined) ? true : !!state.settings.led_enabled;
    const ledDayPct = (state.settings.led_day_pct === undefined) ? 100 : state.settings.led_day_pct;
    const ledNightPct = (state.settings.led_night_pct === undefined) ? 10 : state.settings.led_night_pct;
    const ledDayStart = (state.settings.led_day_start === undefined) ? 7 : state.settings.led_day_start;
    const ledNightStart = (state.settings.led_night_start === undefined) ? 22 : state.settings.led_night_start;
    const nightBaseVol = (state.settings.night_base_volume === undefined) ? 50 : state.settings.night_base_volume;
     
     let tzOptions = "";
     TIMEZONES.forEach(tz => {
         const sel = (tz.val === currentTz) ? "selected" : "";
         tzOptions += `<option value="${tz.val}" ${sel}>${tz.name}</option>`;
     });

     return `
        <div class="card">

            <!-- DATE & TIME PANEL -->
            <div style="background:#222; padding:15px; border-radius:8px; margin-bottom:15px; border:1px solid #444;">
                <h4 style="margin-top:0; color:#d4af37; font-size:0.9rem; text-transform:uppercase; border-bottom:1px solid #444; padding-bottom:5px;">${t('datetime')}</h4>

                <div style="display:flex; justify-content:space-between; align-items:center; margin-top:10px;">
                    <div style="color:#d4af37; font-size:1.1rem; font-family:'Plaisir', serif;">${currentTime}</div>
                    
                    <div style="text-align:right;">
                        <select id="tz-select" style="padding:6px; width:140px; background:#111; color:#fff; border:1px solid #555; border-radius:4px; font-size:0.8rem;" onchange="saveTimezone(this.value)">
                            ${tzOptions}
                        </select>
                    </div>
                </div>
            </div>

            <!-- WIFI PANEL -->
              <div style="background:#222; padding:12px; border-radius:8px; margin-bottom:15px; border:1px solid #444;">
                 <h4 style="margin-top:0; color:#d4af37; font-size:0.9rem; text-transform:uppercase; border-bottom:1px solid #444; padding-bottom:5px;">${t('wifi_network')}</h4>

                        <div style="display:flex; flex-direction:column; gap:6px;">
                            <div class="wifi-ssid-text" style="margin:4px 0 0;">${state.settings.wifi_ssid || t('no_net')} <span class="wifi-ip-text">(${state.settings.ip || '--'})</span></div>
                            <button onclick="nav('/setup')" class="wifi-scan-btn" style="margin-top:0;">${t('scan')}</button>
                        </div>
            </div>

            <!-- CONSOLE PANEL -->
            <div class="crt-panel">
                <div class="crt-screen">
                    <div class="crt-text" id="crt-log">READY&gt;_</div>
                </div>
            </div>

            <!-- VOLUME PANEL -->
            <div style="background:#222; padding:10px; border-radius:8px; margin-bottom:15px; border:1px solid #444;">
                <h4 style="margin-top:0; color:#d4af37; font-size:0.9rem; text-transform:uppercase; border-bottom:1px solid #444; padding-bottom:5px;">${t('volume')}</h4>
                
                <!-- Base Speaker -->
                <div style="margin-top:6px;">
                    <div style="display:flex; justify-content:space-between; margin-bottom:3px;">
                        <label style="font-size:0.8rem; color:#aaa; text-transform:uppercase;">${t('base_speaker_timer')}</label>
                        <span id="vol-disp-base" style="color:#d4af37; font-weight:bold;">${state.settings.volume || 60}%</span>
                    </div>
                    <input type="range" min="0" max="100" value="${state.settings.volume || 60}" 
                           oninput="document.getElementById('vol-disp-base').innerText=this.value+'%'"
                           onchange="saveVol('base', this.value)" style="width:100%"/>
                </div>

                <!-- Handset Speaker -->
                <div style="margin-top:8px;">
                     <div style="display:flex; justify-content:space-between; margin-bottom:3px;">
                        <label style="font-size:0.8rem; color:#aaa; text-transform:uppercase;">${t('handset')}</label>
                        <span id="vol-disp-handset" style="color:#d4af37; font-weight:bold;">${state.settings.volume_handset}%</span>
                    </div>
                    <input type="range" min="0" max="100" value="${state.settings.volume_handset}" 
                           oninput="document.getElementById('vol-disp-handset').innerText=this.value+'%'"
                           onchange="saveVol('handset', this.value)" style="width:100%"/>
                </div>
                <!-- Alarm Volume -->
                <div style="margin-top:8px;">
                     <div style="display:flex; justify-content:space-between; margin-bottom:3px;">
                        <label style="font-size:0.8rem; color:#aaa; text-transform:uppercase;">${t('alarm')} (Min ${state.settings.vol_alarm_min}%)</label>
                        <span id="vol-disp-alarm" style="color:#d4af37; font-weight:bold;">${state.settings.vol_alarm || 90}%</span>
                    </div>
                    <input type="range" min="${state.settings.vol_alarm_min || 60}" max="100" value="${state.settings.vol_alarm || 90}" 
                           oninput="document.getElementById('vol-disp-alarm').innerText=this.value+'%'"
                           onchange="saveVol('alarm', this.value)" style="width:100%"/>
                </div>
            </div>

            <!-- TIMER RINGTONE PANEL -->
            <div style="background:#222; padding:15px; border-radius:8px; margin-bottom:15px; border:1px solid #444;">
                <h4 style="margin-top:0; color:#d4af37; font-size:0.9rem; text-transform:uppercase; border-bottom:1px solid #444; padding-bottom:5px;">${t('timer_sound')}</h4>
                
                <div style="margin-top:12px;">
                    <select id="timer-ringtone-select" onchange="saveTimerRingtone(this.value); previewTone(this.value);" style="width:100%; padding:8px; background:#111; color:#fff; border:1px solid #555; border-radius:4px; font-size:0.9rem;">
                        ${(() => {
                            const ringtones = state.ringtones || [];
                            const current = state.settings.timer_ringtone;
                            let opts = '';
                            ringtones.forEach(r => {
                                const sel = (r === current) ? 'selected' : '';
                                opts += `<option value="${r}" ${sel}>${r}</option>`;
                            });
                            if (ringtones.length === 0) opts = `<option>${current}</option>`;
                            return opts;
                        })()}
                    </select>
                </div>
            </div>

            <!-- SIGNAL LAMP PANEL -->
            <div style="background:#222; padding:15px; border-radius:8px; margin-bottom:15px; border:1px solid #444;">
                <h4 style="margin-top:0; color:#d4af37; font-size:0.9rem; text-transform:uppercase; border-bottom:1px solid #444; padding-bottom:5px;">${t('day_night_settings')}</h4>

                <div style="display:flex; align-items:center; gap:10px; margin:6px 0 12px;">
                    <label class="switch" title="${t('led_enabled')}">
                        <input type="checkbox" id="led-enabled" ${ledEnabled ? 'checked' : ''} onchange="saveLampSettings({led_enabled: this.checked})">
                        <span class="slider"></span>
                    </label>
                    <span class="alarm-label">${t('led_enabled')}</span>
                </div>

                <div style="margin-top:12px;">
                    <div style="display:flex; justify-content:space-between; margin-bottom:3px;">
                        <label style="font-size:0.8rem; color:#aaa; text-transform:uppercase;">${t('day_brightness')}</label>
                        <span id="led-day-disp" style="color:#d4af37; font-weight:bold;">${ledDayPct}%</span>
                    </div>
                    <input type="range" min="0" max="100" value="${ledDayPct}"
                           oninput="document.getElementById('led-day-disp').innerText=this.value+'%'"
                           onchange="saveLampSettings({led_day_pct: parseInt(this.value)})" style="width:100%"/>
                    <div style="display:flex; justify-content:space-between; align-items:center; margin-top:6px;">
                        <label style="font-size:0.8rem; color:#aaa; text-transform:uppercase;">${t('day_start')}</label>
                        <select id="led-day-start" onchange="saveLampSettings({led_day_start: parseInt(this.value)})" style="padding:6px; width:120px; background:#111; color:#fff; border:1px solid #555; border-radius:4px; font-size:0.8rem;">
                            ${renderHourOptions(ledDayStart)}
                        </select>
                    </div>
                </div>

                <div style="margin-top:14px; border-top:1px solid #333;"></div>

                <div style="margin-top:12px;">
                    <div style="display:flex; justify-content:space-between; margin-bottom:3px;">
                        <label style="font-size:0.8rem; color:#aaa; text-transform:uppercase;">${t('night_brightness')}</label>
                        <span id="led-night-disp" style="color:#d4af37; font-weight:bold;">${ledNightPct}%</span>
                    </div>
                    <input type="range" min="0" max="100" value="${ledNightPct}"
                           oninput="document.getElementById('led-night-disp').innerText=this.value+'%'"
                           onchange="saveLampSettings({led_night_pct: parseInt(this.value)})" style="width:100%"/>
                    <div style="display:flex; justify-content:space-between; align-items:center; margin-top:6px;">
                        <label style="font-size:0.8rem; color:#aaa; text-transform:uppercase;">${t('night_start')}</label>
                        <select id="led-night-start" onchange="saveLampSettings({led_night_start: parseInt(this.value)})" style="padding:6px; width:120px; background:#111; color:#fff; border:1px solid #555; border-radius:4px; font-size:0.8rem;">
                            ${renderHourOptions(ledNightStart)}
                        </select>
                    </div>
                </div>

                <div style="margin-top:12px;">
                    <div style="display:flex; justify-content:space-between; margin-bottom:3px;">
                        <label style="font-size:0.8rem; color:#aaa; text-transform:uppercase;">${t('night_base_volume')}</label>
                        <span id="night-base-vol-disp" style="color:#d4af37; font-weight:bold;">${nightBaseVol}%</span>
                    </div>
                    <input type="range" min="0" max="100" value="${nightBaseVol}"
                           oninput="document.getElementById('night-base-vol-disp').innerText=this.value+'%'"
                           onchange="saveLampSettings({night_base_volume: parseInt(this.value)})" style="width:100%"/>
                </div>
            </div>

        </div>
    `;
}

window.saveTimezone = (val) => {
    API.saveSettings({timezone: val}).then(() => {
        // Reload settings to refresh time display
        API.getSettings().then(s => {
            state.settings = s;
            render();
        });
    });
};

window.saveVol = (type, v) => {
    const val = parseInt(v);
    if (type === 'base') {
        state.settings.volume = val;
        API.saveSettings({volume: val});
    } else if (type === 'handset') {
         state.settings.volume_handset = val;
         API.saveSettings({volume_handset: val});
    } else if (type === 'alarm') {
         state.settings.vol_alarm = val;
         API.saveSettings({vol_alarm: val});
    }
};

window.saveTimerRingtone = (filename) => {
    if (!filename) return;
    state.settings.timer_ringtone = filename;
    API.saveSettings({timer_ringtone: filename});
};

window.saveLampSettings = (patch) => {
    if (!patch || typeof patch !== 'object') return;
    Object.assign(state.settings, patch);
    API.saveSettings(patch);
    logLampSettings('settings', patch);
};

function renderSetup() {
    let content = "";
    if (state.scanning) {
        content = `<p style='text-align:center;'>${t('scanning_wifi')} <br> (${t('please_wait')})</p>`;
    } else if (state.wifiError) {
        content = `<p style='color:red'>${t('scan_error')}: ${state.wifiError}. <a href='#' onclick='window.location.reload()'>${t('retry')}</a></p>`;
    } else if (state.wifiList) {
        content = "<ul class='wifi-list'>";
        state.wifiList.forEach(w => {
            const secure = w.auth > 0 ? "🔒" : "🔓";
            content += `<li onclick="selectWifi('${w.ssid}')" style="cursor:pointer; padding:10px; border-bottom:1px solid #444;">
                <b>${w.ssid}</b> <small>${secure} ${w.rssi}dBm</small>
            </li>`;
        });
        content += "</ul>";
    }

    // Modal or Input Area
    if (state.selectedWifi) {
        content = `
            <p style="margin-bottom: 10px;">${t('connect_to')} ${state.selectedWifi}</p>
            <input type="password" id="wifi-pass" placeholder="${t('password')}" style="width:100%; padding:10px; margin:10px 0; color:black;">
            <br>
            <button onclick="connectWifi()" style="padding:10px 20px;">${t('connect')}</button>
            <button onclick="state.selectedWifi=null; render()" style="padding:10px 20px; background:#444;">${t('cancel')}</button>
        `;
    }

    return `
        <div class="card">
            <p>${t('select_network')}</p>
            <div style="max-height:300px; overflow-y:auto; border:1px solid #555;">
                ${content}
            </div>
        </div>
    `;
}

window.selectWifi = (ssid) => {
    state.selectedWifi = ssid;
    render();
};

window.connectWifi = () => {
    const pass = document.getElementById('wifi-pass').value;
    const ssid = state.selectedWifi;
    document.getElementById('app').innerHTML = t('saving_connecting');
    
    API.saveSettings({
        wifi_ssid: ssid,
        wifi_pass: pass
    }).then(() => {
        document.getElementById('app').innerHTML = `
            <div style="text-align:center; padding:50px;">
                <h2>${t('saved')}</h2>
                <p>${t('connecting_to')} <b>${ssid}</b>.</p>
                <p>${t('ap_disappear')}</p>
                <p>${t('switch_back')} <b>${ssid}</b> ${t('find_device_at')}</p>
                <p><a href="http://dial-a-charmer.local">http://dial-a-charmer.local</a></p>
            </div>
        `;
    });
};

// Removed duplicate window.saveVol. It is defined above in renderAdvanced context.
document.addEventListener("DOMContentLoaded", init);
