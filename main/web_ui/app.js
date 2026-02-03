// Dial-A-Charmer Single Page Application using Vanilla JS
// Replicates the original SSR design but runs client-side.

const state = {
    lang: 'de',
    settings: {}, // will hold wifi, volume etc
    phonebook: {},
    loading: true
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
        subtitle: "Hörer abheben zum Wählen",
        alarms: "Wecker",
        pb: "Telefonbuch",
        config: "Konfiguration",
        help: "Hilfe / Manual",
        home: "Home",
        save: "Speichern"
    },
    en: {
        title: "Dial-A-Charmer",
        subtitle: "Lift receiver to dial",
        alarms: "Alarms",
        pb: "Phonebook",
        config: "Configuration",
        help: "Help / Manual",
        home: "Home",
        save: "Save"
    }
};

const API = {
    getSettings: () => fetch('/api/settings').then(r => r.json()),
    saveSettings: (data) => fetch('/api/settings', { method: 'POST', body: JSON.stringify(data) }),
    getPhonebook: () => fetch('/api/phonebook').then(r => r.json()),
    savePhonebook: (data) => fetch('/api/phonebook', { method: 'POST', body: JSON.stringify(data) }),
    scanWifi: () => fetch('/api/wifi/scan').then(r => r.json())
};

async function init() {
    onInitStart();
    try {
        console.log("Fetching Settings...");

        // Check Status First (to see if we are in AP mode)
        const statusResp = await fetch('/api/status');
        const status = await statusResp.json();
        console.log("System Status:", status);

        const response = await fetch('/api/settings');
         if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const settings = await response.json();
        
        console.log("Settings loaded:", settings);
        state.settings = settings;
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
    const app = document.getElementById('app');
    
    // Header / Title
    let html = `
        <div style="text-align:center;">
        <h2>${t('title')}</h2>
        <div style="color:#888; font-family: 'Plaisir', serif; font-size: 0.7rem;">v2.0 ESP-IDF</div>
        </div>
    `;

    // Routing
    if (path === '/' || path === '/index.html') {
        html += renderHome();
    } else if (path === '/phonebook') {
        html += renderPhonebook(); // Async needs handling, but we fetch on load or here
    } else if (path === '/settings') {
        html += renderSettings();
    } else if (path === '/advanced') {
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
    
    // Post-render Logic (Listeners)
    if (path === '/phonebook' && Object.keys(state.phonebook).length === 0) {
        API.getPhonebook().then(pb => {
            state.phonebook = pb;
            render(); // Re-render with data
        });
    }
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
        
        ${renderBtn(t('alarms'), '/settings')}
        ${renderBtn(t('pb'), '/phonebook')}
        ${renderBtn(t('config'), '/advanced')}
        
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
        {k: 'alarms', h: '/settings'},
        {k: 'pb', h: '/phonebook'},
        {k: 'config', h: '/advanced'}
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
    let rows = "";
    // Sort keys
    const keys = Object.keys(state.phonebook).sort((a,b) => a.localeCompare(b));
    keys.forEach(k => {
        const e = state.phonebook[k];
        rows += `
            <div class="row">
                <div class="num">${k}</div>
                <div class="details">
                    <b>${e.name}</b><br>
                    <small>${e.type}: ${e.value}</small>
                </div>
            </div>
        `;
    });
    
    return `
        <div class="card">
            <h3>${t('pb')}</h3>
            <div class="pb-list">${rows}</div>
        </div>
    `;
}

function renderSettings() {
    return `
        <div class="card">
            <h3>${t('alarms')}</h3>
            <p>Coming Soon matching original logic.</p>
        </div>
    `;
}

function renderAdvanced() {
     return `
        <div class="card">
            <h3>${t('config')}</h3>
            <label>WiFi SSID</label>
            <input type="text" value="${state.settings.wifi_ssid || ''}" disabled />
            <br>
            <button onclick="nav('/setup')">Re-Scan WiFi</button>
            <br>
            <label>Volume</label>
            <input type="range" min="0" max="100" value="${state.settings.volume || 60}" onchange="saveVol(this.value)"/>
        </div>
    `;
}

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
}

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
}

window.saveVol = (v) => {
    API.saveSettings({volume: parseInt(v)});
}

document.addEventListener("DOMContentLoaded", init);
