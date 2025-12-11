// 08_html.ino
#include "openhandy.h"
#include <Arduino.h>

extern HandyConfig   g_cfg;
extern NetworkConfig g_netCfg;

// ---------------------------------------------------------------------------
// Shared HTML header / footer
// ---------------------------------------------------------------------------

static String htmlHeader(const String &title) {
  String html =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>" + title + "</title>"
    "<style>"
    "body{margin:0;font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif;background:#050509;color:#f5f5f5;}"
    "a{color:#5ec8ff;text-decoration:none;}"
    ".wrap{max-width:1080px;margin:0 auto;padding:16px;}"
    ".grid{display:grid;grid-template-columns:minmax(0,2fr) minmax(0,1.4fr);gap:16px;}"
    "@media(max-width:800px){.grid{grid-template-columns:1fr;}}"
    ".grid2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;}"
    ".card{background:radial-gradient(circle at top left,#20223a,#111321);border-radius:14px;padding:16px;border:1px solid #26293d;box-shadow:0 0 30px rgba(0,0,0,0.7);}"
    "h1{margin-top:0;margin-bottom:4px;font-size:1.5rem;}"
    "h2{margin-top:16px;margin-bottom:8px;font-size:1.05rem;color:#d0d4ff;border-bottom:1px solid #2c3150;padding-bottom:4px;}"
    "p{margin:4px 0;font-size:0.9rem;color:#d0d0e0;}"
    "label{display:block;margin:6px 0 2px;font-size:0.85rem;color:#c0c3e0;}"
    "input[type=text],input[type=password],input[type=number],select{width:100%;box-sizing:border-box;padding:6px 8px;margin-bottom:6px;border-radius:7px;border:1px solid #2c3050;background:#050713;color:#f5f5ff;font-size:0.9rem;}"
    "input[type=checkbox]{margin-right:6px;}"
    ".btn{display:inline-block;margin:4px 4px 0 0;padding:6px 12px;border-radius:999px;border:1px solid #3a3f70;background:linear-gradient(135deg,#262b4a,#181b33);color:#f5f5ff;font-size:0.85rem;cursor:pointer;}"
    ".btn:hover{background:linear-gradient(135deg,#313760,#1f2340);}"
    "code{background:#121526;padding:2px 5px;border-radius:4px;font-size:0.8rem;}"
    ".debug-log{white-space:pre-wrap;font-family:monospace;background:#050509;border-radius:10px;border:1px solid #2c3050;padding:8px;max-height:420px;overflow-y:auto;font-size:0.8rem;}"
    ".row{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:4px 0;}"
    ".row label{margin:0;}"
    ".small{font-size:0.8rem;color:#9ea2d0;}"
    "input[type=range]{width:100%;}"
    ".value-badge{display:inline-block;padding:2px 6px;border-radius:6px;background:#151833;font-size:0.8rem;margin-left:6px;}"
    "</style>"
    "</head><body>";
  return html;
}

static String htmlFooter() {
  String html =
    "<div class='wrap'><p style='margin-top:32px;font-size:0.8rem;color:#777a9e;'>theOPENhandy</p></div>"
    "</body></html>";
  return html;
}

// ---------------------------------------------------------------------------
// Root / main menu page
// ---------------------------------------------------------------------------

String html_pageRoot() {
  String ver = getFwVersionFull();
  String html = htmlHeader("theOPENhandy Portal");

  html +=
    "<div class='wrap'><div class='card'>"
    "<h1>theOPENhandy</h1>"
    "<p>Configuration &amp; OTA firmware upload.</p>"
    "<p>Firmware: <code>" + ver + "</code></p>"

    "<h2>Control</h2>"
    "<p>"
      "<a class='btn' href='/motion'>Motion</a>"
      "<a class='btn' href='/settings'>Settings</a>"
    "</p>"

    "<h2>Network</h2>"
    "<p><a class='btn' href='/net'>Network</a></p>"

    "<h2>Debug</h2>"
    "<p><a class='btn' href='/debug'>Debug view</a></p>"

    "<h2>Firmware</h2>"
    "<p><a class='btn' href='/update'>Firmware update</a></p>"

    "<h2>System</h2>"
    "<p><a class='btn' href='/reboot'>Reboot device</a></p>"

    "</div></div>";

  html += htmlFooter();
  return html;
}

// ---------------------------------------------------------------------------
// Motion page
// ---------------------------------------------------------------------------

String html_pageMotion() {
  int    sp;
  float  cutL, cutU;
  uint8_t pat;
  actuator_getManualUiState(sp, cutL, cutU, pat);

  if (sp < 0)   sp = 0;
  if (sp > 100) sp = 100;
  int cutLPct = (int)(cutL * 100.0f + 0.5f);
  int cutUPct = (int)(cutU * 100.0f + 0.5f);
  if (cutLPct < 0) cutLPct = 0;
  if (cutLPct > 100) cutLPct = 100;
  if (cutUPct < 0) cutUPct = 0;
  if (cutUPct > 100) cutUPct = 100;

  String html = htmlHeader("Motion control");

  html +=
    "<div class='wrap'><div class='card'>"
    "<h1>Motion control</h1>"
    "<p>Control the built-in manual pattern generator. Physical buttons still work as usual.</p>"
    "<h2>Pattern</h2>"
    "<div class='row'>"
    "<label for='pattern'>Pattern style</label>"
    "<select id='pattern' onchange='onPatternChange()'>"
      "<option value='0'" + String(pat == 0 ? " selected" : "") + ">Sine</option>"
      "<option value='1'" + String(pat == 1 ? " selected" : "") + ">Bounce</option>"
      "<option value='2'" + String(pat == 2 ? " selected" : "") + ">Double bounce</option>"
    "</select>"
    "</div>"
    "<h2>Speed</h2>"
    "<div class='row'>"
    "<button class='btn' onclick='api(\"/api/motion?action=slower\", refreshState)'>Slower</button>"
    "<button class='btn' onclick='api(\"/api/motion?action=faster\", refreshState)'>Faster</button>"
    "<span class='small'>Current speed: <span id='speedVal' class='value-badge'>" + String(sp) + "%</span></span>"
    "</div>"
    "<h2>Start / stop</h2>"
    "<div class='row'>"
    "<form method='POST' action='/motion/start' style='display:inline-block;margin-right:4px;'>"
    "<button class='btn' type='submit'>Start motion</button>"
    "</form>"
    "<form method='POST' action='/motion/stop' style='display:inline-block;'>"
    "<button class='btn' type='submit'>Stop motion</button>"
    "</form>"
    "</div>"
    "<h2>Stroke cropping</h2>"
    "<p class='small'>Lower = crop from bottom; Upper = crop from top. Physical LEFT/RIGHT buttons do the same.</p>"
    "<label for='cropLower'>Lower crop (%)</label>"
    "<input type='range' id='cropLower' min='0' max='80' value='" + String(cutLPct) + "' oninput='onCropChange()'>"
    "<div class='small'>Lower: <span id='cropLowerVal' class='value-badge'>" + String(cutLPct) + "%</span></div>"
    "<label for='cropUpper' style='margin-top:10px;'>Upper crop (%)</label>"
    "<input type='range' id='cropUpper' min='0' max='80' value='" + String(cutUPct) + "' oninput='onCropChange()'>"
    "<div class='small'>Upper: <span id='cropUpperVal' class='value-badge'>" + String(cutUPct) + "%</span></div>"
    "<p style='margin-top:16px;'><a class='btn' href='/'>Back</a></p>"
    "</div></div>";

  html +=
    "<script>"
    "function api(url, cb){"
      "fetch(url).then(function(r){return r.json();}).then(function(j){if(cb)cb(j);}).catch(function(e){console.log(e);});"
    "}"
    "function refreshState(){"
      "api('/api/motion?action=status', function(s){"
        "if(typeof s.speed !== 'undefined'){"
          "document.getElementById('speedVal').textContent = s.speed + '%';"
        "}"
        "if(typeof s.cutLower !== 'undefined'){"
          "var lp = Math.round(s.cutLower*100);"
          "document.getElementById('cropLower').value = lp;"
          "document.getElementById('cropLowerVal').textContent = lp + '%';"
        "}"
        "if(typeof s.cutUpper !== 'undefined'){"
          "var up = Math.round(s.cutUpper*100);"
          "document.getElementById('cropUpper').value = up;"
          "document.getElementById('cropUpperVal').textContent = up + '%';"
        "}"
        "if(typeof s.pattern !== 'undefined'){"
          "var sel = document.getElementById('pattern');"
          "if(sel && s.pattern>=0 && s.pattern<=2){ sel.value = s.pattern; }"
        "}"
      "});"
    "}"
    "function onPatternChange(){"
      "var m = document.getElementById('pattern').value;"
      "api('/api/motion?action=setpattern&mode=' + encodeURIComponent(m), refreshState);"
    "}"
    "function onCropChange(){"
      "var l = document.getElementById('cropLower').value;"
      "var u = document.getElementById('cropUpper').value;"
      "document.getElementById('cropLowerVal').textContent = l + '%';"
      "document.getElementById('cropUpperVal').textContent = u + '%';"
      "var lf = (parseFloat(l)||0)/100.0;"
      "var uf = (parseFloat(u)||0)/100.0;"
      "api('/api/motion?action=setcrop&lower=' + lf + '&upper=' + uf, null);"
    "}"
    "refreshState();"
    "</script>";

  html += htmlFooter();
  return html;
}

// ---------------------------------------------------------------------------
// Settings / tuning page
// ---------------------------------------------------------------------------

String html_pageSettings() {
  String html = htmlHeader("Tuning settings");

  html +=
    "<div class='wrap'><div class='card'>"
    "<h1>Tuning settings</h1>"
    "<p>Motor, sensor and safety parameters.</p>"
    "<h2>Motion &amp; sensor tuning</h2>"
    "<form method='POST' action='/config'>"
    "<div class='grid2'>"
      "<div>"
      "<label>Kp (speed control gain):<br>"
      "<input type='number' name='kp' step='0.01' min='0.01' max='1.00' "
        "value='" + String(g_cfg.kp, 2) + "'></label>"
      "</div>"
      "<div>"
      "<label>Max PWM (0..1023):<br>"
      "<input type='number' name='maxPwm' min='0' max='1023' "
        "value='" + String(g_cfg.maxPwm) + "'></label>"
      "</div>"
      "<div>"
      "<label>Homing PWM (0..1023):<br>"
      "<input type='number' name='homPwm' min='0' max='1023' "
        "value='" + String(g_cfg.homingPwm) + "'></label>"
      "</div>"
      "<div>"
      "<label>Stall timeout (ms without Hall A ticks):<br>"
      "<input type='number' name='stallMs' min='100' max='10000' "
        "value='" + String(g_cfg.stallTimeoutMs) + "'></label>"
      "</div>"
    "</div>"

    "<div class='grid2'>"
      "<div>"
      "<label>IR1 polarity (Position UP):<br>"
      "<input type='checkbox' name='ir1low' " +
        String(g_cfg.ir1ActiveLow ? "checked" : "") +
        "> Active low</label>"
      "</div>"
      "<div>"
      "<label>IR3 polarity (Position DOWN):<br>"
      "<input type='checkbox' name='ir3low' " +
        String(g_cfg.ir3ActiveLow ? "checked" : "") +
        "> Active low</label>"
      "</div>"
    "</div>"

    "<label>Thermal cutoff ADC (lower = hotter):<br>"
    "<input type='number' name='thrAdc' min='0' max='4095' "
      "value='" + String(g_cfg.thermalThresholdAdc) + "'></label>"

    "<div class='grid2'>"
      "<div>"
        "<label>Overshoot IR1 (UP):<br>"
          "<input type='number' name='ovrIr1' min='-20000' max='20000' "
            "value='" + String(g_cfg.overshootIr1Counts) + "'>"
        "</label>"
      "</div>"
      "<div>"
        "<label>Overshoot IR3 (DOWN):<br>"
          "<input type='number' name='ovrIr3' min='-20000' max='20000' "
            "value='" + String(g_cfg.overshootIr3Counts) + "'>"
        "</label>"
      "</div>"
    "</div>"

    "<div class='grid2'>"
      "<div>"
      "<label>Home position:<br>"
      "<select name='homeMode'>"
        "<option value='0'" + String(g_cfg.homeMode == 0 ? " selected" : "") + ">IR1 end</option>"
        "<option value='1'" + String(g_cfg.homeMode == 1 ? " selected" : "") + ">IR3 end</option>"
        "<option value='2'" + String(g_cfg.homeMode == 2 ? " selected" : "") + ">Middle</option>"
      "</select></label>"
      "</div>"
      "<div>"
      "<label>Boot jingle:<br>"
      "<select name='bootSound'>"
        "<option value='0'" + String(g_netCfg.bootSound == 0 ? " selected" : "") + ">None</option>"
        "<option value='1'" + String(g_netCfg.bootSound == 1 ? " selected" : "") + ">Vader</option>"
        "<option value='2'" + String(g_netCfg.bootSound == 2 ? " selected" : "") + ">Mario</option>"
        "<option value='3'" + String(g_netCfg.bootSound == 3 ? " selected" : "") + ">Streetfighter</option>"
        "<option value='4'" + String(g_netCfg.bootSound == 4 ? " selected" : "") + ">Space Odyssey</option>"
      "</select></label>"
      "</div>"
    "</div>"

    "<br><input class='btn' type='submit' value='Save &amp; reboot'>"
    "</form>"
    "<p style='margin-top:16px;'><a class='btn' href='/'>Back</a></p>"
    "</div></div>";

  html += htmlFooter();
  return html;
}

// ---------------------------------------------------------------------------
// Config saved page
// ---------------------------------------------------------------------------

String html_pageConfigSaved() {
  String html = htmlHeader("Config saved");
  html +=
    "<div class='wrap'><div class='card'>"
    "<h1>theOPENhandy</h1>"
    "<p>Configuration saved.</p>"
    "<p>Rebooting...</p>"
    "</div></div>";
  html += htmlFooter();
  return html;
}

// ---------------------------------------------------------------------------
// Debug page
// ---------------------------------------------------------------------------

String html_pageDebug(
  RunState st,
  bool ir1, bool ir3,
  long encAbs, long encAxis,
  bool limits, long posMin, long posMax,
  int thermAdc,
  bool collErr, bool thermErr,
  const String &log
) {
  String html = htmlHeader("Debug");
  html += "<div class='wrap'><div class='grid'>";

  // Left - live status
  html +=
    "<div class='card'>"
    "<h1>Live status</h1>"
    "<p><strong>RunState:</strong> " + String((int)st) + "</p>"
    "<p><strong>Endstop UP:</strong> " + String(ir1 ? "ON" : "OFF") + "</p>"
    "<p><strong>Endstop DOWN:</strong> " + String(ir3 ? "ON" : "OFF") + "</p>"
    "<p><strong>Encoder abs:</strong> " + String(encAbs) + "</p>"
    "<p><strong>Axis (0..10000):</strong> " + String(encAxis) + "</p>"
    "<p><strong>Limits valid:</strong> " + String(limits ? "YES" : "no") + "</p>"
    "<p><strong>posMin:</strong> " + String(posMin) + "</p>"
    "<p><strong>posMax:</strong> " + String(posMax) + "</p>"
    "<p><strong>Thermal ADC:</strong> " + String(thermAdc) + "</p>"
    "<p><strong>Collision err:</strong> " + String(collErr ? "YES" : "NO") + "</p>"
    "<p><strong>Thermal err:</strong> " + String(thermErr ? "YES" : "NO") + "</p>"
    "<p><a class='btn' href='/'>Back</a></p>"
    "</div>";

  // Right - debug log
  html +=
    "<div class='card'>"
    "<h1>Debug log</h1>"
    "<p><a class='btn' href='/debug?clear=1'>Clear log</a> "
    "<a class='btn' href='/debug?download=1'>Download</a> "
    "<a class='btn' href='/'>Back</a></p>"
    "<div class='debug-log'>";
  html += log;
  html += "</div></div>"; // log card

  html += "</div></div>"; // grid + wrap

  html +=
    "<script>"
    "setTimeout(function(){ window.location.reload(); }, 500);"
    "</script>";

  html += htmlFooter();
  return html;
}

// ---------------------------------------------------------------------------
// Reboot page
// ---------------------------------------------------------------------------

String html_pageReboot() {
  String html = htmlHeader("Rebooting");
  html +=
    "<div class='wrap'><div class='card'>"
    "<h1>theOPENhandy</h1>"
    "<p>Rebooting...</p>"
    "<p><a class='btn' href='/'>Back</a></p>"
    "</div></div>";
  html += htmlFooter();
  return html;
}

// ---------------------------------------------------------------------------
// OTA update pages
// ---------------------------------------------------------------------------

String html_pageUpdateForm() {
  String html = htmlHeader("Firmware update");
  html +=
    "<div class='wrap'><div class='card'>"
    "<h1>theOPENhandy</h1>"
    "<h2>OTA firmware upload</h2>"
    "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<label>Firmware (.bin):<br><input type='file' name='update'></label>"
    "<br><input class='btn' type='submit' value='Upload &amp; flash'>"
    "</form>"
    "<p><a class='btn' href='/'>Back</a></p>"
    "</div></div>";
  html += htmlFooter();
  return html;
}

String html_pageUpdateResult(bool success, const String &msg) {
  (void)success; // reserved for future styling differences
  String html = htmlHeader("Firmware update");
  html +=
    "<div class='wrap'><div class='card'>"
    "<h1>theOPENhandy</h1>"
    "<p>" + msg + "</p>"
    "<p><a class='btn' href='/'>Back</a></p>"
    "</div></div>";
  html += htmlFooter();
  return html;
}

// ---------------------------------------------------------------------------
// Network config pages
// ---------------------------------------------------------------------------

String html_pageNetConfig() {
  String html = htmlHeader("Network config");
  html +=
    "<div class='wrap'><div class='card'>"
    "<h1>theOPENhandy</h1>"
    "<h2>WiFi / IP / UDP</h2>"
    "<form method='POST' action='/net'>"

    "<label>SSID:<br>"
    "<div class='row'>"
    "<input type='text' id='ssidInput' name='ssid' value='" + g_netCfg.ssid + "'>"
    "</div>"
    "</label>"

    "<div class='row'>"
    "<button class='btn' type='button' onclick='scanWifi()'>Scan for networks</button>"
    "<select id='ssidList' style='flex:1 1 auto; min-width:160px;'>"
    "<option value=''>-- choose from scan --</option>"
    "</select>"
    "</div>"

    "<label>Password:<br>"
    "<div class='row'>"
    "<input type='password' id='wifiPass' name='pass' value='" + g_netCfg.password + "'>"
    "<label class='small'><input type='checkbox' id='showPass' onclick='togglePass()'> Show</label>"
    "</div>"
    "</label>"

    "<label>Hostname:<br>"
    "<input type='text' name='host' value='" + g_netCfg.hostname + "'></label>"

    "<label><input type='checkbox' name='dhcp' " +
      String(g_netCfg.useDhcp ? "checked" : "") +
      "> Use DHCP</label>"

    "<label>Static IP:<br>"
    "<input type='text' name='ip' value='" + g_netCfg.ip + "'></label>"

    "<label>Gateway:<br>"
    "<input type='text' name='gw' value='" + g_netCfg.gateway + "'></label>"

    "<label>Netmask:<br>"
    "<input type='text' name='mask' value='" + g_netCfg.netmask + "'></label>"

    "<label>DNS:<br>"
    "<input type='text' name='dns' value='" + g_netCfg.dns + "'></label>"

    "<label>UDP port (TCode):<br>"
    "<input type='number' name='udpport' min='1' max='65535' value='" +
      String(g_netCfg.udpPort) + "'></label>"

    "<br><input class='btn' type='submit' value='Save &amp; reboot'>"
    "</form>"
    "<p><a class='btn' href='/'>Back</a></p>"
    "</div></div>"
    "<script>"
    "function togglePass(){"
      "var f=document.getElementById('wifiPass');"
      "if(!f)return;"
      "f.type=(f.type==='password')?'text':'password';"
    "}"
    "function fillSsidDropdown(nets){"
      "var sel=document.getElementById('ssidList');"
      "if(!sel)return;"
      "sel.innerHTML='';"
      "var def=document.createElement('option');"
      "def.value='';"
      "def.textContent='-- choose from scan --';"
      "sel.appendChild(def);"
      "for(var i=0;i<nets.length;i++){"
        "var n=nets[i];"
        "var label=n.ssid + ' (' + n.rssi + ' dBm' + (n.open?' open':'') + ')';"
        "var o=document.createElement('option');"
        "o.value=n.ssid;"
        "o.textContent=label;"
        "sel.appendChild(o);"
      "}"
    "}"
    "function scanWifi(){"
      "var sel=document.getElementById('ssidList');"
      "if(sel){"
        "sel.innerHTML='';"
        "var opt=document.createElement('option');"
        "opt.value='';"
        "opt.textContent='Scanning...';"
        "sel.appendChild(opt);"
      "}"
      "fetch('/api/wifiscan')"
        ".then(function(r){return r.json();})"
        ".then(function(j){"
          "if(!j.ok){"
            "if(sel){"
              "sel.innerHTML='';"
              "var o=document.createElement('option');"
              "o.value='';"
              "o.textContent='Scan error';"
              "sel.appendChild(o);"
            "}"
            "return;"
          "}"
          "fillSsidDropdown(j.nets||[]);"
        "})"
        ".catch(function(e){"
          "if(sel){"
            "sel.innerHTML='';"
            "var o=document.createElement('option');"
            "o.value='';"
            "o.textContent='Scan failed';"
            "sel.appendChild(o);"
          "}"
        "});"
    "}"
    "document.getElementById('ssidList').addEventListener('change', function(){"
      "var v=this.value||'';"
      "var inpt=document.getElementById('ssidInput');"
      "if(inpt && v.length>0){ inpt.value=v; }"
    "});"
    "</script>";

  html += htmlFooter();
  return html;
}

String html_pageNetSaved() {
  String html = htmlHeader("Network saved");
  html +=
    "<div class='wrap'><div class='card'>"
    "<h1>theOPENhandy</h1>"
    "<p>Network configuration saved.</p>"
    "<p>Rebooting...</p>"
    "</div></div>";
  html += htmlFooter();
  return html;
}