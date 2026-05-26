#include "wifi_provision.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>

static Preferences prefs;
static WebServer *captiveServer = nullptr;
static ProvisionState state = ProvisionState::IDLE;
static String apSSID;
static String savedSSID;
static String savedPass;

static const char PROVISION_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Vibe Pi Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,system-ui,sans-serif;background:#0a0a0a;color:#e0e0e0;
  display:flex;justify-content:center;align-items:center;min-height:100vh;padding:20px}
.card{background:#1a1a1a;border-radius:16px;padding:32px;max-width:380px;width:100%;
  border:1px solid #333}
h1{font-size:24px;margin-bottom:8px;color:#fff}
.sub{color:#888;margin-bottom:24px;font-size:14px}
label{display:block;font-size:13px;color:#aaa;margin-bottom:6px;margin-top:16px}
input{width:100%;padding:12px;border-radius:8px;border:1px solid #444;background:#111;
  color:#fff;font-size:15px;outline:none;transition:border-color .2s}
input:focus{border-color:#64B5F6}
.btn{width:100%;padding:14px;border:none;border-radius:10px;background:#64B5F6;color:#000;
  font-size:16px;font-weight:600;cursor:pointer;margin-top:24px;transition:opacity .2s}
.btn:hover{opacity:.9}
.btn:disabled{opacity:.4;cursor:wait}
.status{margin-top:16px;padding:12px;border-radius:8px;font-size:13px;display:none}
.status.ok{display:block;background:#1a3a1a;color:#4caf50;border:1px solid #2d5a2d}
.status.err{display:block;background:#3a1a1a;color:#ef5350;border:1px solid #5a2d2d}
.status.wait{display:block;background:#1a2a3a;color:#64B5F6;border:1px solid #2d3d5a}
.footer{text-align:center;margin-top:20px;font-size:12px;color:#555}
</style>
</head>
<body>
<div class="card">
  <h1>&#x1f4e1; Vibe Pi Setup</h1>
  <p class="sub">Connect your device to WiFi to get started.</p>
  <form id="f" onsubmit="return doSubmit()">
    <label>WiFi Network</label>
    <input id="ssid" name="ssid" placeholder="Enter WiFi name" required>
    <label>Password</label>
    <input id="pass" name="pass" type="password" placeholder="Enter WiFi password">
    <button class="btn" type="submit" id="btn">Connect</button>
  </form>
  <div id="st" class="status"></div>
  <p class="footer">Vibe Pi v%FW_VERSION%</p>
</div>
<script>
function doSubmit(){
  var b=document.getElementById('btn'),s=document.getElementById('st');
  b.disabled=true;b.textContent='Connecting...';
  s.className='status wait';s.textContent='Connecting to WiFi...';
  fetch('/connect',{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'ssid='+encodeURIComponent(document.getElementById('ssid').value)+
         '&pass='+encodeURIComponent(document.getElementById('pass').value)
  }).then(r=>r.json()).then(d=>{
    if(d.ok){s.className='status ok';s.textContent='Connected! IP: '+d.ip+'. Device will restart...';}
    else{s.className='status err';s.textContent='Failed: '+d.error;b.disabled=false;b.textContent='Connect';}
  }).catch(e=>{s.className='status err';s.textContent='Error: '+e;b.disabled=false;b.textContent='Connect';});
  return false;
}
</script>
</body>
</html>
)rawhtml";

static void handleRoot() {
    String html = FPSTR(PROVISION_HTML);
    html.replace("%FW_VERSION%", FW_VERSION);
    captiveServer->send(200, "text/html", html);
}

static void handleConnect() {
    String ssid = captiveServer->arg("ssid");
    String pass = captiveServer->arg("pass");

    if (ssid.isEmpty()) {
        captiveServer->send(200, "application/json", "{\"ok\":false,\"error\":\"SSID required\"}");
        return;
    }

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        prefs.begin(NVS_NAMESPACE, false);
        prefs.putString(NVS_KEY_WIFI_SSID, ssid);
        prefs.putString(NVS_KEY_WIFI_PASS, pass);
        prefs.putBool(NVS_KEY_SETUP_DONE, true);
        prefs.end();

        String ip = WiFi.localIP().toString();
        String resp = "{\"ok\":true,\"ip\":\"" + ip + "\"}";
        captiveServer->send(200, "application/json", resp);

        delay(2000);
        ESP.restart();
    } else {
        WiFi.disconnect();
        WiFi.mode(WIFI_AP);
        captiveServer->send(200, "application/json", "{\"ok\":false,\"error\":\"Could not connect. Check credentials.\"}");
        state = ProvisionState::AP_ACTIVE;
    }
}

static void handleNotFound() {
    captiveServer->sendHeader("Location", "/");
    captiveServer->send(302, "text/plain", "");
}

void provision_init() {
    prefs.begin(NVS_NAMESPACE, true);
    savedSSID = prefs.getString(NVS_KEY_WIFI_SSID, "");
    savedPass = prefs.getString(NVS_KEY_WIFI_PASS, "");
    prefs.end();

    uint8_t mac[6];
    WiFi.macAddress(mac);
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%02x%02x%02x", mac[3], mac[4], mac[5]);
    apSSID = String(AP_SSID_PREFIX) + suffix;
}

void provision_start_ap() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID.c_str());
    delay(200);

    if (!captiveServer) {
        captiveServer = new WebServer(80);
        captiveServer->on("/", handleRoot);
        captiveServer->on("/connect", HTTP_POST, handleConnect);
        captiveServer->onNotFound(handleNotFound);
        captiveServer->begin();
    }

    state = ProvisionState::AP_ACTIVE;
    Serial.printf("[Provision] AP started: %s (http://%s)\n",
                  apSSID.c_str(), WiFi.softAPIP().toString().c_str());
}

void provision_loop() {
    if (captiveServer && state == ProvisionState::AP_ACTIVE) {
        captiveServer->handleClient();
    }
}

bool provision_is_configured() {
    return !savedSSID.isEmpty();
}

bool provision_connect_saved() {
    if (savedSSID.isEmpty()) return false;

    state = ProvisionState::CONNECTING;
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
            state = ProvisionState::FAILED;
            return false;
        }
        delay(100);
    }

    state = ProvisionState::CONNECTED;
    return true;
}

ProvisionState provision_get_state() {
    return state;
}

String provision_get_ap_ssid() {
    return apSSID;
}

String provision_get_ip() {
    return WiFi.localIP().toString();
}

void provision_reset() {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    savedSSID = "";
    savedPass = "";
    state = ProvisionState::IDLE;
    Serial.println("[Provision] Settings cleared");
}
