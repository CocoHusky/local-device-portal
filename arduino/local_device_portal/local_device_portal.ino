// Wifi Device provisioning — ESP32 Arduino
// v8: captive-portal-safe setup, mDNS dashboard, cleaner product flow
//
// Requires:
//   esp32 by Espressif Systems v3.1.0+
//   Board: ESP32C6 Dev Module or Seeed XIAO ESP32C6

#include "portal_config.h"
#include "portal_utils.h"
#include "wifi_portal.h"
#include "portal_steps.h"
#include "portal_status_card.h"
#include "portal_actions.h"
#include "portal_forms.h"

// ── AP / STA ──────────────────────────────────────────────────────────────────

void startAP() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);

  bool ok = WiFi.softAP(setupApSsid.c_str(), AP_PASS);
  apRunning = ok;

  if (ok) {
    dns.start(DNS_PORT, "*", AP_IP);

    Serial.println();
    Serial.println("AP ON");
    Serial.print("SSID: ");
    Serial.println(setupApSsid);
    Serial.println("Setup URL: http://192.168.4.1/");
  } else {
    Serial.println("AP FAILED");
  }
}

void stopAP() {
  if (!apRunning) return;

  Serial.println("Stopping setup AP for dashboard handoff");
  dns.stop();
  WiFi.softAPdisconnect(true);
  apRunning = false;
}

bool connectSTA(const String& ssid, const String& pass, unsigned long timeoutMs) {
  Serial.println();
  Serial.print("Connecting to local Wi-Fi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long start = millis();

  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println();
      Serial.print("Connected. Local IP: ");
      Serial.println(WiFi.localIP());

      startMDNS();
      return true;
    }

    if (apRunning) {
      dns.processNextRequest();
    }

    server.handleClient();

    delay(100);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Connect failed");
  return false;
}

void updateOnlineData(bool force = false) {
  (void)force;

  if (WiFi.status() != WL_CONNECTED) {
    onlineOk = false;
    onlineSummary = "Local Wi-Fi is not connected.";
  } else {
    onlineOk = false;
    onlineSummary = "Offline dashboard mode";
  }

  onlineSource = "local";
  onlineTimezone = "UTC";
  onlineUpdatedAt = millis();
}

// ── Shared HTML ───────────────────────────────────────────────────────────────

String pageHead(const String& title) {
  return String(R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>)HTML") + htmlEscape(title) + R"HTML(</title>
<style>
:root{
  --bg:#050b16;
  --surface:#0d1727;
  --surface2:#101d32;
  --border:#1b2d4a;
  --border2:#274262;
  --text:#d8e6f7;
  --muted:#7890ad;
  --muted2:#9fb0c8;
  --accent:#00b8f0;
  --accent2:#46d8ff;
  --green:#00d97e;
  --red:#ff5c5c;
}
*{box-sizing:border-box;margin:0;padding:0}
html{-webkit-text-size-adjust:100%}
body{
  font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;
  background:
    radial-gradient(circle at top left, rgba(0,184,240,.14), transparent 30rem),
    radial-gradient(circle at bottom right, rgba(0,217,126,.07), transparent 28rem),
    var(--bg);
  color:var(--text);
  min-height:100vh;
  padding:1.15rem .9rem 3rem;
}
.wrap{width:100%;max-width:480px;margin:0 auto}
.header{
  display:flex;
  align-items:center;
  gap:.85rem;
  margin-bottom:1rem;
}
.logo{
  width:42px;
  height:42px;
  border-radius:15px;
  border:1px solid rgba(0,184,240,.55);
  background:linear-gradient(135deg, rgba(0,184,240,.16), rgba(0,217,126,.08));
  display:flex;
  align-items:center;
  justify-content:center;
  color:var(--accent2);
  font-weight:900;
  letter-spacing:.02em;
  box-shadow:0 0 24px rgba(0,184,240,.14);
}
.header h1{font-size:1.12rem;color:#fff;letter-spacing:-.02em}
.header p{font-size:.77rem;color:var(--muted);margin-top:.1rem}
.card{
  background:linear-gradient(180deg, rgba(16,29,50,.96), rgba(13,23,39,.96));
  border:1px solid var(--border);
  border-radius:17px;
  padding:1.1rem;
  margin-bottom:1rem;
  box-shadow:0 18px 50px rgba(0,0,0,.22);
}
.status-card{
  border-color:rgba(0,184,240,.35);
  background:linear-gradient(180deg, rgba(13,31,50,.98), rgba(10,18,32,.98));
}
.card-title{
  font-size:.67rem;
  font-weight:900;
  text-transform:uppercase;
  letter-spacing:.12em;
  color:var(--muted);
  margin-bottom:.82rem;
}
h2{font-size:1.08rem;color:#fff;margin-bottom:.45rem;letter-spacing:-.015em}
p{font-size:.85rem;line-height:1.5;color:var(--muted);margin-bottom:.75rem}
a{color:var(--accent2)}
.btn,button{
  display:block;
  width:100%;
  padding:.86rem;
  border:0;
  border-radius:11px;
  font:inherit;
  font-size:.92rem;
  font-weight:850;
  cursor:pointer;
  text-align:center;
  text-decoration:none;
  margin:.45rem 0;
}
.btn-primary{
  background:linear-gradient(135deg,var(--accent),var(--accent2));
  color:#001018;
  box-shadow:0 10px 24px rgba(0,184,240,.18);
}
.btn-ghost{
  background:rgba(255,255,255,.02);
  color:var(--muted2);
  border:1px solid var(--border);
}
.btn-danger{
  background:rgba(255,92,92,.08);
  color:var(--red);
  border:1px solid rgba(255,92,92,.24);
}
.btn-small{
  padding:.65rem;
  font-size:.8rem;
  border-radius:10px;
}
button:active,.btn:active{transform:scale(.99);opacity:.84}
input{
  width:100%;
  padding:.9rem;
  margin:.4rem 0 .8rem;
  background:#07101f;
  color:var(--text);
  border:1.5px solid var(--border);
  border-radius:11px;
  font:inherit;
  outline:none;
}
input:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(0,184,240,.12)}
.pass-wrap{position:relative}
.pass-wrap input{padding-right:3.25rem}
.eye-btn{
  position:absolute;
  right:.45rem;
  top:.48rem;
  width:2.4rem;
  height:2.4rem;
  margin:0;
  padding:.4rem;
  border-radius:9px;
  background:transparent;
  color:#8fa3bd;
  border:1px solid transparent;
}
.eye-btn svg{
  width:21px;
  height:21px;
  display:block;
  stroke:#8fa3bd;
}
.eye-btn .eye-off{display:none}
.eye-btn.showing .eye-open{display:none}
.eye-btn.showing .eye-off{display:block}
.eye-btn:active{transform:none}
.steps{
  display:flex;
  align-items:center;
  margin-bottom:1rem;
}
.step-dot{
  width:28px;
  height:28px;
  border-radius:50%;
  display:flex;
  align-items:center;
  justify-content:center;
  border:2px solid var(--border2);
  color:var(--muted);
  font-size:.75rem;
  font-weight:900;
}
.step-dot.active{border-color:var(--accent);color:var(--accent);background:rgba(0,184,240,.08)}
.step-dot.done{border-color:var(--green);background:var(--green);color:#00140b}
.step-line{flex:1;height:2px;background:var(--border2)}
.step-line.done{background:var(--green)}
.status-grid{
  display:grid;
  grid-template-columns:1fr;
  gap:.38rem;
  font-size:.83rem;
}
.status-row{
  display:flex;
  justify-content:space-between;
  gap:.8rem;
  padding:.38rem 0;
  border-bottom:1px solid rgba(255,255,255,.055);
}
.status-row:last-child{border-bottom:0}
.status-label{color:var(--muted)}
.status-value{color:#fff;font-weight:750;text-align:right;word-break:break-word}
.good{color:var(--green)!important}
.bad{color:var(--red)!important}
.net-form{margin:.48rem 0}
.net-item{
  width:100%;
  background:#07101f;
  border:1.5px solid var(--border);
  color:var(--text);
  text-align:left;
  padding:.86rem .95rem;
  border-radius:13px;
}
.net-item:active{border-color:var(--accent)}
.net-name{
  display:flex;
  align-items:center;
  justify-content:space-between;
  gap:.7rem;
  font-size:.93rem;
  font-weight:850;
  color:#fff;
  margin-bottom:.42rem;
  word-break:break-word;
}
.lock{
  flex:0 0 auto;
  min-width:1.25rem;
  text-align:right;
  color:var(--muted2);
}
.net-meta{
  display:flex;
  justify-content:space-between;
  gap:.7rem;
  font-size:.76rem;
  color:var(--muted);
  margin-bottom:.5rem;
}
.bar{
  width:100%;
  height:10px;
  background:#26344f;
  border-radius:999px;
  overflow:hidden;
}
.fill{
  height:100%;
  background:linear-gradient(90deg,var(--accent),var(--green));
  border-radius:999px;
}
.msg{
  border:1px solid var(--border);
  border-radius:12px;
  padding:.82rem;
  background:#081325;
  color:var(--muted2);
}
.ok{border-color:#0d4b33;color:var(--green);background:#071c15}
.err{border-color:#4b1414;color:var(--red);background:#1d0808}
.info{border-color:rgba(0,184,240,.28);color:#bfefff;background:#081b2d}
.ip{
  font-size:1.35rem;
  color:var(--green);
  font-weight:950;
  font-variant-numeric:tabular-nums;
  margin:.6rem 0;
  word-break:break-all;
}
.hostname{
  font-size:1.15rem;
  color:#fff;
  font-weight:900;
  word-break:break-all;
  margin:.5rem 0;
}
.small{font-size:.76rem;color:var(--muted)}
.bottom-actions{
  opacity:.85;
  margin-top:.3rem;
}
hr{border:0;border-top:1px solid var(--border);margin:1rem 0}
.spinner{
  width:52px;
  height:52px;
  border-radius:50%;
  border:4px solid rgba(255,255,255,.08);
  border-top-color:var(--accent);
  border-right-color:var(--green);
  animation:spin .85s linear infinite;
  margin:1rem auto;
}
@keyframes spin{to{transform:rotate(360deg)}}
.countdown{
  font-size:2.35rem;
  font-weight:950;
  color:#fff;
  text-align:center;
  font-variant-numeric:tabular-nums;
  margin:.4rem 0 .8rem;
}
</style>
</head>
<body>
<div class="wrap">
<div class="header">
  <div class="logo">WD</div>
  <div>
    <h1>Wifi Device</h1>
    <p>Wi-Fi setup</p>
  </div>
</div>
)HTML";
}

String pageFoot() {
  return R"HTML(
</div>
</body>
</html>
)HTML";
}

// ── Pages ─────────────────────────────────────────────────────────────────────

String setupPage(const String& message = "") {
  String h = pageHead("Wifi Device");

  h += statusCard();
  h += stepsBar(1);

  h += "<div class='card'>";
  h += "<div class='card-title'>Step 1 of 3</div>";
  h += "<h2>Choose Wi-Fi</h2>";
  h += "<p>Pick the Wi-Fi network for this sensor.</p>";

  h += "<form method='GET' action='/scan'>";
  h += "<button class='btn-primary' type='submit'>Scan networks</button>";
  h += "</form>";

  h += "<a class='btn btn-ghost' href='/manual'>Type network manually</a>";

  if (message.length()) {
    h += "<p class='msg info'>" + htmlEscape(message) + "</p>";
  }

  h += "</div>";

  h += bottomActions();
  h += pageFoot();
  return h;
}

String manualPage(const String& msg = "") {
  String h = pageHead("Manual Wi-Fi");

  h += statusCard();
  h += stepsBar(2);

  h += "<div class='card'>";
  h += "<div class='card-title'>Step 2 of 3</div>";
  h += "<h2>Enter Wi-Fi</h2>";
  h += "<p>Use this if your network is hidden.</p>";

  if (msg.length()) h += "<p class='msg err'>" + htmlEscape(msg) + "</p>";

  h += "<form method='POST' action='/connect'>";
  h += "<input name='ssid' placeholder='Wi-Fi network name' autocomplete='off'>";

  h += "<div class='pass-wrap'>";
  h += "<input id='pass' name='pass' type='password' placeholder='Wi-Fi password' autocomplete='current-password'>";
  h += eyeButton();
  h += "</div>";

  h += "<button class='btn-primary' type='submit'>Connect</button>";
  h += "</form>";

  h += "<a class='btn btn-ghost' href='/'>Back</a>";
  h += "</div>";

  h += passwordToggleScript();
  h += bottomActions();
  h += pageFoot();
  return h;
}

String scanPage() {
  Serial.println();
  Serial.println("Scanning Wi-Fi networks...");

  WiFi.mode(WIFI_AP_STA);

  int n = WiFi.scanNetworks(false, false);

  Serial.print("Scan found: ");
  Serial.println(n);

  String h = pageHead("Scan Results");

  h += statusCard();
  h += stepsBar(1);

  h += "<div class='card'>";
  h += "<div class='card-title'>Step 1 of 3</div>";
  h += "<h2>Choose Wi-Fi</h2>";

  if (n <= 0) {
    h += "<p class='msg err'>No networks found. Try again or enter it manually.</p>";
    h += "<form method='GET' action='/scan'><button class='btn-primary' type='submit'>Scan again</button></form>";
    h += "<a class='btn btn-ghost' href='/manual'>Type network manually</a>";
    h += "</div>";
    WiFi.scanDelete();
    h += bottomActions();
    h += pageFoot();
    return h;
  }

  h += "<p>Tap your network.</p>";

  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    ssid.trim();
    if (!ssid.length()) continue;

    int rssi = WiFi.RSSI(i);
    bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    int pct = signalPercent(rssi);

    Serial.print(i);
    Serial.print(": ");
    Serial.print(ssid);
    Serial.print(" RSSI=");
    Serial.print(rssi);
    Serial.print(" secure=");
    Serial.println(secure ? "yes" : "no");

    h += "<form class='net-form' method='GET' action='/pick'>";
    h += "<input type='hidden' name='ssid' value='" + htmlEscape(ssid) + "'>";

    h += "<button class='net-item' type='submit'>";
    h += "<div class='net-name'>";
    h += "<span>" + htmlEscape(ssid) + "</span>";
    h += "<span class='lock'>";
    h += secure ? "&#128274;" : "&nbsp;";
    h += "</span>";
    h += "</div>";

    h += "<div class='net-meta'>";
    h += "<span>" + signalLabel(rssi) + "</span>";
    h += "<span>" + String(rssi) + " dBm</span>";
    h += "</div>";

    h += "<div class='bar'><div class='fill' style='width:" + String(pct) + "%'></div></div>";
    h += "</button>";
    h += "</form>";
  }

  h += "<hr>";
  h += "<form method='GET' action='/scan'><button class='btn-ghost' type='submit'>Scan again</button></form>";
  h += "<a class='btn btn-ghost' href='/manual'>Type network manually</a>";
  h += "</div>";

  WiFi.scanDelete();

  h += bottomActions();
  h += pageFoot();
  return h;
}

String pickPage(const String& ssid, const String& error = "") {
  String h = pageHead("Enter Password");

  h += statusCard();
  h += stepsBar(2);

  h += "<div class='card'>";
  h += "<div class='card-title'>Step 2 of 3</div>";
  h += "<h2>Enter password</h2>";
  h += "<p><b style='color:#fff'>" + htmlEscape(ssid) + "</b></p>";

  if (error.length()) {
    h += "<p class='msg err'>" + htmlEscape(error) + "</p>";
  }

  h += "<form method='POST' action='/connect'>";
  h += "<input type='hidden' name='ssid' value='" + htmlEscape(ssid) + "'>";

  h += "<div class='pass-wrap'>";
  h += "<input id='pass' name='pass' type='password' placeholder='Wi-Fi password' autocomplete='current-password'>";
  h += eyeButton();
  h += "</div>";

  h += "<button class='btn-primary' type='submit'>Connect</button>";
  h += "</form>";

  h += "<a class='btn btn-ghost' href='/scan'>Choose different network</a>";
  h += "</div>";

  h += passwordToggleScript();
  h += bottomActions();
  h += pageFoot();
  return h;
}

String setupApUrl(const String& path = "") {
  return "http://" + AP_IP.toString() + path;
}

String successPage(const String& ssid, const String& ip) {
  String h = pageHead("Connected");

  h += statusCard();
  h += stepsBar(3);

  h += "<div class='card'>";
  h += "<div class='card-title'>Step 3 of 3</div>";
  h += "<h2>Connected</h2>";
  h += "<p>The device joined <b style='color:#fff'>" + htmlEscape(ssid) + "</b>.</p>";

  h += "<p>Dashboard:</p>";
  h += "<div class='hostname'>" + htmlEscape(dashboardUrl) + "</div>";
  h += "<p class='small'>Backup IP: " + htmlEscape(ip) + "</p>";

  h += "<p>Copy the dashboard address, then close the setup Wi-Fi.</p>";
  h += "<a class='btn btn-primary' href='" + htmlEscape(setupApUrl("/handoff")) + "'>Finish setup and close setup Wi-Fi</a>";
  h += "</div>";

  h += bottomActions();
  h += pageFoot();
  return h;
}

String handoffPage(const String& ip) {
  String h = pageHead("Finish Setup");

  h += "<div class='card'>";
  h += "<div class='card-title'>Finish setup</div>";
  h += "<h2>Setup Wi-Fi closing</h2>";

  h += "<p class='msg info'>The setup Wi-Fi is turning off. Captive portal browsers can block automatic dashboard redirects, so this page will not redirect.</p>";
  h += String("<p>Copy the dashboard URL below. When ") + htmlEscape(setupApSsid) + " disconnects, reconnect to your normal Wi-Fi and paste the URL into a normal browser tab.</p>";

  h += "<p>Dashboard:</p>";
  h += "<input id='dashUrl' readonly value='" + htmlEscape(dashboardUrl) + "' ";
  h += "onclick='this.select()' style='font-size:.9rem;font-weight:800;color:#fff'>";
  h += "<button class='btn-primary' type='button' onclick='copyDashUrl()'>Copy dashboard URL</button>";
  h += "<p class='small'>Backup IP: " + htmlEscape(ip) + "</p>";
  h += "</div>";

  h += "<script>";
  h += "function copyDashUrl(){var i=document.getElementById('dashUrl');if(!i)return;i.select();i.setSelectionRange(0,9999);if(navigator.clipboard){navigator.clipboard.writeText(i.value);}else{document.execCommand('copy');}}";
  h += "</script>";

  h += pageFoot();
  return h;
}

String dashboardPage() {
  String ip = WiFi.localIP().toString();

  String h = pageHead("Wifi Device");

  h += statusCard();

  h += "<div class='card'>";
  h += "<div class='card-title'>Dashboard</div>";
  h += "<h2>Device ready</h2>";
  h += "<p class='msg ok'>Connected on local Wi-Fi.</p>";
  h += "<p>Dashboard: <b style='color:#fff'>" + htmlEscape(dashboardUrl) + "</b></p>";
  h += "<p class='small'>Backup IP: " + htmlEscape(ip) + "</p>";
  h += "</div>";

  h += "<div class='card'>";
  h += "<div class='card-title'>Device Data</div>";
  h += "<div class='status-grid' id='deviceData'>";
  h += "<div class='status-row'><span class='status-label'>Uptime</span><span class='status-value'>loading...</span></div>";
  h += "<div class='status-row'><span class='status-label'>RSSI</span><span class='status-value'>loading...</span></div>";
  h += "</div>";
  h += "</div>";

  h += "<div class='card'>";
  h += "<div class='card-title'>Online Data</div>";
  h += "<div id='onlineData'>";
  h += "<p class='msg info'>Offline dashboard mode.</p>";
  h += "</div>";
  h += "</div>";

  h += "<script>";
  h += "function setRows(id,rows){var el=document.getElementById(id); if(!el)return; el.innerHTML=rows.map(function(r){return '<div class=\"status-row\"><span class=\"status-label\">'+r[0]+'</span><span class=\"status-value\">'+r[1]+'</span></div>';}).join('');}";
  h += "function onlineHtml(cls,rows){return '<p class=\"msg '+cls+'\">'+rows[0]+'</p><div class=\"status-grid\">'+rows.slice(1).map(function(r){return '<div class=\"status-row\"><span class=\"status-label\">'+r[0]+'</span><span class=\"status-value\">'+r[1]+'</span></div>';}).join('')+'</div>';}";
  h += "function setOnline(cls,rows){var o=document.getElementById('onlineData'); if(o)o.innerHTML=onlineHtml(cls,rows);}";
  h += "function localOnlineRows(d){return [d.online_summary||'Offline dashboard mode',['Internet time','not available'],['Timezone',d.online_timezone||'UTC']];}";
  h += "function refreshData(){fetch('/data',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){";
  h += "setRows('deviceData', [['Uptime',d.uptime],['RSSI',d.rssi+' dBm']]);";
  h += "setOnline('info',localOnlineRows(d));";
  h += "}).catch(function(){setOnline('err',['Dashboard could not read live data from the device.',['Internet time','not available']]);});}";
  h += "refreshData(); setInterval(refreshData,10000);";
  h += "</script>";

  h += bottomActions();
  h += pageFoot();
  return h;
}

String resetPage() {
  String h = pageHead("Resetting Wi-Fi");

  h += "<div class='card'>";
  h += "<div class='card-title'>Reset Wi-Fi</div>";
  h += "<h2>Saved Wi-Fi cleared</h2>";
  h += "<div class='spinner'></div>";
  h += String("<p class='msg info'>The device is restarting. Reconnect to <b>") + htmlEscape(setupApSsid) + "</b>, then open <b>http://192.168.4.1/</b>.</p>";
  h += "</div>";

  h += pageFoot();
  return h;
}

// ── HTTP handlers ─────────────────────────────────────────────────────────────

void sendNoCache() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
}

bool requestIsFromLocalDashboardHost() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String host = server.hostHeader();
  host.toLowerCase();

  String staHost = WiFi.localIP().toString();
  String mdnsHost = deviceHost + ".local";

  return host.startsWith(staHost) || host.startsWith(mdnsHost);
}

String currentConnectedSsid() {
  String ssid = WiFi.SSID();
  ssid.trim();

  if (ssid.length()) return ssid;
  if (savedSSID.length()) return savedSSID;
  return "local Wi-Fi";
}

String setupEntryPage() {
  if (WiFi.status() == WL_CONNECTED) {
    return successPage(currentConnectedSsid(), WiFi.localIP().toString());
  }

  return setupPage();
}

void handleRoot() {
  sendNoCache();

  if (requestIsFromLocalDashboardHost()) {
    server.send(200, "text/html; charset=utf-8", dashboardPage());
  } else {
    server.send(200, "text/html; charset=utf-8", setupEntryPage());
  }
}

void handleSetup() {
  sendNoCache();
  server.send(200, "text/html; charset=utf-8", setupEntryPage());
}

void handleDashboard() {
  sendNoCache();

  if (WiFi.status() == WL_CONNECTED) {
    server.send(200, "text/html; charset=utf-8", dashboardPage());
  } else {
    server.send(200, "text/html; charset=utf-8", setupPage("Connect to Wi-Fi first."));
  }
}

void handleManual() {
  sendNoCache();
  server.send(200, "text/html; charset=utf-8", manualPage());
}

void handleScan() {
  sendNoCache();
  server.send(200, "text/html; charset=utf-8", scanPage());
}

void handlePick() {
  sendNoCache();

  String ssid = server.arg("ssid");
  ssid.trim();

  if (!ssid.length()) {
    server.send(200, "text/html; charset=utf-8",
                setupPage("Missing network name. Scan again."));
    return;
  }

  server.send(200, "text/html; charset=utf-8", pickPage(ssid));
}

void handleConnect() {
  sendNoCache();

  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.trim();

  if (!ssid.length()) {
    server.send(200, "text/html; charset=utf-8",
                manualPage("Missing network name."));
    return;
  }

  bool ok = connectSTA(ssid, pass, 20000);

  if (ok) {
    saveCreds(ssid, pass);
    server.send(200, "text/html; charset=utf-8",
                successPage(ssid, WiFi.localIP().toString()));
  } else {
    WiFi.disconnect(false, false);
    startAP();
    server.send(200, "text/html; charset=utf-8",
                pickPage(ssid, "Connection failed. Check the password or move closer to the router."));
  }
}

void handleHandoff() {
  sendNoCache();

  if (WiFi.status() != WL_CONNECTED) {
    server.send(200, "text/html; charset=utf-8",
                setupPage("The device is not connected to local Wi-Fi yet."));
    return;
  }

  String ip = WiFi.localIP().toString();

  handoffPending = true;
  handoffAt = millis() + 1800;

  server.send(200, "text/html; charset=utf-8", handoffPage(ip));
}

void handleForget() {
  sendNoCache();

  forgetCredsOnly();

  server.send(200, "text/html; charset=utf-8", resetPage());

  rebootFlag = true;
  rebootAt = millis() + 1000;
}

void handleStatus() {
  sendNoCache();

  bool connected = WiFi.status() == WL_CONNECTED;

  String json = "{";
  json += "\"ap_ssid\":\"" + jsonEscape(setupApSsid) + "\",";
  json += "\"sta_connected\":" + String(connected ? "true" : "false") + ",";
  json += "\"saved_ssid\":\"" + jsonEscape(savedSSID) + "\",";
  json += "\"local_ip\":\"" + String(connected ? WiFi.localIP().toString() : "") + "\",";
  json += "\"host\":\"" + jsonEscape(deviceHost) + "\",";
  json += "\"dashboard\":\"" + jsonEscape(dashboardUrl) + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleData() {
  sendNoCache();
  updateOnlineData(false);

  bool connected = WiFi.status() == WL_CONNECTED;

  String json = "{";
  json += "\"sta_connected\":" + String(connected ? "true" : "false") + ",";
  json += "\"local_ip\":\"" + String(connected ? WiFi.localIP().toString() : "") + "\",";
  json += "\"rssi\":" + String(connected ? WiFi.RSSI() : 0) + ",";
  json += "\"uptime_ms\":" + String(millis()) + ",";
  json += "\"uptime\":\"" + jsonEscape(formatUptime(millis())) + "\",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"setup_clients\":" + String(apRunning ? WiFi.softAPgetStationNum() : 0) + ",";
  json += "\"online_ok\":" + String(onlineOk ? "true" : "false") + ",";
  json += "\"online_source\":\"" + jsonEscape(onlineSource) + "\",";
  json += "\"online_timezone\":\"" + jsonEscape(onlineTimezone) + "\",";
  json += "\"online_updated_ms\":" + String(onlineUpdatedAt) + ",";
  json += "\"online_summary\":\"" + jsonEscape(onlineSummary) + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleCaptive() {
  sendNoCache();
  server.send(200, "text/html; charset=utf-8", setupEntryPage());
}

// ── Routes ────────────────────────────────────────────────────────────────────

void setupRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/setup", HTTP_GET, handleSetup);
  server.on("/dashboard", HTTP_GET, handleDashboard);
  server.on("/manual", HTTP_GET, handleManual);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/pick", HTTP_GET, handlePick);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/handoff", HTTP_GET, handleHandoff);
  server.on("/handoff", HTTP_POST, handleHandoff);
  server.on("/forget", HTTP_POST, handleForget);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/data", HTTP_GET, handleData);

  // Captive portal probes
  server.on("/generate_204", HTTP_GET, handleCaptive);              // Android
  server.on("/gen_204", HTTP_GET, handleCaptive);                   // Android alt
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptive);       // iOS/macOS
  server.on("/library/test/success.html", HTTP_GET, handleCaptive); // Apple alt
  server.on("/connecttest.txt", HTTP_GET, handleCaptive);           // Windows
  server.on("/ncsi.txt", HTTP_GET, handleCaptive);                  // Windows
  server.on("/redirect", HTTP_GET, handleCaptive);

  server.onNotFound(handleCaptive);

  server.begin();
  Serial.println("HTTP server started");
}

// ── Setup / Loop ──────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(700);

  Serial.println();
  Serial.println("=== Wifi Device provisioning v8 ===");

  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);

  setupHostname();
  clearWifiIfNewUpload();

  setupRoutes();
  startAP();
  loadCreds();

  if (savedSSID.length() > 0) {
    Serial.print("Saved network found: ");
    Serial.println(savedSSID);
    connectSTA(savedSSID, savedPass, 12000);
  } else {
    Serial.println("No saved credentials.");
  }

  Serial.println();
  Serial.println("Setup page:");
  Serial.println("http://192.168.4.1/");
  Serial.println("Future dashboard:");
  Serial.println(dashboardUrl);
}

void loop() {
  if (apRunning) {
    dns.processNextRequest();
  }

  server.handleClient();

  if (handoffPending && millis() >= handoffAt) {
    handoffPending = false;
    stopAP();
  }

  if (rebootFlag && millis() >= rebootAt) {
    Serial.println("Restarting after Wi-Fi reset...");
    ESP.restart();
  }

  static unsigned long lastReconnect = 0;
  if (savedSSID.length() > 0 &&
      WiFi.status() != WL_CONNECTED &&
      millis() - lastReconnect > 30000) {
    lastReconnect = millis();
    Serial.println("STA disconnected. Retrying saved Wi-Fi...");
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());
  }

  if (WiFi.status() == WL_CONNECTED) {
    updateOnlineData(false);
  }

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 5000) {
    lastPrint = millis();

    Serial.print("AP=");
    Serial.print(apRunning ? "on" : "off");

    Serial.print(" STA=");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print(WiFi.localIP());
      Serial.print(" ");
      Serial.print(dashboardUrl);
    } else {
      Serial.print("disconnected");
    }

    Serial.print(" clients=");
    Serial.println(WiFi.softAPgetStationNum());
  }
}
