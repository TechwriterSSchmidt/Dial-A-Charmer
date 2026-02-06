// Dial-A-Charmer Single Page Application using Vanilla JS
// Replicates the original SSR design but runs client-side.

const state = {
    lang: 'de',
    settings: {}, // will hold wifi, volume etc
    loading: true,
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
        snooze: "Schlummerzeit", // New
        datetime: "Datum & Zeit",
        timezone: "Zeitzone",
        tz_save: "Zone speichern"
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
        snooze: "Snooze Time", // New
        datetime: "Date & Time",
        timezone: "Timezone",
        tz_save: "Save Zone"
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

const API = {
    getSettings: () => fetch('/api/settings').then(r => r.json()),
    saveSettings: (data) => fetch('/api/settings', { method: 'POST', body: JSON.stringify(data) }),
    scanWifi: () => fetch('/api/wifi/scan').then(r => r.json()),
    getRingtones: () => fetch('/api/ringtones').then(r => r.json()),
    getLogs: () => fetch('/api/logs').then(r => r.json()),
    getPhonebook: () => fetch('/api/phonebook').then(r => r.json()),
    savePhonebook: (data) => fetch('/api/phonebook', { method: 'POST', body: JSON.stringify(data) }),
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
        document.getElementById('app').innerHTML = `<h1 style='color:red;'>Network Error</h1><p>${e.message}</p><p>Check Console</p>`;
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
    if (path === '/alarm') pageTitle = "Wecker";
    else if (path === '/configuration') pageTitle = "Konfiguration";
    else if (path === '/phonebook') pageTitle = t('pb');
    else if (path === '/setup') pageTitle = "Setup";

    // Header / Title (Dynamic + Version)
    let html = `
        <div style="text-align:center;">
        <h2>${pageTitle}<br><span style="color:#888; font-family: 'Plaisir', serif; font-size: 0.7rem; display:block; margin-top:5px;">v2.0 ESP-IDF</span></h2>
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
    if (path === '/phonebook' && Object.keys(state.phonebook).length === 0) {
        API.getPhonebook().then(pb => {
            state.phonebook = pb;
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
            html += `<a href="#" onclick="nav('${l.h}')" style="${style}">${t(l.k)}</a>`;
        }
    });
    html += "</div>";
    return html;
}

// Helpers
window.setLang = (l) => {
    state.lang = l;
    state.settings.lang = l;
    API.saveSettings({lang: l}).then(() => render());
};

window.nav = (h) => {
    window.history.pushState({}, "", h);
    render();
};

window.onpopstate = render;

// --- SPECIFIC PAGES ---
function renderPhonebook() {
    const isDe = state.lang === 'de';
    const tTitle = isDe ? "Telefonbuch" : "Phonebook";
    const fullData = {};

    const systemItems = [
        { id: 'p1', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '1', defName: 'Persona 1 (Default)', defNum: '1' },
        { id: 'p2', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '2', defName: 'Persona 2 (Joke)', defNum: '2' },
        { id: 'p3', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '3', defName: 'Persona 3 (SciFi)', defNum: '3' },
        { id: 'p4', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '4', defName: 'Persona 4 (Captain)', defNum: '4' },
        { id: 'p5', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '5', defName: 'Persona 5', defNum: '5' },
        { id: 'p6', type: 'FUNCTION', val: 'COMPLIMENT_MIX', param: '0', defName: 'Random Mix (Surprise)', defNum: '11' },

        { id: 'time', type: 'FUNCTION', val: 'ANNOUNCE_TIME', param: '', defName: isDe ? 'Zeitauskunft' : 'Time Announcement', defNum: '110' },
        { id: 'gem', type: 'FUNCTION', val: 'GEMINI_CHAT', param: '', defName: 'Gemini AI', defNum: '000' },
        { id: 'menu', type: 'FUNCTION', val: 'VOICE_MENU', param: '', defName: isDe ? 'Sprachmenue' : 'Voice Admin Menu', defNum: '900' },
        { id: 'tog', type: 'FUNCTION', val: 'TOGGLE_ALARMS', param: '', defName: isDe ? 'Wecker schalten' : 'Toggle Alarms', defNum: '910' },
        { id: 'skip', type: 'FUNCTION', val: 'SKIP_NEXT_ALARM', param: '', defName: isDe ? 'Naechsten Wecker ueberspringen' : 'Skip Next Alarm', defNum: '911' },
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
    });

    return `
        <div class="pb-wrapper">
            <div class="pb-title">${tTitle}</div>
            <div class="pb-notepad">
                <table class="pb-table">
                    <thead>
                        <tr class="pb-head">
                            <th class="pb-head-num"><span class="pb-phone-icon">&#128222;</span></th>
                            <th class="pb-head-name">Name</th>
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

window.savePhonebook = () => {
    const isDe = state.lang === 'de';
    const fullData = state.phonebook || {};

    const systemItems = [
        { id: 'p1', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '1', defName: 'Persona 1 (Default)' },
        { id: 'p2', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '2', defName: 'Persona 2 (Joke)' },
        { id: 'p3', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '3', defName: 'Persona 3 (SciFi)' },
        { id: 'p4', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '4', defName: 'Persona 4 (Captain)' },
        { id: 'p5', type: 'FUNCTION', val: 'COMPLIMENT_CAT', param: '5', defName: 'Persona 5' },
        { id: 'p6', type: 'FUNCTION', val: 'COMPLIMENT_MIX', param: '0', defName: 'Random Mix (Surprise)' },

        { id: 'time', type: 'FUNCTION', val: 'ANNOUNCE_TIME', param: '', defName: isDe ? 'Zeitauskunft' : 'Time Announcement' },
        { id: 'gem', type: 'FUNCTION', val: 'GEMINI_CHAT', param: '', defName: 'Gemini AI' },
        { id: 'menu', type: 'FUNCTION', val: 'VOICE_MENU', param: '', defName: isDe ? 'Sprachmenue' : 'Voice Admin Menu' },
        { id: 'tog', type: 'FUNCTION', val: 'TOGGLE_ALARMS', param: '', defName: isDe ? 'Wecker schalten' : 'Toggle Alarms' },
        { id: 'skip', type: 'FUNCTION', val: 'SKIP_NEXT_ALARM', param: '', defName: isDe ? 'Naechsten Wecker ueberspringen' : 'Skip Next Alarm' },
        { id: 'reboot', type: 'FUNCTION', val: 'REBOOT', param: '', defName: isDe ? 'System Neustart' : 'System Reboot' }
    ];

    const newData = {};

    for (const [key, entry] of Object.entries(fullData)) {
        const isSystem = systemItems.some(item => {
            const entryParam = entry.parameter || "";
            const itemParam = item.param || "";
            return entry.type === item.type && entry.value === item.val && entryParam === itemParam;
        });
        if (!isSystem) newData[key] = entry;
    }

    systemItems.forEach(item => {
        const input = document.getElementById(`pb_input_${item.id}`);
        if (!input) return;

        const newKey = input.value.trim();
        if (!newKey) return;

        let name = item.defName;
        for (const entry of Object.values(fullData)) {
            const entryParam = entry.parameter || "";
            const itemParam = item.param || "";
            if (entry.type === item.type && entry.value === item.val && entryParam === itemParam) {
                if (entry.name && entry.name !== "Unknown") name = entry.name;
                break;
            }
        }

        newData[newKey] = {
            name: name,
            type: item.type,
            value: item.val,
            parameter: item.param
        };
    });

    document.getElementById('app').innerHTML = `<div style="text-align:center; padding:50px; color:#d4af37;"><h3>${isDe ? 'Speichern...' : 'Saving...'}</h3></div>`;

    API.savePhonebook(newData).then(() => {
        state.phonebook = newData;
        render();
    });
};

function renderSettings() {
    const DAYS_DE = ["Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"];
    const DAYS_EN = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"];
    
    const days = state.lang === 'de' ? DAYS_DE : DAYS_EN;
    // Ensure alarms exist (backend should send them, but safe fallback)
    const alarms = state.settings.alarms || []; 
    
    // Display order: Mon(1) ... Sun(0)
    const displayOrder = [1, 2, 3, 4, 5, 6, 0];
    const ringtones = state.ringtones || [];

    let rows = "";
    if (alarms.length === 0) {
        rows = "<p style='text-align:center;'>Loading alarms...</p>";
    } else {
        displayOrder.forEach(dayIndex => {
            const a = alarms.find(x => x.d === dayIndex);
            if (!a) return;

            const dayName = days[a.d] || "Day " + a.d;
            const hh = String(a.h).padStart(2,'0');
            const mm = String(a.m).padStart(2,'0');
            const timeVal = `${hh}:${mm}`;
            const checked = a.en ? "checked" : "";
            const rampChecked = a.rmp ? "checked" : ""; // Rising volume
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
                <div style="display:flex; justify-content:space-between; width:100%; align-items:center; margin-bottom: 8px;">
                    <div style="display:flex; align-items:center;">
                        <label class="switch" title="${t('active')}">
                            <input type="checkbox" id="check-${a.d}" ${checked}>
                            <span class="slider"></span>
                        </label>
                        <span style='margin-left: 10px; font-size: 0.9rem; color: #aaa;'>${t('active')}</span>
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
                        <span style='margin-left: 10px; font-size: 0.9rem; color: #aaa;'>${t('fade')}</span>
                    </div>
                    
                    <select id="snd-${a.d}" onchange="previewTone(this.value)" class="alarm-sound-select">
                        ${soundOpts}
                    </select>
                </div>
            </div>
            `;
        });

        // Snooze Selection
        const currentSnooze = state.settings.snooze_min || 5;
        let snoozeOpts = "";
        for (let i=1; i<=20; i++) {
            const sel = (i === currentSnooze) ? "selected" : "";
            snoozeOpts += `<option value="${i}" ${sel}>${i} min</option>`;
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

window.previewTone = (filename) => {
    if (!filename) return;
    
    // Play on device via API
    fetch(`/api/preview?file=${filename}`)
        .catch(e => console.log("Preview request failed: " + e));
};

window.saveAlarms = () => {
    const newAlarms = [];
    for(let i=0; i<7; i++) {
        const timeInput = document.getElementById(`time-${i}`);
        const checkInput = document.getElementById(`check-${i}`);
        const rampInput = document.getElementById(`ramp-${i}`);
        const sndInput = document.getElementById(`snd-${i}`);
        
        if(timeInput && checkInput) {
            const timeStr = timeInput.value; // "HH:MM"
            const en = checkInput.checked;
            const rmp = rampInput ? rampInput.checked : false;
            const snd = sndInput ? sndInput.value : "digital_alarm.wav";
            const [h, m] = timeStr.split(':').map(Number);
            newAlarms.push({d: i, h: h, m: m, en: en, rmp: rmp, snd: snd});
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
            <div style="background:#222; padding:15px; border-radius:8px; margin-bottom:15px; border:1px solid #444;">
                 <h4 style="margin-top:0; color:#d4af37; font-size:0.9rem; text-transform:uppercase; border-bottom:1px solid #444; padding-bottom:5px;">WiFi Network</h4>

                 <div class="wifi-ssid-text">${state.settings.wifi_ssid || 'No Net'}</div>
                 <button onclick="nav('/setup')" class="wifi-scan-btn">SCAN</button>
            </div>

            <!-- CONSOLE PANEL -->
            <div class="crt-panel">
                <div class="crt-screen">
                    <div class="crt-text" id="crt-log">READY&gt;_</div>
                </div>
            </div>

            <!-- VOLUME PANEL -->
            <div style="background:#222; padding:15px; border-radius:8px; margin-bottom:15px; border:1px solid #444;">
                <h4 style="margin-top:0; color:#d4af37; font-size:0.9rem; text-transform:uppercase; border-bottom:1px solid #444; padding-bottom:5px;">Volume</h4>
                
                <!-- Base Speaker -->
                <div style="margin-top:12px;">
                    <div style="display:flex; justify-content:space-between; margin-bottom:5px;">
                        <label style="font-size:0.8rem; color:#aaa; text-transform:uppercase;">Base Speaker</label>
                        <span id="vol-disp-base" style="color:#d4af37; font-weight:bold;">${state.settings.volume || 60}%</span>
                    </div>
                    <input type="range" min="0" max="100" value="${state.settings.volume || 60}" 
                           oninput="document.getElementById('vol-disp-base').innerText=this.value+'%'"
                           onchange="saveVol('base', this.value)" style="width:100%"/>
                </div>

                <!-- Handset Speaker -->
                <div style="margin-top:15px;">
                     <div style="display:flex; justify-content:space-between; margin-bottom:5px;">
                        <label style="font-size:0.8rem; color:#aaa; text-transform:uppercase;">Handset</label>
                        <span id="vol-disp-handset" style="color:#d4af37; font-weight:bold;">${state.settings.volume_handset || 60}%</span>
                    </div>
                    <input type="range" min="0" max="100" value="${state.settings.volume_handset || 60}" 
                           oninput="document.getElementById('vol-disp-handset').innerText=this.value+'%'"
                           onchange="saveVol('handset', this.value)" style="width:100%"/>
                </div>
            </div>

            <!-- TIMER RINGTONE PANEL -->
            <div style="background:#222; padding:15px; border-radius:8px; margin-bottom:15px; border:1px solid #444;">
                <h4 style="margin-top:0; color:#d4af37; font-size:0.9rem; text-transform:uppercase; border-bottom:1px solid #444; padding-bottom:5px;">Timer Alarm</h4>
                
                <div style="margin-top:12px;">
                    <label style="font-size:0.8rem; color:#aaa; text-transform:uppercase; display:block; margin-bottom:8px;">Ringtone</label>
                    <select id="timer-ringtone-select" onchange="saveTimerRingtone(this.value); previewTone(this.value);" style="width:100%; padding:8px; background:#111; color:#fff; border:1px solid #555; border-radius:4px; font-size:0.9rem;">
                        ${(() => {
                            const ringtones = state.ringtones || [];
                            const current = state.settings.timer_ringtone || 'digital_alarm.wav';
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
    } else {
         state.settings.volume_handset = val;
         API.saveSettings({volume_handset: val});
    }
};

window.saveTimerRingtone = (filename) => {
    if (!filename) return;
    state.settings.timer_ringtone = filename;
    API.saveSettings({timer_ringtone: filename});
};

function renderSetup() {
    let content = "";
    if (state.scanning) {
        content = "<p style='text-align:center;'>Scanning WiFi Networks... <br> (Please wait)</p>";
    } else if (state.wifiError) {
        content = `<p style='color:red'>Scan Error: ${state.wifiError}. <a href='#' onclick='window.location.reload()'>Retry</a></p>`;
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
            <h3>Connect to ${state.selectedWifi}</h3>
            <input type="password" id="wifi-pass" placeholder="Password" style="width:100%; padding:10px; margin:10px 0; color:black;">
            <br>
            <button onclick="connectWifi()" style="padding:10px 20px;">Connect</button>
            <button onclick="state.selectedWifi=null; render()" style="padding:10px 20px; background:#444;">Cancel</button>
        `;
    }

    return `
        <div class="card">
            <h3>WiFi Setup</h3>
            <p>Select your local network:</p>
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
    document.getElementById('app').innerHTML = "Saving and Connecting...";
    
    API.saveSettings({
        wifi_ssid: ssid,
        wifi_pass: pass
    }).then(() => {
        document.getElementById('app').innerHTML = `
            <div style="text-align:center; padding:50px;">
                <h2>Saved!</h2>
                <p>The device is now connecting to <b>${ssid}</b>.</p>
                <p>The Access Point will disappear.</p>
                <p>Please switch your phone/PC back to <b>${ssid}</b> and find the device at:</p>
                <p><a href="http://dial-a-charmer.local">http://dial-a-charmer.local</a></p>
            </div>
        `;
    });
};

// Removed duplicate window.saveVol. It is defined above in renderAdvanced context.
document.addEventListener("DOMContentLoaded", init);
