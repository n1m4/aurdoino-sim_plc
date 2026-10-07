#include "web_config.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cstring>

#include "board_config.h"
#include "config_store.h"

static WebServer s_server(80);
static GatewayConfig s_editCfg;
static GatewayConfig s_savedPasswordSource;
static uint32_t s_configStartedMs = 0;

static void copyStr(char *dst, size_t dstLen, const char *src) {
  if (!dst || dstLen == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fa" dir="rtl">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>PLC Gateway Setup</title>
<style>
body{font-family:sans-serif;margin:16px;max-width:960px}
label{display:block;margin-top:8px;font-weight:600}
input,select{width:100%;padding:8px;margin-top:4px;box-sizing:border-box}
table{width:100%;border-collapse:collapse;margin-top:12px}
th,td{border:1px solid #ccc;padding:6px;text-align:right}
button{margin-top:12px;padding:10px 16px;cursor:pointer}
.msg{padding:10px;margin:10px 0;border-radius:4px}
.ok{background:#e6ffe6}.err{background:#ffe6e6}
</style>
</head>
<body>
<h1>Gateway CONFIG</h1>
<p>WiFi AP: PLC-GW-Setup — http://192.168.4.1</p>
<div id="msg"></div>

<label>Profile name</label>
<input id="profile_name"/>

<h2>WiFi</h2>
<label>SSID</label><input id="wifi_ssid"/>
<label>Password (leave blank to keep saved)</label><input id="wifi_password" type="password"/>

<h2>PLC (Modbus TCP)</h2>
<label>Host (IPv4)</label><input id="plc_host"/>
<label>Port</label><input id="plc_port" type="number"/>
<label>Unit ID</label><input id="plc_unit_id" type="number"/>
<label>Poll interval (ms)</label><input id="poll_interval_ms" type="number"/>

<h2>Tags</h2>
<table>
<thead><tr><th>id</th><th>label</th><th>kind</th><th>datatype</th><th>address</th><th>scale</th><th></th></tr></thead>
<tbody id="tags_body"></tbody>
</table>
<button type="button" onclick="addTag()">+ Add tag</button>
<button type="button" onclick="saveConfig()">Save &amp; Reboot to WORK</button>

<script>
function showMsg(t,ok){const m=document.getElementById('msg');m.className='msg '+(ok?'ok':'err');m.textContent=t;}
function addTagRow(t={}){const tb=document.getElementById('tags_body');const tr=document.createElement('tr');
tr.innerHTML=`<td><input class="t_id" value="${t.id||''}"/></td>
<td><input class="t_label" value="${t.label||''}"/></td>
<td><select class="t_kind"><option value="holding">holding</option><option value="coil">coil</option></select></td>
<td><select class="t_dtype"><option value="bool">bool</option><option value="int16">int16</option><option value="uint16">uint16</option><option value="int32">int32</option><option value="float32">float32</option></select></td>
<td><input class="t_addr" type="number" value="${t.address??0}"/></td>
<td><input class="t_scale" type="number" step="any" value="${t.scale??''}"/></td>
<td><button type="button" onclick="this.closest('tr').remove()">Del</button></td>`;
tb.appendChild(tr);tr.querySelector('.t_kind').value=t.kind||'holding';tr.querySelector('.t_dtype').value=t.datatype||'uint16';}
function addTag(){addTagRow({});}
async function loadConfig(){const r=await fetch('/api/config');const j=await r.json();
document.getElementById('profile_name').value=j.profile_name||'';
document.getElementById('wifi_ssid').value=j.wifi?.ssid||'';
document.getElementById('wifi_password').value='';
document.getElementById('plc_host').value=j.plc?.host||'';
document.getElementById('plc_port').value=j.plc?.port||5020;
document.getElementById('plc_unit_id').value=j.plc?.unit_id||1;
document.getElementById('poll_interval_ms').value=j.poll?.interval_ms||1000;
document.getElementById('tags_body').innerHTML='';
(j.tags||[]).forEach(addTagRow);}
function collectPayload(){const tags=[];document.querySelectorAll('#tags_body tr').forEach(tr=>{
const id=tr.querySelector('.t_id').value.trim();if(!id)return;
const scale=tr.querySelector('.t_scale').value;const kind=tr.querySelector('.t_kind').value;
const o={id,label:tr.querySelector('.t_label').value.trim(),kind,address:parseInt(tr.querySelector('.t_addr').value||0)};
if(kind==='holding')o.datatype=tr.querySelector('.t_dtype').value;
if(scale!=='')o.scale=parseFloat(scale);tags.push(o);});
return{schema_version:1,profile_name:document.getElementById('profile_name').value.trim()||'default',
wifi:{ssid:document.getElementById('wifi_ssid').value,password:document.getElementById('wifi_password').value},
plc:{protocol:'modbus_tcp',host:document.getElementById('plc_host').value,
port:parseInt(document.getElementById('plc_port').value||5020),
unit_id:parseInt(document.getElementById('plc_unit_id').value||1)},
poll:{interval_ms:parseInt(document.getElementById('poll_interval_ms').value||1000)},tags};}
async function saveConfig(){showMsg('Saving...',true);
const r=await fetch('/api/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(collectPayload())});
const j=await r.json();showMsg(j.message||j.error,!j.error);if(j.ok)setTimeout(()=>location.reload(),3000);}
loadConfig().catch(e=>showMsg(String(e),false));
</script>
</body>
</html>
)rawliteral";

static void handleRoot() {
  s_server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void handleGetConfig() {
  String json;
  configStoreSerialize(s_editCfg, json, true);
  s_server.send(200, "application/json", json);
}

static void mergeWifiPasswordFromSaved(GatewayConfig &incoming) {
  const char *pw = incoming.wifi_password;
  if (pw[0] == '\0' || strcmp(pw, "********") == 0) {
    copyStr(incoming.wifi_password, sizeof(incoming.wifi_password),
            s_savedPasswordSource.wifi_password);
  }
}

static void handleSave() {
  if (!s_server.hasArg("plain")) {
    s_server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
    return;
  }
  String body = s_server.arg("plain");
  char err[160];
  if (!configStoreParseJsonNoValidate(body.c_str(), body.length(), s_editCfg, err, sizeof(err))) {
    String resp = String("{\"ok\":false,\"error\":\"") + err + "\"}";
    s_server.send(400, "application/json", resp);
    return;
  }
  mergeWifiPasswordFromSaved(s_editCfg);
  if (!configStoreSave(s_editCfg, err, sizeof(err))) {
    String resp = String("{\"ok\":false,\"error\":\"") + err + "\"}";
    s_server.send(400, "application/json", resp);
    return;
  }
  s_server.send(200, "application/json",
                "{\"ok\":true,\"message\":\"Saved. Rebooting to WORK...\"}");
  delay(500);
  ESP.restart();
}

static void handleRebootWork() {
  s_server.send(200, "application/json", "{\"ok\":true,\"message\":\"Rebooting...\"}");
  delay(300);
  ESP.restart();
}

bool webConfigBegin() {
  configStoreLoadForEdit(s_editCfg);
  configStoreLoadForEdit(s_savedPasswordSource);

  s_configStartedMs = millis();
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("AP start failed");
    return false;
  }
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  s_server.on("/", HTTP_GET, handleRoot);
  s_server.on("/api/config", HTTP_GET, handleGetConfig);
  s_server.on("/api/save", HTTP_POST, handleSave);
  s_server.on("/api/reboot", HTTP_POST, handleRebootWork);
  s_server.begin();
  return true;
}

void webConfigLoop() {
  s_server.handleClient();
}

bool webConfigTimedOut() {
  return (millis() - s_configStartedMs) > CONFIG_TIMEOUT_MS;
}

void webConfigRebootToWork() {
  ESP.restart();
}
