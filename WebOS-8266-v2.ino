/*
 * WebOS-8266 v2.0
 * Features: App Store, Task Manager, Chat Server (WebSocket), Desktop Widgets
 * Board: ESP8266 (NodeMCU / Wemos D1 Mini)
 * Libraries needed:
 *   - ESP8266WiFi
 *   - ESP8266WebServer
 *   - WebSocketsServer  (by Markus Sattler)
 *   - ArduinoJson        (v6)
 *   - LittleFS           (built-in ESP8266 core)
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// ─── Wi-Fi Config ────────────────────────────────────────────────
const char* WIFI_SSID     = "Isabella";
const char* WIFI_PASSWORD = "warandpeace1867";

// ─── Servers ─────────────────────────────────────────────────────
ESP8266WebServer server(80);
WebSocketsServer wsChat(81);
WebSocketsServer wsSystem(82);

// ─── Chat history ─────────────────────────────────────────────────
#define MAX_CHAT_MESSAGES 50
struct ChatMsg { String user; String text; unsigned long ts; };
ChatMsg chatHistory[MAX_CHAT_MESSAGES];
int chatHead = 0, chatCount = 0;

// ─── WS usernames ─────────────────────────────────────────────────
String wsUsers[5] = {"","","","",""};

// ─── Stats ────────────────────────────────────────────────────────
unsigned long lastStatBroadcast = 0;
float wifiSpeedKbps = 0;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  UTILITY
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
String readFile(const String& path) {
  File f = LittleFS.open(path, "r");
  if (!f) return "";
  String s = f.readString();
  f.close();
  return s;
}
bool writeFile(const String& path, const String& data) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  f.print(data);
  f.close();
  return true;
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  SYSTEM STATS
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
String getSystemStats() {
  StaticJsonDocument<256> doc;
  uint32_t freeRam  = ESP.getFreeHeap();
  uint32_t totalRam = 80 * 1024;
  float    ramPct   = 100.0f - (freeRam * 100.0f / totalRam);
  FSInfo fs;
  LittleFS.info(fs);
  float flashPct = (fs.usedBytes * 100.0f) / fs.totalBytes;
  float cpuTemp  = 45.0f + (ramPct * 0.15f);
  doc["ram_free_kb"]   = freeRam / 1024;
  doc["ram_pct"]       = (int)ramPct;
  doc["flash_used_kb"] = fs.usedBytes / 1024;
  doc["flash_total_kb"]= fs.totalBytes / 1024;
  doc["flash_pct"]     = (int)flashPct;
  doc["wifi_rssi"]     = WiFi.RSSI();
  doc["wifi_kbps"]     = (int)wifiSpeedKbps;
  doc["cpu_temp"]      = cpuTemp;
  doc["uptime_s"]      = millis() / 1000;
  doc["ip"]            = WiFi.localIP().toString();
  String out;
  serializeJson(doc, out);
  return out;
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CHAT WebSocket
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
void pushChatMsg(const String& user, const String& text) {
  chatHistory[chatHead] = {user, text, millis()};
  chatHead = (chatHead + 1) % MAX_CHAT_MESSAGES;
  if (chatCount < MAX_CHAT_MESSAGES) chatCount++;
}
String chatHistoryJson() {
  String out = "[";
  int start = (chatCount < MAX_CHAT_MESSAGES) ? 0 : chatHead;
  for (int i = 0; i < chatCount; i++) {
    int idx = (start + i) % MAX_CHAT_MESSAGES;
    if (i > 0) out += ",";
    out += "{\"user\":\"" + chatHistory[idx].user +
           "\",\"text\":\"" + chatHistory[idx].text +
           "\",\"ts\":" + String(chatHistory[idx].ts) + "}";
  }
  out += "]";
  return out;
}
void broadcastChat(const String& user, const String& text) {
  pushChatMsg(user, text);
  String msg = "{\"type\":\"msg\",\"user\":\"" + user +
               "\",\"text\":\"" + text +
               "\",\"ts\":" + String(millis()) + "}";
  wsChat.broadcastTXT(msg);
}
void onChatWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsUsers[num] = "User" + String(num + 1);
      {
        String historyMsg = "{\"type\":\"history\",\"data\":" + chatHistoryJson() + "}";
        wsChat.sendTXT(num, historyMsg);
      }
      broadcastChat("System", wsUsers[num] + " joined");
      break;
    case WStype_DISCONNECTED:
      broadcastChat("System", wsUsers[num] + " left");
      wsUsers[num] = "";
      break;
    case WStype_TEXT: {
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, payload, length) == DeserializationError::Ok) {
        String msgType = doc["type"] | "msg";
        if (msgType == "setname") {
          String newName = doc["name"] | wsUsers[num];
          broadcastChat("System", wsUsers[num] + " is now " + newName);
          wsUsers[num] = newName;
        } else {
          String text = doc["text"] | "";
          if (text.length()) broadcastChat(wsUsers[num], text);
        }
      }
      break;
    }
    default: break;
  }
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  SYSTEM STATS WebSocket
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
void onSysWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    String sysStats = getSystemStats();
    wsSystem.sendTXT(num, sysStats);
  }
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  APP STORE helpers
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
String defaultAppRegistry() {
  return F(R"([
    {"id":"clock","name":"Clock Widget","version":"1.0","installed":false,"size_kb":2,"desc":"Analog+digital desktop clock"},
    {"id":"weather","name":"Weather Widget","version":"1.1","installed":false,"size_kb":3,"desc":"Local weather via API"},
    {"id":"notes","name":"Notes","version":"1.0","installed":false,"size_kb":4,"desc":"Sticky notes on desktop"},
    {"id":"sysmon","name":"System Monitor","version":"2.0","installed":false,"size_kb":5,"desc":"CPU, RAM, Flash live graphs"},
    {"id":"chat","name":"Chat","version":"1.0","installed":true,"size_kb":6,"desc":"Local network chat"},
    {"id":"files","name":"File Manager","version":"1.0","installed":false,"size_kb":4,"desc":"Browse LittleFS files"},
    {"id":"terminal","name":"Terminal","version":"0.9","installed":false,"size_kb":3,"desc":"Basic serial terminal"}
  ])");
}
void ensureAppRegistry() {
  if (!LittleFS.exists("/apps.json"))
    writeFile("/apps.json", defaultAppRegistry());
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  ROOT — chunked send to avoid heap OOM (blank page fix)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
void handleRoot() {
  if (LittleFS.exists("/index.html")) {
    File f = LittleFS.open("/index.html", "r");
    server.streamFile(f, "text/html");
    f.close();
    return;
  }

  WiFiClient client = server.client();
  client.print(F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"));

  // HEAD + CSS chunk 1
  client.print(F("<!DOCTYPE html><html lang='en'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>WebOS 8266</title><style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    ":root{"
      "--bg:#0f1117;--panel:#1a1d27;--card:#22263a;--accent:#4f8ef7;"
      "--green:#3ecf8e;--red:#ff5f57;--amber:#f5a623;--text:#e8eaf0;"
      "--muted:#7b8099;--border:#2d3250;--radius:10px;"
      "--font:'Segoe UI',system-ui,sans-serif;}"
    "body{background:var(--bg);color:var(--text);font-family:var(--font);height:100vh;overflow:hidden;user-select:none}"
    "#taskbar{position:fixed;bottom:0;left:0;right:0;height:48px;background:rgba(26,29,39,0.95);"
      "backdrop-filter:blur(12px);border-top:1px solid var(--border);"
      "display:flex;align-items:center;padding:0 16px;gap:8px;z-index:1000}"
    ".tb-btn{background:var(--card);border:1px solid var(--border);color:var(--text);"
      "padding:6px 14px;border-radius:6px;cursor:pointer;font-size:13px;transition:.15s}"
    ".tb-btn:hover{background:var(--accent);border-color:var(--accent);color:#fff}"
    ".tb-clock{margin-left:auto;font-size:13px;color:var(--muted)}"
    "#desktop{position:fixed;inset:0 0 48px 0;overflow:hidden}"));

  // CSS chunk 2 — windows
  client.print(F(
    ".win{position:absolute;background:var(--panel);border:1px solid var(--border);"
      "border-radius:var(--radius);box-shadow:0 8px 40px #0008;min-width:340px;"
      "resize:both;overflow:hidden;display:flex;flex-direction:column}"
    ".win-title{background:var(--card);padding:10px 14px;font-size:13px;font-weight:600;"
      "display:flex;align-items:center;gap:8px;cursor:move;flex-shrink:0;border-bottom:1px solid var(--border)}"
    ".win-close{margin-left:auto;width:14px;height:14px;background:var(--red);border-radius:50%;cursor:pointer;border:none}"
    ".win-close:hover{opacity:.8}"
    ".win-body{padding:16px;overflow:auto;flex:1}"
    ".app-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}"
    ".app-card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:14px;transition:.2s}"
    ".app-card:hover{border-color:var(--accent)}"
    ".app-name{font-size:14px;font-weight:600;margin-bottom:4px}"
    ".app-ver{font-size:11px;color:var(--muted);margin-bottom:8px}"
    ".app-desc{font-size:12px;color:var(--muted);margin-bottom:10px;line-height:1.5}"
    ".btn-sm{padding:5px 10px;border-radius:5px;border:none;font-size:12px;cursor:pointer;transition:.15s}"
    ".btn-install{background:var(--accent);color:#fff}.btn-install:hover{opacity:.85}"
    ".btn-remove{background:#2d3250;color:var(--muted)}.btn-remove:hover{background:var(--red);color:#fff}"
    ".btn-update{background:#1a3a2a;color:var(--green);margin-left:4px}.btn-update:hover{opacity:.8}"
    ".badge-ok{background:#1a3a2a;color:var(--green);font-size:10px;padding:2px 7px;border-radius:4px}"));

  // CSS chunk 3 — task manager, chat, widgets
  client.print(F(
    ".proc-table{width:100%;border-collapse:collapse;font-size:13px}"
    ".proc-table th{text-align:left;padding:8px 6px;color:var(--muted);font-weight:500;"
      "border-bottom:1px solid var(--border);font-size:11px;text-transform:uppercase;letter-spacing:.5px}"
    ".proc-table td{padding:7px 6px;border-bottom:1px solid #1e2133}"
    ".stat-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:16px}"
    ".stat-card{background:var(--card);border-radius:8px;padding:12px}"
    ".stat-label{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.5px;margin-bottom:4px}"
    ".stat-val{font-size:22px;font-weight:700;color:var(--accent)}"
    ".bar{height:6px;background:#1e2133;border-radius:3px;margin-top:6px;overflow:hidden}"
    ".bar-fill{height:100%;background:var(--accent);border-radius:3px;transition:.6s}"
    "#chat-log{flex:1;overflow-y:auto;padding:12px;display:flex;flex-direction:column;gap:8px;min-height:0}"
    ".msg{max-width:80%;padding:8px 12px;border-radius:12px;font-size:13px;line-height:1.5}"
    ".msg-mine{background:var(--accent);color:#fff;align-self:flex-end;border-bottom-right-radius:3px}"
    ".msg-other{background:var(--card);color:var(--text);align-self:flex-start;border-bottom-left-radius:3px}"
    ".msg-sys{color:var(--muted);font-size:11px;text-align:center;align-self:center}"
    ".msg-user{font-size:10px;color:rgba(255,255,255,.6);margin-bottom:2px}"
    "#chat-input-row{display:flex;gap:8px;padding:12px;border-top:1px solid var(--border);flex-shrink:0}"
    "#chat-input{flex:1;background:var(--card);border:1px solid var(--border);color:var(--text);"
      "padding:8px 12px;border-radius:8px;font-size:13px;outline:none}"
    "#chat-input:focus{border-color:var(--accent)}"
    "#chat-send{background:var(--accent);color:#fff;border:none;padding:8px 16px;border-radius:8px;cursor:pointer}"
    ".widget-desktop{position:absolute}"
    ".widget-clock{background:var(--panel);border:1px solid var(--border);border-radius:12px;"
      "padding:16px 20px;text-align:center;cursor:move;width:160px}"
    ".wc-time{font-size:28px;font-weight:700;color:var(--accent);letter-spacing:2px}"
    ".wc-date{font-size:11px;color:var(--muted);margin-top:4px}"
    ".widget-sysmon{background:var(--panel);border:1px solid var(--border);border-radius:12px;padding:14px;cursor:move;width:200px}"
    ".wsm-row{display:flex;align-items:center;justify-content:space-between;margin-bottom:8px;font-size:12px}"
    ".wsm-label{color:var(--muted)}.wsm-val{font-weight:600}"
    ".widget-notes{background:#2a2210;border:1px solid #4a3a10;border-radius:12px;padding:14px;cursor:move;width:200px;resize:both;overflow:auto}"
    ".widget-notes textarea{width:100%;background:transparent;border:none;color:#f5d87a;"
      "font-size:13px;resize:none;outline:none;font-family:inherit;min-height:80px}"
    "</style></head><body>"
    "<div id='desktop'></div>"
    "<div id='taskbar'>"
      "<button class='tb-btn' onclick=\"openWin('appstore')\">&#9783; App Store</button>"
      "<button class='tb-btn' onclick=\"openWin('tasks')\">&#9881; Tasks</button>"
      "<button class='tb-btn' onclick=\"openWin('chat')\">&#128172; Chat</button>"
      "<button class='tb-btn' onclick=\"openWin('widgets')\">&#9638; Widgets</button>"
      "<span class='tb-clock' id='tbclock'></span>"
    "</div>"));

  // JS chunk 1 — window manager + clock
  client.print(F("<script>"
    "const IP=location.hostname;"
    "const WS_CHAT=`ws://${IP}:81`;"
    "const WS_SYS=`ws://${IP}:82`;"
    "let zTop=100,openWindows={};"
    "function openWin(id){"
      "if(openWindows[id]){focusWin(openWindows[id]);return;}"
      "const configs={"
        "appstore:{title:'&#9783; App Store',w:580,h:420,build:buildAppStore},"
        "tasks:{title:'&#9881; Task Manager',w:560,h:440,build:buildTaskMgr},"
        "chat:{title:'&#128172; Chat',w:380,h:480,build:buildChat},"
        "widgets:{title:'&#9638; Widgets',w:320,h:260,build:buildWidgetPanel}};"
      "const cfg=configs[id];"
      "const win=document.createElement('div');"
      "win.className='win';"
      "win.style.cssText=`width:${cfg.w}px;height:${cfg.h}px;top:${60+Math.random()*80}px;left:${80+Math.random()*120}px;z-index:${++zTop}`;"
      "win.innerHTML=`<div class='win-title'>${cfg.title}<button class='win-close' onclick='closeWin(\"${id}\")'></button></div><div class='win-body' id='wb-${id}'></div>`;"
      "makeDraggable(win,win.querySelector('.win-title'));"
      "document.getElementById('desktop').appendChild(win);"
      "openWindows[id]=win;"
      "win.addEventListener('mousedown',()=>{win.style.zIndex=++zTop;});"
      "cfg.build(document.getElementById('wb-'+id));}"
    "function closeWin(id){if(openWindows[id]){openWindows[id].remove();delete openWindows[id];}}"
    "function focusWin(w){w.style.zIndex=++zTop;}"
    "function makeDraggable(el,handle){"
      "let ox,oy;"
      "handle.addEventListener('mousedown',e=>{"
        "ox=e.clientX-el.offsetLeft;oy=e.clientY-el.offsetTop;"
        "const mm=e2=>{el.style.left=(e2.clientX-ox)+'px';el.style.top=(e2.clientY-oy)+'px';};"
        "const mu=()=>{removeEventListener('mousemove',mm);removeEventListener('mouseup',mu);};"
        "addEventListener('mousemove',mm);addEventListener('mouseup',mu);e.preventDefault();});}"
    "function tickClock(){"
      "const now=new Date();"
      "document.getElementById('tbclock').textContent="
        "now.toLocaleDateString('en-US',{weekday:'short',month:'short',day:'numeric'})+' '+"
        "now.toLocaleTimeString('en-US',{hour:'2-digit',minute:'2-digit'});}"
    "setInterval(tickClock,1000);tickClock();"));

  // JS chunk 2 — App Store
  client.print(F(
    "function buildAppStore(el){"
      "el.innerHTML='<div style=\"margin-bottom:12px;display:flex;gap:8px;align-items:center\">"
        "<span style=\"font-size:13px;color:var(--muted)\">App Registry</span>"
        "<button class=\"btn-sm btn-install\" onclick=\"refreshApps()\">&#8635; Refresh</button>"
        "</div><div class=\"app-grid\" id=\"app-grid\">Loading...</div>';"
      "refreshApps();}"
    "async function refreshApps(){"
      "const grid=document.getElementById('app-grid');if(!grid)return;"
      "const apps=await(await fetch('/api/apps')).json();"
      "grid.innerHTML=apps.map(a=>"
        "`<div class='app-card'>"
          "<div class='app-name'>${a.name}</div>"
          "<div class='app-ver'>v${a.version} ${a.size_kb}KB</div>"
          "<div class='app-desc'>${a.desc}</div>"
          "${a.installed"
            "?`<span class='badge-ok'>Installed</span>"
              "<button class='btn-sm btn-update' style='margin-top:6px' onclick='appUpdate(\"${a.id}\")'>Update</button>"
              "<button class='btn-sm btn-remove' style='margin-top:6px' onclick='appRemove(\"${a.id}\")'>Remove</button>`"
            ":`<button class='btn-sm btn-install' onclick='appInstall(\"${a.id}\")'>Install</button>`}"
        "</div>`).join('');}"
    "async function appInstall(id){await fetch(`/api/apps/install?id=${id}`,{method:'POST'});refreshApps();}"
    "async function appRemove(id){await fetch(`/api/apps/uninstall?id=${id}`,{method:'POST'});refreshApps();}"
    "async function appUpdate(id){await fetch(`/api/apps/update?id=${id}`,{method:'POST'});refreshApps();}"));

  // JS chunk 3 — Task Manager
  client.print(F(
    "let tmInterval=null;"
    "function buildTaskMgr(el){"
      "el.style.cssText='padding:0;display:flex;flex-direction:column';"
      "el.innerHTML=`<div style='padding:14px 16px 0'><div class='stat-grid'>"
        "<div class='stat-card'><div class='stat-label'>RAM Free</div><div class='stat-val' id='tm-ram'>--</div><div class='bar'><div class='bar-fill' id='tm-rambar' style='width:0%'></div></div></div>"
        "<div class='stat-card'><div class='stat-label'>Flash</div><div class='stat-val' id='tm-flash'>--</div><div class='bar'><div class='bar-fill' id='tm-flashbar' style='width:0%;background:var(--amber)'></div></div></div>"
        "<div class='stat-card'><div class='stat-label'>CPU Temp</div><div class='stat-val' id='tm-temp'>--</div></div>"
        "<div class='stat-card'><div class='stat-label'>Wi-Fi RSSI</div><div class='stat-val' id='tm-rssi'>--</div></div>"
      "</div></div>"
      "<div style='padding:0 16px 14px;overflow:auto;flex:1'>"
        "<table class='proc-table'>"
          "<thead><tr><th>PID</th><th>Name</th><th>Status</th><th>Mem</th><th>CPU%</th></tr></thead>"
          "<tbody id='proc-body'></tbody>"
        "</table></div>`;"
      "refreshTM();tmInterval=setInterval(refreshTM,2000);}"
    "async function refreshTM(){"
      "const r=document.getElementById('tm-ram');if(!r){clearInterval(tmInterval);return;}"
      "const[sRes,pRes]=await Promise.all([fetch('/api/stats'),fetch('/api/tasks')]);"
      "const stats=await sRes.json();const procs=await pRes.json();"
      "r.textContent=stats.ram_free_kb+' KB';"
      "document.getElementById('tm-rambar').style.width=stats.ram_pct+'%';"
      "document.getElementById('tm-flash').textContent=stats.flash_used_kb+'/'+stats.flash_total_kb+'KB';"
      "document.getElementById('tm-flashbar').style.width=stats.flash_pct+'%';"
      "document.getElementById('tm-temp').textContent=stats.cpu_temp.toFixed(1)+'°C';"
      "document.getElementById('tm-rssi').textContent=stats.wifi_rssi+' dBm';"
      "document.getElementById('proc-body').innerHTML=procs.map(p=>"
        "`<tr><td style='color:var(--muted)'>${p.pid}</td><td>${p.name}</td>"
          "<td><span style='color:${p.status===\"running\"?\"var(--green)\":\"var(--muted)\"}'>${p.status}</span></td>"
          "<td>${p.mem_kb}KB</td><td>${p.cpu_pct}%</td></tr>`).join('');}"));

  // JS chunk 4 — Chat
  client.print(F(
    "let ws=null,myName='User'+Math.floor(Math.random()*99+1);"
    "function buildChat(el){"
      "el.style.cssText='padding:0;display:flex;flex-direction:column;height:100%';"
      "el.innerHTML=`<div style='padding:10px 14px;border-bottom:1px solid var(--border);display:flex;align-items:center;gap:8px;flex-shrink:0'>"
          "<span style='font-size:12px;color:var(--muted)'>Name:</span>"
          "<input id='chat-name' value='${myName}' style='background:var(--card);border:1px solid var(--border);color:var(--text);padding:4px 8px;border-radius:5px;font-size:12px;width:110px;outline:none'>"
          "<button class='btn-sm btn-install' onclick='setChatName()'>Set</button>"
          "<span id='ws-status' style='margin-left:auto;font-size:11px;color:var(--muted)'>● Connecting</span>"
        "</div>"
        "<div id='chat-log'></div>"
        "<div id='chat-input-row'>"
          "<input id='chat-input' placeholder='Type a message...' onkeydown='if(event.key===\"Enter\")sendMsg()'>"
          "<button id='chat-send' onclick='sendMsg()'>Send</button>"
        "</div>`;"
      "connectChat();}"
    "function connectChat(){"
      "ws=new WebSocket(WS_CHAT);"
      "ws.onopen=()=>{const s=document.getElementById('ws-status');if(s)s.textContent='● Online';};"
      "ws.onclose=()=>{const s=document.getElementById('ws-status');if(s)s.textContent='● Offline';};"
      "ws.onmessage=e=>{"
        "const d=JSON.parse(e.data),log=document.getElementById('chat-log');if(!log)return;"
        "if(d.type==='history')d.data.forEach(m=>appendMsg(log,m.user,m.text));"
        "else if(d.type==='msg')appendMsg(log,d.user,d.text);};"
    "}"
    "function appendMsg(log,user,text){"
      "const mine=(user===myName),sys=(user==='System');"
      "const div=document.createElement('div');"
      "if(sys){div.className='msg msg-sys';div.textContent=text;}"
      "else{div.className='msg '+(mine?'msg-mine':'msg-other');"
        "if(!mine)div.innerHTML=`<div class='msg-user'>${user}</div>`;"
        "div.innerHTML+=`<span>${text}</span>`;}"
      "log.appendChild(div);log.scrollTop=log.scrollHeight;}"
    "function sendMsg(){"
      "const inp=document.getElementById('chat-input');"
      "if(!inp||!inp.value.trim()||!ws||ws.readyState!==1)return;"
      "ws.send(JSON.stringify({type:'msg',text:inp.value.trim()}));inp.value='';}"
    "function setChatName(){"
      "const inp=document.getElementById('chat-name');if(!inp)return;"
      "myName=inp.value.trim()||myName;"
      "if(ws&&ws.readyState===1)ws.send(JSON.stringify({type:'setname',name:myName}));}"));

  // JS chunk 5 — Widgets
  client.print(F(
    "let activeWidgets={};"
    "function buildWidgetPanel(el){"
      "el.innerHTML=`<p style='font-size:12px;color:var(--muted);margin-bottom:14px'>Add widgets to the desktop:</p>"
        "<div style='display:flex;flex-direction:column;gap:10px'>"
          "${[['clock','&#128336; Clock'],['sysmon','&#128202; System Monitor'],['notes','&#128203; Notes']].map(([id,label])=>"
            "`<div style='display:flex;align-items:center;justify-content:space-between;background:var(--card);padding:10px 14px;border-radius:8px;border:1px solid var(--border)'>"
              "<span style='font-size:13px'>${label}</span>"
              "<button class='btn-sm btn-install' id='wbtn-${id}' onclick='toggleWidget(\"${id}\")'>${activeWidgets[id]?'Remove':'Add'}</button>"
            "</div>`).join('')}"
        "</div>`;}"
    "function toggleWidget(id){"
      "if(activeWidgets[id]){activeWidgets[id].remove();delete activeWidgets[id];}"
      "else spawnWidget(id);"
      "const btn=document.getElementById('wbtn-'+id);if(btn)btn.textContent=activeWidgets[id]?'Remove':'Add';}"
    "function spawnWidget(id){"
      "const desk=document.getElementById('desktop');"
      "const el=document.createElement('div');"
      "el.className='widget-desktop';"
      "el.style.cssText=`top:80px;left:${20+Object.keys(activeWidgets).length*220}px`;"
      "if(id==='clock'){"
        "el.className+=' widget-clock';"
        "el.innerHTML='<div class=\"wc-time\" id=\"wc-t\">--:--</div><div class=\"wc-date\" id=\"wc-d\"></div>';"
        "const tick=()=>{const n=new Date();"
          "const et=document.getElementById('wc-t'),ed=document.getElementById('wc-d');"
          "if(!et){clearInterval(wi);return;}"
          "et.textContent=n.toLocaleTimeString('en-US',{hour:'2-digit',minute:'2-digit',second:'2-digit',hour12:false});"
          "ed.textContent=n.toLocaleDateString('en-US',{weekday:'short',month:'short',day:'numeric'});};"
        "const wi=setInterval(tick,1000);tick();"
      "}else if(id==='sysmon'){"
        "el.className+=' widget-sysmon';"
        "el.innerHTML='<div style=\"font-size:11px;color:var(--muted);margin-bottom:8px\">SYSTEM</div>"
          "<div class=\"wsm-row\"><span class=\"wsm-label\">RAM</span><span class=\"wsm-val\" id=\"wsm-ram\">--</span></div>"
          "<div class=\"wsm-row\"><span class=\"wsm-label\">Flash</span><span class=\"wsm-val\" id=\"wsm-flash\">--</span></div>"
          "<div class=\"wsm-row\"><span class=\"wsm-label\">Temp</span><span class=\"wsm-val\" id=\"wsm-temp\">--</span></div>"
          "<div class=\"wsm-row\"><span class=\"wsm-label\">RSSI</span><span class=\"wsm-val\" id=\"wsm-rssi\">--</span></div>';"
        "const ref=async()=>{const r=document.getElementById('wsm-ram');if(!r){clearInterval(wi2);return;}"
          "const s=await(await fetch('/api/stats')).json();"
          "r.textContent=s.ram_free_kb+' KB';"
          "document.getElementById('wsm-flash').textContent=s.flash_pct+'%';"
          "document.getElementById('wsm-temp').textContent=s.cpu_temp.toFixed(1)+'°C';"
          "document.getElementById('wsm-rssi').textContent=s.wifi_rssi+' dBm';};"
        "const wi2=setInterval(ref,3000);ref();"
      "}else if(id==='notes'){"
        "el.className+=' widget-notes';"
        "el.innerHTML='<div style=\"font-size:11px;color:#f5d87a;margin-bottom:6px\">&#128203; Notes</div>"
          "<textarea id=\"notes-ta\" placeholder=\"Write anything...\" style=\"min-height:100px\"></textarea>';"
        "const saved=localStorage.getItem('notes-widget')||'';"
        "setTimeout(()=>{const t=document.getElementById('notes-ta');if(t)t.value=saved;},50);"
        "el.addEventListener('input',()=>{const t=document.getElementById('notes-ta');if(t)localStorage.setItem('notes-widget',t.value);});}"
      "makeDraggable(el,el);desk.appendChild(el);activeWidgets[id]=el;}"
    "</script></body></html>"));

  client.stop();
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  API ROUTES
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
void handleAppList() {
  String data = readFile("/apps.json");
  if (data.isEmpty()) data = defaultAppRegistry();
  server.send(200, "application/json", data);
}
void handleAppInstall() {
  if (!server.hasArg("id")) { server.send(400, "application/json", "{\"error\":\"missing id\"}"); return; }
  String id = server.arg("id");
  String data = readFile("/apps.json");
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, data);
  bool found = false;
  for (JsonObject app : doc.as<JsonArray>()) {
    if (app["id"] == id) { app["installed"] = true; found = true; break; }
  }
  if (!found) { server.send(404, "application/json", "{\"error\":\"app not found\"}"); return; }
  String out; serializeJson(doc, out); writeFile("/apps.json", out);
  server.send(200, "application/json", "{\"status\":\"installed\",\"id\":\"" + id + "\"}");
}
void handleAppUninstall() {
  if (!server.hasArg("id")) { server.send(400, "application/json", "{\"error\":\"missing id\"}"); return; }
  String id = server.arg("id");
  String data = readFile("/apps.json");
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, data);
  for (JsonObject app : doc.as<JsonArray>()) {
    if (app["id"] == id) { app["installed"] = false; break; }
  }
  String out; serializeJson(doc, out); writeFile("/apps.json", out);
  server.send(200, "application/json", "{\"status\":\"uninstalled\",\"id\":\"" + id + "\"}");
}
void handleAppUpdate() {
  if (!server.hasArg("id")) { server.send(400, "application/json", "{\"error\":\"missing id\"}"); return; }
  String id = server.arg("id");
  String data = readFile("/apps.json");
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, data);
  for (JsonObject app : doc.as<JsonArray>()) {
    if (app["id"] == id) {
      String ver = app["version"] | "1.0";
      float v = ver.toFloat() + 0.1f;
      char buf[8]; dtostrf(v, 3, 1, buf);
      app["version"] = String(buf);
      break;
    }
  }
  String out; serializeJson(doc, out); writeFile("/apps.json", out);
  server.send(200, "application/json", "{\"status\":\"updated\",\"id\":\"" + id + "\"}");
}
void handleTaskList() {
  String uptime = String(millis() / 1000);
  String json = "[";
  json += "{\"pid\":1,\"name\":\"webserver\",\"status\":\"running\",\"mem_kb\":12,\"cpu_pct\":15,\"uptime\":" + uptime + "},";
  json += "{\"pid\":2,\"name\":\"ws-chat\",\"status\":\"running\",\"mem_kb\":6,\"cpu_pct\":3,\"uptime\":" + uptime + "},";
  json += "{\"pid\":3,\"name\":\"ws-system\",\"status\":\"running\",\"mem_kb\":4,\"cpu_pct\":2,\"uptime\":" + uptime + "},";
  json += "{\"pid\":4,\"name\":\"littlefs\",\"status\":\"idle\",\"mem_kb\":2,\"cpu_pct\":0,\"uptime\":" + uptime + "},";
  json += "{\"pid\":5,\"name\":\"wifi-driver\",\"status\":\"running\",\"mem_kb\":8,\"cpu_pct\":5,\"uptime\":" + uptime + "}]";
  server.send(200, "application/json", json);
}
void handleSystemStats() {
  server.send(200, "application/json", getSystemStats());
}
void handleWidgetList() {
  String raw = readFile("/apps.json");
  if (raw.isEmpty()) raw = defaultAppRegistry();
  DynamicJsonDocument src(2048);
  deserializeJson(src, raw);
  DynamicJsonDocument out(1024);
  JsonArray arr = out.to<JsonArray>();
  for (JsonObject app : src.as<JsonArray>()) {
    if (app["installed"] == true) {
      JsonObject w = arr.createNestedObject();
      w["id"] = app["id"]; w["name"] = app["name"];
    }
  }
  String s; serializeJson(out, s);
  server.send(200, "application/json", s);
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  SETUP
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== WebOS-8266 v2.0 Booting ===");
  if (!LittleFS.begin()) {
    Serial.println("[FS] LittleFS mount failed - formatting...");
    LittleFS.format(); LittleFS.begin();
  }
  ensureAppRegistry();
  Serial.println("[FS] LittleFS OK");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.println("\n[WiFi] IP: " + WiFi.localIP().toString());
  server.on("/",                   HTTP_GET,  handleRoot);
  server.on("/api/apps",           HTTP_GET,  handleAppList);
  server.on("/api/apps/install",   HTTP_POST, handleAppInstall);
  server.on("/api/apps/uninstall", HTTP_POST, handleAppUninstall);
  server.on("/api/apps/update",    HTTP_POST, handleAppUpdate);
  server.on("/api/tasks",          HTTP_GET,  handleTaskList);
  server.on("/api/stats",          HTTP_GET,  handleSystemStats);
  server.on("/api/widgets",        HTTP_GET,  handleWidgetList);
  server.begin();
  Serial.println("[HTTP] Server on port 80");
  wsChat.begin();   wsChat.onEvent(onChatWsEvent);
  wsSystem.begin(); wsSystem.onEvent(onSysWsEvent);
  Serial.println("[WS] Chat:81  Sys:82");
  Serial.println("[BOOT] WebOS-8266 ready!");
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  LOOP
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
void loop() {
  server.handleClient();
  wsChat.loop();
  wsSystem.loop();
  if (millis() - lastStatBroadcast > 3000) {
    lastStatBroadcast = millis();
    String stats = getSystemStats();
    wsSystem.broadcastTXT(stats);
    wifiSpeedKbps = max(0.0f, (float)(WiFi.RSSI() + 100) * 2.5f);
  }
}
