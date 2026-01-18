#ifndef WEB_RESOURCES_H
#define WEB_RESOURCES_H

#include <Arduino.h>

const char* COMMON_CSS = R"rawliteral(
<style>
@font-face {
    font-family: 'Zen Tokyo Zoo';
    src: url('/fonts/ZenTokyoZoo-Regular.ttf') format('truetype');
    font-display: swap;
}
@font-face {
    font-family: 'Pompiere';
    src: url('/fonts/Pompiere-Regular.ttf') format('truetype');
    font-display: swap;
}

body {
    font-family: 'Pompiere', cursive, sans-serif;
    font-size: 1.4rem;
    background-color: #080808; /* Deep Black */
    color: #f0e6d2; /* Cream/Champagne */
    margin: 0;
    padding: 20px;
    line-height: 1.5;
}
h2 {
    font-family: 'Zen Tokyo Zoo', cursive;
    text-align: center;
    text-transform: uppercase;
    letter-spacing: 4px; /* Art Deco Spacing */
    color: #d4af37; /* Metallic Gold */
    border-bottom: 2px solid #d4af37;
    margin-bottom: 40px;
    padding-bottom: 15px;
    font-weight: normal;
    font-size: 2.2rem;
}
.card {
    background: #111;
    border: 1px solid #222;
    border-top: 4px solid #d4af37; /* Gold Accent */
    padding: 30px;
    margin-bottom: 30px;
    box-shadow: 0 10px 30px rgba(0,0,0,0.8);
    border-radius: 15px; /* Soft Card */
}
.card h3 {
    font-family: 'Zen Tokyo Zoo', cursive;
    margin-top: 0;
    color: #d4af37;
    font-size: 1.5rem;
    text-transform: uppercase;
    letter-spacing: 2px;
    border-bottom: 1px solid #333;
    padding-bottom: 15px;
    font-weight: normal;
}
label {
    display: block;
    margin-top: 20px;
    font-size: 1.3rem;
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
    font-family: 'Pompiere', cursive, sans-serif;
    font-size: 1.4rem;
    box-sizing: border-box;
    border-radius: 12px; /* Soft Rounded Edges */
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
    font-family: 'Pompiere', cursive, sans-serif;
    border-radius: 12px; /* Rounded Button */
}
button:hover {
    background-color: #b22222;
    color: #fff;
    border-color: #f00;
    box-shadow: 0 0 15px rgba(178, 34, 34, 0.4);
}
/* Toggle Switch Style (Radio-look replacement) */
.switch {
  position: relative;
  display: inline-block;
  width: 50px;
  height: 28px;
  margin: 0;
}
.switch input { 
  opacity: 0;
  width: 0;
  height: 0;
}
.slider {
  position: absolute;
  cursor: pointer;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background-color: #333;
  transition: .4s;
  border-radius: 34px;
  border: 1px solid #555;
}
.slider:before {
  position: absolute;
  content: "";
  height: 20px;
  width: 20px;
  left: 3px;
  bottom: 3px;
  background-color: #888;
  transition: .4s;
  border-radius: 50%;
}
input:checked + .slider {
  background-color: #d4af37;
  border-color: #d4af37;
}
input:focus + .slider {
  box-shadow: 0 0 1px #d4af37;
}
input:checked + .slider:before {
  transform: translateX(22px);
  background-color: #fff;
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

const char* AP_SETUP_CSS = R"rawliteral(
<style>
body { font-family: sans-serif; background: #111; color: #eee; padding: 20px; text-align: center; }
h2 { color: #d4af37; margin-bottom: 30px; }
input { width: 100%; padding: 12px; margin: 10px 0; border: 1px solid #444; background: #222; color: #fff; font-size: 1.2rem; border-radius: 5px; box-sizing: border-box; }
button { width: 100%; padding: 15px; margin-top: 20px; background: #d4af37; color: #000; font-size: 1.2rem; font-weight: bold; border: none; border-radius: 5px; cursor: pointer; }
button:hover { background: #eac14d; }
.card { background: #1a1a1a; padding: 20px; border-radius: 10px; max-width: 400px; margin: 0 auto; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }
a { color: #888; text-decoration: underline; margin-top: 20px; display: inline-block; }
</style>
)rawliteral";

#endif
