// Dial-A-Charmer SPA Logic

document.addEventListener("DOMContentLoaded", () => {
    console.log("App Loaded");
});

function saveLang(lang) {
    fetch('/api/settings/lang', { 
        method: 'POST', 
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ lang: lang }) 
    }).then(() => location.reload());
}
