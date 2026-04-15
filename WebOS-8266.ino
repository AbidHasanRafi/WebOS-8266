// ═══════════════════════════════════════════════════════════════════════════════
// @file    WebOS-8266.ino
// @brief   WebOS-8266 1.0 — Complete Web-Based Desktop OS for ESP8266
// @version 1.0.0
// @desc    A full desktop operating system served from an ESP8266 microcontroller.
//          Features: windowed desktop environment, 8 applications, 30+ terminal
//          commands, task scheduler, pin watcher, WiFi manager, OTA updates,
//          authentication, NTP time sync, and a premium glassmorphism UI.
//          Fully responsive for mobile and desktop browsers.
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <ESP8266mDNS.h>
#include <Servo.h>
#include <ArduinoOTA.h>
#include <time.h>

// ═══════════════════════════════════════════════════════════════════════════════
//  CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════════
#define MICROWEBOS_VERSION "1.0.0"
#define HOSTNAME           "webos-8266"
#define MAX_NOTIFICATIONS  10
#define MAX_SCHED_TASKS    5
#define MAX_PIN_WATCHERS   5
#define MAX_LOG_ENTRIES    20
#define NTP_SERVER         "pool.ntp.org"
#define GMT_OFFSET_SEC     21600   // UTC+6 in seconds
#define DST_OFFSET_SEC     0
#define AUTH_ENABLED       true
#define DEFAULT_USERNAME   "admin"
#define DEFAULT_PASSWORD   "admin"

// ═══════════════════════════════════════════════════════════════════════════════
//  WiFi CREDENTIALS
// ═══════════════════════════════════════════════════════════════════════════════
const char* ssid       = "HONOR 200";
const char* password   = "random_state_42";
const char* ap_ssid    = "WebOS-8266";
const char* ap_password = "12345678";

// ═══════════════════════════════════════════════════════════════════════════════
//  PIN DEFINITIONS & STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════════
#define LED_BUILTIN_PIN 2
const int availablePins[] = {0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16};
const int numPins = 11;

struct PinConfig {
    int pin;
    String name;
    String mode;   // OUTPUT, INPUT, PWM, SERVO
    bool state;
    int pwmValue;
    int servoAngle;
    String label;  // User-defined label
};

PinConfig pins[] = {
    {0,  "GPIO0",  "INPUT",  false, 0, 0, "Flash Button"},
    {1,  "GPIO1",  "OUTPUT", false, 0, 0, "TX"},
    {2,  "GPIO2",  "OUTPUT", false, 0, 0, "Built-in LED"},
    {3,  "GPIO3",  "INPUT",  false, 0, 0, "RX"},
    {4,  "GPIO4",  "OUTPUT", false, 0, 0, "D2 / SDA"},
    {5,  "GPIO5",  "OUTPUT", false, 0, 0, "D1 / SCL"},
    {12, "GPIO12", "OUTPUT", false, 0, 0, "D6 / MISO"},
    {13, "GPIO13", "OUTPUT", false, 0, 0, "D7 / MOSI"},
    {14, "GPIO14", "OUTPUT", false, 0, 0, "D5 / SCLK"},
    {15, "GPIO15", "OUTPUT", false, 0, 0, "D8 / CS"},
    {16, "GPIO16", "OUTPUT", false, 0, 0, "D0 / Wake"}
};

Servo servos[3];
int servoPins[] = {4, 5, 14};
bool servosAttached[3] = {false, false, false};

// ═══════════════════════════════════════════════════════════════════════════════
//  NOTIFICATION SYSTEM
// ═══════════════════════════════════════════════════════════════════════════════
struct Notification {
    String message;
    String type;      // info, success, warning, error
    unsigned long ts;  // millis timestamp
    bool read;
};

Notification notifications[MAX_NOTIFICATIONS];
int notifCount = 0;
int notifHead  = 0;

void addNotification(const String& msg, const String& type) {
    int idx = (notifHead + notifCount) % MAX_NOTIFICATIONS;
    if (notifCount >= MAX_NOTIFICATIONS) {
        notifHead = (notifHead + 1) % MAX_NOTIFICATIONS;
    } else {
        notifCount++;
    }
    notifications[idx].message = msg;
    notifications[idx].type = type;
    notifications[idx].ts = millis();
    notifications[idx].read = false;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  TASK SCHEDULER
// ═══════════════════════════════════════════════════════════════════════════════
struct ScheduledTask {
    bool active;
    bool enabled;
    int pin;
    String action;        // on, off, toggle, pwm
    int value;            // for PWM
    unsigned long intervalMs;
    unsigned long lastRun;
    String name;
};

ScheduledTask schedTasks[MAX_SCHED_TASKS];

// ═══════════════════════════════════════════════════════════════════════════════
//  PIN WATCHER
// ═══════════════════════════════════════════════════════════════════════════════
struct PinWatcher {
    bool active;
    int pin;
    String condition;  // high, low, above, below
    int threshold;
    bool triggered;
    String name;
};

PinWatcher pinWatchers[MAX_PIN_WATCHERS];

// ═══════════════════════════════════════════════════════════════════════════════
//  SYSTEM LOG
// ═══════════════════════════════════════════════════════════════════════════════
struct LogEntry {
    String message;
    String level;      // info, warn, error
    unsigned long ts;
};

LogEntry sysLog[MAX_LOG_ENTRIES];
int logCount = 0;
int logHead  = 0;

void addLog(const String& msg, const String& level) {
    int idx = (logHead + logCount) % MAX_LOG_ENTRIES;
    if (logCount >= MAX_LOG_ENTRIES) {
        logHead = (logHead + 1) % MAX_LOG_ENTRIES;
    } else {
        logCount++;
    }
    sysLog[idx].message = msg;
    sysLog[idx].level = level;
    sysLog[idx].ts = millis();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  GLOBAL OBJECTS & STATE
// ═══════════════════════════════════════════════════════════════════════════════
ESP8266WebServer server(80);
File uploadFile;

String authToken     = "";
bool   authEnabled   = AUTH_ENABLED;
String adminUser     = DEFAULT_USERNAME;
String adminPass     = DEFAULT_PASSWORD;

unsigned long lastSchedulerTick = 0;
unsigned long lastWatcherTick   = 0;
unsigned long bootTime          = 0;
bool          ntpSynced         = false;
bool          wifiConnected     = false;

// ═══════════════════════════════════════════════════════════════════════════════
//  HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════════
bool isBuiltinLedPin(int p) {
    return p == LED_BUILTIN_PIN;
}

void applyOutputState(int p, bool logicalState) {
    bool activeLow = isBuiltinLedPin(p);
    digitalWrite(p, activeLow ? (logicalState ? LOW : HIGH) : (logicalState ? HIGH : LOW));
}

int findPinIndex(int p) {
    for (int i = 0; i < numPins; i++) {
        if (pins[i].pin == p) return i;
    }
    return -1;
}

int findServoIndex(int p) {
    for (int i = 0; i < 3; i++) {
        if (servoPins[i] == p) return i;
    }
    return -1;
}

String extractJsonField(const String& body, const String& field) {
    String key = "\"" + field + "\"";
    int kIdx = body.indexOf(key);
    if (kIdx < 0) return "";
    int colon = body.indexOf(':', kIdx + key.length());
    if (colon < 0) return "";
    int start = colon + 1;
    while (start < (int)body.length() && isspace(body[start])) start++;
    if (start >= (int)body.length()) return "";
    if (body[start] == '"') {
        start++;
        int end = body.indexOf('"', start);
        if (end < 0) return "";
        return body.substring(start, end);
    }
    int end = start;
    while (end < (int)body.length() && body[end] != ',' && body[end] != '}' && !isspace(body[end])) end++;
    return body.substring(start, end);
}

String escapeJson(String s) {
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    s.replace("\n", "\\n");
    s.replace("\r", "");
    s.replace("\t", "\\t");
    return s;
}

String uptimeString() {
    unsigned long secs = millis() / 1000;
    int d = secs / 86400;
    int h = (secs % 86400) / 3600;
    int m = (secs % 3600) / 60;
    int s = secs % 60;
    String r = "";
    if (d > 0) r += String(d) + "d ";
    r += String(h) + "h " + String(m) + "m " + String(s) + "s";
    return r;
}

String getTimeString() {
    time_t now = time(nullptr);
    struct tm* ti = localtime(&now);
    if (ti->tm_year < 100) return "--:--";
    char buf[20];
    sprintf(buf, "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
    return String(buf);
}

String getDateString() {
    time_t now = time(nullptr);
    struct tm* ti = localtime(&now);
    if (ti->tm_year < 100) return "----/--/--";
    char buf[20];
    sprintf(buf, "%04d/%02d/%02d", ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday);
    return String(buf);
}

bool checkAuth() {
    if (!authEnabled) return true;
    if (authToken.length() == 0) return false;
    String tok = server.header("X-Auth-Token");
    return tok.length() > 0 && tok == authToken;
}

void sendUnauthorized() {
    server.send(401, F("application/json"), F("{\"error\":\"Unauthorized\"}"));
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PROGMEM HTML — Part 1: Head + CSS
// ═══════════════════════════════════════════════════════════════════════════════
const char HTML_START[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no">
    <title>WebOS-8266</title>
<style>
)rawliteral";

// ═══════════════════════════════════════════════════════════════════════════════
//  PROGMEM CSS
// ═══════════════════════════════════════════════════════════════════════════════
const char HTML_CSS[] PROGMEM = R"rawliteral(
:root {
    --bg-primary: #0a0a1a;
    --bg-secondary: #12122a;
    --bg-surface: rgba(18, 18, 42, 0.92);
    --bg-surface-solid: #16163a;
    --bg-hover: rgba(30, 30, 60, 0.95);
    --bg-glass: rgba(16, 16, 40, 0.78);
    --accent: #6366f1;
    --accent-hover: #818cf8;
    --accent-glow: rgba(99, 102, 241, 0.25);
    --accent2: #8b5cf6;
    --success: #22c55e;
    --success-bg: rgba(34, 197, 94, 0.15);
    --error: #ef4444;
    --error-bg: rgba(239, 68, 68, 0.15);
    --warning: #f59e0b;
    --warning-bg: rgba(245, 158, 11, 0.15);
    --info: #06b6d4;
    --info-bg: rgba(6, 182, 212, 0.15);
    --text: #e2e8f0;
    --text-sec: #94a3b8;
    --text-muted: #64748b;
    --border: rgba(255, 255, 255, 0.07);
    --border-light: rgba(255, 255, 255, 0.12);
    --shadow: 0 8px 32px rgba(0, 0, 0, 0.5);
    --shadow-sm: 0 2px 8px rgba(0, 0, 0, 0.3);
    --shadow-glow: 0 0 30px rgba(99, 102, 241, 0.12);
    --radius: 10px;
    --radius-lg: 16px;
    --radius-sm: 6px;
    --taskbar-h: 50px;
    --titlebar-h: 40px;
    --transition: 200ms cubic-bezier(0.4, 0, 0.2, 1);
    --font-mono: 'Consolas', 'Monaco', 'Courier New', monospace;
}

*, *::before, *::after { margin:0; padding:0; box-sizing:border-box; }

html, body {
    width: 100%; height: 100%;
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Inter', Roboto, sans-serif;
    background: var(--bg-primary);
    color: var(--text);
    overflow: hidden;
    font-size: 13px;
    line-height: 1.5;
    -webkit-font-smoothing: antialiased;
}

/* ── Boot Screen ────────────────────────────────────── */
#bootScreen {
    position: fixed; inset: 0; z-index: 99999;
    background: var(--bg-primary);
    display: flex; flex-direction: column;
    align-items: center; justify-content: center;
    transition: opacity 0.6s ease;
}
#bootScreen.hidden { opacity: 0; pointer-events: none; }
.boot-logo {
    font-size: 36px; font-weight: 800;
    background: linear-gradient(135deg, var(--accent), var(--accent2));
    -webkit-background-clip: text; -webkit-text-fill-color: transparent;
    background-clip: text; margin-bottom: 12px;
    animation: bootPulse 2s ease-in-out infinite;
}
.boot-sub { color: var(--text-sec); font-size: 13px; margin-bottom: 30px; }
.boot-bar-wrap {
    width: 260px; height: 4px;
    background: rgba(255,255,255,0.06);
    border-radius: 4px; overflow: hidden;
}
.boot-bar {
    height: 100%; width: 0%;
    background: linear-gradient(90deg, var(--accent), var(--accent2));
    border-radius: 4px;
    transition: width 0.4s ease;
}
.boot-status {
    margin-top: 16px; font-size: 12px;
    color: var(--text-muted);
    min-height: 18px;
}
@keyframes bootPulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.7; }
}

/* ── Login Screen ───────────────────────────────────── */
#loginScreen {
    position: fixed; inset: 0; z-index: 99998;
    background: var(--bg-primary);
    display: none; align-items: center; justify-content: center;
}
#loginScreen.active { display: flex; }
.login-card {
    background: var(--bg-surface);
    border: 1px solid var(--border-light);
    border-radius: var(--radius-lg);
    padding: 40px;
    width: 360px; max-width: 90vw;
    backdrop-filter: blur(24px);
    box-shadow: var(--shadow), var(--shadow-glow);
    text-align: center;
}
.login-card h2 {
    font-size: 22px; font-weight: 700;
    margin-bottom: 6px;
    background: linear-gradient(135deg, var(--accent), var(--accent2));
    -webkit-background-clip: text; -webkit-text-fill-color: transparent;
    background-clip: text;
}
.login-card p { color: var(--text-sec); margin-bottom: 24px; font-size: 13px; }
.login-card input {
    width: 100%; padding: 12px 16px;
    background: rgba(255,255,255,0.04);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    color: var(--text); font-size: 14px;
    margin-bottom: 12px; outline: none;
    transition: border var(--transition);
}
.login-card input:focus { border-color: var(--accent); }
.login-card button {
    width: 100%; padding: 12px;
    background: linear-gradient(135deg, var(--accent), var(--accent2));
    border: none; border-radius: var(--radius);
    color: white; font-size: 14px; font-weight: 600;
    cursor: pointer; margin-top: 8px;
    transition: all var(--transition);
}
.login-card button:hover { transform: translateY(-1px); box-shadow: 0 4px 20px var(--accent-glow); }
.login-error {
    color: var(--error); font-size: 12px;
    margin-top: 10px; min-height: 18px;
}

/* ── Desktop ────────────────────────────────────────── */
#desktop {
    position: fixed; inset: 0;
    display: none; flex-direction: column;
    background: linear-gradient(135deg, #0a0a1a 0%, #131340 40%, #1a0a2e 70%, #0a0a1a 100%);
    background-size: 400% 400%;
    animation: desktopGradient 20s ease infinite;
}
#desktop.active { display: flex; }

@keyframes desktopGradient {
    0% { background-position: 0% 50%; }
    25% { background-position: 50% 0%; }
    50% { background-position: 100% 50%; }
    75% { background-position: 50% 100%; }
    100% { background-position: 0% 50%; }
}

.desktop-area {
    flex: 1; position: relative; overflow: hidden;
    padding: 16px;
}

/* ── Desktop Icons ──────────────────────────────────── */
.desktop-icons {
    display: grid;
    grid-template-rows: repeat(auto-fill, 90px);
    grid-auto-flow: column;
    grid-auto-columns: 90px;
    gap: 8px;
    position: absolute; top: 16px; left: 16px; bottom: 16px;
    z-index: 1;
}
.desktop-icon {
    display: flex; flex-direction: column;
    align-items: center; gap: 6px;
    padding: 10px 6px;
    border-radius: var(--radius);
    cursor: pointer;
    transition: all var(--transition);
    user-select: none;
    text-decoration: none;
}
.desktop-icon:hover {
    background: rgba(255,255,255,0.06);
}
.desktop-icon:active {
    background: rgba(99, 102, 241, 0.15);
    transform: scale(0.95);
}
.desktop-icon .icon {
    width: 46px; height: 46px;
    display: flex; align-items: center; justify-content: center;
    font-size: 24px;
    background: var(--bg-glass);
    border: 1px solid var(--border);
    border-radius: 12px;
    backdrop-filter: blur(8px);
    transition: all var(--transition);
}
.desktop-icon:hover .icon {
    border-color: var(--accent);
    box-shadow: 0 0 16px var(--accent-glow);
    transform: translateY(-2px);
}
.icon svg, .sm-icon svg, .win-title-icon svg, .tb-app-icon,
.file-icon svg, .app-btn svg, .term-send svg {
    width: 16px;
    height: 16px;
    display: inline-block;
    vertical-align: middle;
    flex-shrink: 0;
}
.desktop-icon .icon svg {
    width: 24px;
    height: 24px;
}
.sm-icon svg, .win-title-icon svg {
    width: 18px;
    height: 18px;
}
.file-icon svg {
    width: 20px;
    height: 20px;
}
.app-btn svg {
    margin-right: 6px;
}
.desktop-icon .label {
    font-size: 11px; color: var(--text);
    text-align: center; line-height: 1.2;
    text-shadow: 0 1px 4px rgba(0,0,0,0.8);
    max-width: 80px; overflow: hidden;
    text-overflow: ellipsis; white-space: nowrap;
}

/* ── Window Manager ─────────────────────────────────── */
.windows-layer {
    position: absolute; inset: 0;
    z-index: 10;
    pointer-events: none;
}
.win {
    position: absolute;
    background: var(--bg-surface-solid);
    border: 1px solid var(--border-light);
    border-radius: var(--radius-lg);
    box-shadow: var(--shadow);
    display: flex; flex-direction: column;
    overflow: hidden;
    min-width: 340px; min-height: 220px;
    pointer-events: auto;
    animation: winOpen 0.25s ease-out;
    transition: box-shadow var(--transition);
}
.win.focused {
    box-shadow: var(--shadow), 0 0 0 1px rgba(99,102,241,0.35), var(--shadow-glow);
    z-index: auto;
}
.win.closing {
    animation: winClose 0.2s ease-in forwards;
}
.win.minimized {
    display: none !important;
}
.win.maximized {
    top: 0 !important; left: 0 !important;
    width: 100% !important;
    height: calc(100% - var(--taskbar-h)) !important;
    border-radius: 0;
}
.win.snapped-left {
    top: 0 !important; left: 0 !important;
    width: 50% !important;
    height: calc(100% - var(--taskbar-h)) !important;
    border-radius: 0;
}
.win.snapped-right {
    top: 0 !important; left: 50% !important;
    width: 50% !important;
    height: calc(100% - var(--taskbar-h)) !important;
    border-radius: 0;
}
@keyframes winOpen {
    from { opacity: 0; transform: scale(0.88) translateY(10px); }
    to { opacity: 1; transform: scale(1) translateY(0); }
}
@keyframes winClose {
    from { opacity: 1; transform: scale(1); }
    to { opacity: 0; transform: scale(0.88) translateY(10px); }
}

/* ── Window Titlebar ────────────────────────────────── */
.win-titlebar {
    height: var(--titlebar-h);
    background: linear-gradient(180deg, rgba(255,255,255,0.03) 0%, transparent 100%);
    border-bottom: 1px solid var(--border);
    display: flex; align-items: center;
    padding: 0 6px 0 14px;
    cursor: default; user-select: none;
    flex-shrink: 0;
}
.win-titlebar-drag {
    flex: 1; cursor: move;
    display: flex; align-items: center;
    height: 100%; gap: 8px;
}
.win-title-icon { font-size: 14px; }
.win-title {
    font-size: 13px; font-weight: 600;
    color: var(--text); white-space: nowrap;
    overflow: hidden; text-overflow: ellipsis;
}
.win-controls { display: flex; gap: 2px; }
.win-ctrl {
    width: 32px; height: 28px;
    border: none; background: transparent;
    color: var(--text-sec);
    cursor: pointer; border-radius: var(--radius-sm);
    font-size: 14px;
    display: flex; align-items: center; justify-content: center;
    transition: all 0.15s ease;
}
.win-ctrl:hover { background: rgba(255,255,255,0.08); color: var(--text); }
.win-ctrl.close:hover { background: var(--error); color: white; }

/* ── Window Body ────────────────────────────────────── */
.win-body {
    flex: 1; overflow: auto;
    padding: 16px;
    background: var(--bg-surface-solid);
}
.win-body::-webkit-scrollbar { width: 6px; }
.win-body::-webkit-scrollbar-track { background: transparent; }
.win-body::-webkit-scrollbar-thumb { background: var(--border-light); border-radius: 3px; }
.win-body::-webkit-scrollbar-thumb:hover { background: var(--accent); }

/* ── Resize Handle ──────────────────────────────────── */
.win-resize {
    position: absolute; bottom: 0; right: 0;
    width: 18px; height: 18px;
    cursor: nwse-resize;
    z-index: 2;
}
.win-resize::after {
    content: '';
    position: absolute; bottom: 4px; right: 4px;
    width: 8px; height: 8px;
    border-right: 2px solid var(--text-muted);
    border-bottom: 2px solid var(--text-muted);
    opacity: 0.5;
}

/* ── Taskbar ────────────────────────────────────────── */
.taskbar {
    height: var(--taskbar-h);
    background: var(--bg-glass);
    backdrop-filter: blur(24px);
    -webkit-backdrop-filter: blur(24px);
    border-top: 1px solid var(--border);
    display: flex; align-items: center;
    padding: 0 8px; gap: 4px;
    z-index: 9000;
    flex-shrink: 0;
}
.tb-start-btn {
    width: 40px; height: 36px;
    background: linear-gradient(135deg, var(--accent), var(--accent2));
    border: none; border-radius: var(--radius);
    color: white; font-size: 16px; font-weight: 700;
    cursor: pointer; flex-shrink: 0;
    display: flex; align-items: center; justify-content: center;
    transition: all var(--transition);
}
.tb-start-btn:hover {
    box-shadow: 0 0 16px var(--accent-glow);
    transform: scale(1.05);
}
.tb-start-btn.active {
    background: var(--accent-hover);
}
.tb-sep {
    width: 1px; height: 28px;
    background: var(--border);
    margin: 0 4px; flex-shrink: 0;
}
.tb-apps {
    flex: 1; display: flex;
    gap: 3px; overflow-x: auto;
    padding: 0 4px;
}
.tb-apps::-webkit-scrollbar { display: none; }
.tb-app-btn {
    height: 34px; padding: 0 12px;
    background: rgba(255,255,255,0.04);
    border: 1px solid transparent;
    border-radius: var(--radius-sm);
    color: var(--text-sec);
    cursor: pointer; font-size: 12px;
    white-space: nowrap;
    display: flex; align-items: center; gap: 6px;
    transition: all 0.15s ease;
    flex-shrink: 0;
}
.tb-app-btn:hover { background: rgba(255,255,255,0.08); color: var(--text); }
.tb-app-btn.active {
    background: rgba(99, 102, 241, 0.15);
    border-color: var(--accent);
    color: var(--text);
}
.tb-app-btn.minimized { opacity: 0.5; }
.tb-tray {
    display: flex; align-items: center;
    gap: 8px; flex-shrink: 0;
    margin-left: auto;
}
.tb-tray-item {
    font-size: 12px; color: var(--text-sec);
    cursor: pointer; padding: 4px 6px;
    border-radius: var(--radius-sm);
    transition: all 0.15s ease;
    position: relative;
}
.tb-tray-item:hover { background: rgba(255,255,255,0.06); color: var(--text); }
.tb-tray-wifi {
    min-width: 32px;
    min-height: 26px;
    display: flex;
    align-items: center;
    justify-content: center;
    color: var(--text-sec);
}
.tb-tray-wifi .tb-wifi-icon {
    width: 16px;
    height: 16px;
    display: flex;
    align-items: center;
    justify-content: center;
}
.tb-tray-wifi .tb-wifi-icon svg {
    width: 16px;
    height: 16px;
}
.tb-tray-wifi.good { color: var(--success); }
.tb-tray-wifi.fair { color: var(--warning); }
.tb-tray-wifi.weak { color: var(--text-sec); }
.tb-tray-wifi.off { color: var(--error); }
.tb-tray-wifi.off .tb-wifi-icon { opacity: 0.75; }
.tb-notif-badge {
    position: absolute; top: -2px; right: -4px;
    width: 16px; height: 16px;
    background: var(--error);
    border-radius: 50%; font-size: 9px;
    display: flex; align-items: center; justify-content: center;
    color: white; font-weight: 700;
}
.tb-clock {
    font-size: 12px; color: var(--text);
    font-weight: 500; padding: 4px 8px;
    cursor: default;
}

/* ── Start Menu ─────────────────────────────────────── */
.start-menu {
    position: fixed; bottom: var(--taskbar-h);
    left: 0; width: 340px;
    max-height: 75vh;
    background: var(--bg-glass);
    backdrop-filter: blur(28px);
    -webkit-backdrop-filter: blur(28px);
    border: 1px solid var(--border-light);
    border-bottom: none;
    border-radius: 0 var(--radius-lg) 0 0;
    z-index: 9500;
    transform: translateY(110%);
    opacity: 0;
    transition: transform 0.3s cubic-bezier(0.16, 1, 0.3, 1), opacity 0.3s ease;
    overflow-y: auto;
    box-shadow: var(--shadow);
}
.start-menu.open {
    transform: translateY(0);
    opacity: 1;
}
.start-menu::-webkit-scrollbar { width: 4px; }
.start-menu::-webkit-scrollbar-thumb { background: var(--border-light); border-radius: 2px; }
.sm-header {
    padding: 20px 20px 12px;
    border-bottom: 1px solid var(--border);
}
.sm-header h3 {
    font-size: 16px; font-weight: 700;
    background: linear-gradient(135deg, var(--accent), var(--accent2));
    -webkit-background-clip: text; -webkit-text-fill-color: transparent;
    background-clip: text;
}
.sm-header p { font-size: 11px; color: var(--text-muted); margin-top: 2px; }
.sm-search {
    margin: 12px 16px;
    padding: 10px 14px;
    background: rgba(255,255,255,0.04);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    color: var(--text); font-size: 13px;
    width: calc(100% - 32px);
    outline: none;
    transition: border var(--transition);
}
.sm-search:focus { border-color: var(--accent); }
.sm-section-label {
    padding: 8px 20px 4px;
    font-size: 10px; text-transform: uppercase;
    letter-spacing: 1px; color: var(--text-muted);
    font-weight: 600;
}
.sm-grid {
    display: grid; grid-template-columns: 1fr 1fr;
    gap: 4px; padding: 4px 12px 12px;
}
.sm-item {
    display: flex; align-items: center; gap: 10px;
    padding: 10px 12px; border-radius: var(--radius);
    cursor: pointer; transition: all 0.15s ease;
}
.sm-item:hover { background: rgba(255,255,255,0.06); }
.sm-item:active { background: rgba(99,102,241,0.15); transform: scale(0.97); }
.sm-item .sm-icon {
    width: 36px; height: 36px;
    display: flex; align-items: center; justify-content: center;
    font-size: 18px;
    background: rgba(255,255,255,0.04);
    border-radius: var(--radius);
}
.sm-item .sm-label { font-size: 12px; font-weight: 500; }
.sm-footer {
    padding: 12px 20px;
    border-top: 1px solid var(--border);
    display: flex; justify-content: space-between;
    align-items: center;
}
.sm-footer button {
    padding: 8px 14px;
    background: rgba(255,255,255,0.05);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    color: var(--text-sec); font-size: 12px;
    cursor: pointer; transition: all 0.15s ease;
}
.sm-footer button:hover { background: rgba(255,255,255,0.1); color: var(--text); }
.sm-footer .sm-power:hover { background: var(--error-bg); color: var(--error); border-color: rgba(239,68,68,0.3); }

/* ── Notification Panel ─────────────────────────────── */
.notif-panel {
    position: fixed; top: 0; right: 0;
    width: 340px; height: calc(100% - var(--taskbar-h));
    background: var(--bg-glass);
    backdrop-filter: blur(28px);
    -webkit-backdrop-filter: blur(28px);
    border-left: 1px solid var(--border-light);
    z-index: 9400;
    transform: translateX(100%);
    transition: transform 0.3s cubic-bezier(0.16, 1, 0.3, 1);
    display: flex; flex-direction: column;
    box-shadow: -4px 0 20px rgba(0,0,0,0.3);
}
.notif-panel.open { transform: translateX(0); }
.notif-header {
    padding: 16px 20px;
    border-bottom: 1px solid var(--border);
    display: flex; justify-content: space-between;
    align-items: center;
}
.notif-header h3 { font-size: 15px; font-weight: 600; }
.notif-clear-btn {
    padding: 6px 12px; font-size: 11px;
    background: rgba(255,255,255,0.05);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    color: var(--text-sec); cursor: pointer;
    transition: all 0.15s ease;
}
.notif-clear-btn:hover { background: var(--error-bg); color: var(--error); }
.notif-list {
    flex: 1; overflow-y: auto; padding: 8px;
}
.notif-item {
    padding: 12px 14px;
    border-radius: var(--radius);
    margin-bottom: 4px;
    border-left: 3px solid var(--accent);
    background: rgba(255,255,255,0.02);
    transition: all 0.15s ease;
}
.notif-item:hover { background: rgba(255,255,255,0.04); }
.notif-item.info { border-left-color: var(--info); }
.notif-item.success { border-left-color: var(--success); }
.notif-item.warning { border-left-color: var(--warning); }
.notif-item.error { border-left-color: var(--error); }
.notif-msg { font-size: 12px; color: var(--text); line-height: 1.4; }
.notif-time { font-size: 10px; color: var(--text-muted); margin-top: 4px; }
.notif-empty {
    text-align: center; color: var(--text-muted);
    padding: 40px 20px; font-size: 13px;
}

/* ── Context Menu ───────────────────────────────────── */
.context-menu {
    position: fixed; z-index: 9999;
    background: var(--bg-surface);
    border: 1px solid var(--border-light);
    border-radius: var(--radius);
    padding: 6px;
    min-width: 180px;
    box-shadow: var(--shadow);
    backdrop-filter: blur(20px);
    display: none;
}
.context-menu.show { display: block; }
.ctx-item {
    padding: 8px 14px;
    border-radius: var(--radius-sm);
    cursor: pointer; font-size: 12px;
    color: var(--text-sec);
    transition: all 0.15s ease;
    display: flex; align-items: center; gap: 10px;
}
.ctx-item:hover { background: rgba(255,255,255,0.06); color: var(--text); }
.ctx-sep {
    height: 1px; background: var(--border);
    margin: 4px 8px;
}

/* ── Snap Preview ───────────────────────────────────── */
.snap-preview {
    position: fixed; z-index: 8;
    background: rgba(99,102,241,0.08);
    border: 2px solid rgba(99,102,241,0.3);
    border-radius: var(--radius);
    display: none;
    pointer-events: none;
    transition: all 0.2s ease;
}

/* ── App Styles — Shared ────────────────────────────── */
.app-toolbar {
    display: flex; gap: 6px;
    margin-bottom: 14px;
    flex-wrap: wrap;
}
.app-btn {
    padding: 7px 14px;
    background: rgba(255,255,255,0.05);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    color: var(--text-sec); font-size: 12px;
    cursor: pointer; transition: all 0.15s ease;
    font-family: inherit;
}
.app-btn:hover { background: rgba(255,255,255,0.1); color: var(--text); }
.app-btn.primary { background: var(--accent); border-color: var(--accent); color: white; }
.app-btn.primary:hover { background: var(--accent-hover); }
.app-btn.danger { color: var(--error); }
.app-btn.danger:hover { background: var(--error-bg); }

/* ── GPIO Cards ─────────────────────────────────────── */
.gpio-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(230px, 1fr));
    gap: 10px;
}
.gpio-card {
    background: rgba(255,255,255,0.02);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px;
    transition: all var(--transition);
}
.gpio-card:hover { border-color: rgba(255,255,255,0.15); background: rgba(255,255,255,0.04); }
.gpio-top {
    display: flex; justify-content: space-between;
    align-items: center; margin-bottom: 10px;
}
.gpio-name { font-size: 13px; font-weight: 600; }
.gpio-badge {
    font-size: 10px; padding: 3px 8px;
    border-radius: 20px; font-weight: 600;
    text-transform: uppercase; letter-spacing: 0.5px;
}
.gpio-badge.output { background: var(--accent-glow); color: var(--accent-hover); }
.gpio-badge.input { background: var(--info-bg); color: var(--info); }
.gpio-badge.pwm { background: var(--warning-bg); color: var(--warning); }
.gpio-badge.servo { background: var(--success-bg); color: var(--success); }
.gpio-label {
    font-size: 11px; color: var(--text-muted);
    margin-bottom: 10px;
}
.gpio-controls {
    display: flex; gap: 6px;
    flex-wrap: wrap; align-items: center;
}
.toggle-sw {
    position: relative; width: 44px; height: 24px;
    cursor: pointer;
}
.toggle-sw input { display: none; }
.toggle-track {
    position: absolute; inset: 0;
    background: var(--text-muted);
    border-radius: 12px;
    transition: background 0.2s ease;
}
.toggle-sw input:checked + .toggle-track { background: var(--success); }
.toggle-knob {
    position: absolute; top: 2px; left: 2px;
    width: 20px; height: 20px;
    background: white; border-radius: 50%;
    transition: transform 0.2s ease;
    box-shadow: 0 1px 3px rgba(0,0,0,0.3);
}
.toggle-sw input:checked ~ .toggle-knob { transform: translateX(20px); }
.pwm-range {
    width: 100%; height: 4px;
    -webkit-appearance: none; appearance: none;
    background: rgba(255,255,255,0.1);
    border-radius: 2px; outline: none;
    cursor: pointer;
}
.pwm-range::-webkit-slider-thumb {
    -webkit-appearance: none; width: 16px; height: 16px;
    background: var(--accent); border-radius: 50%;
    cursor: pointer; border: 2px solid white;
}
.servo-input {
    width: 70px; padding: 6px 8px;
    background: rgba(255,255,255,0.04);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    color: var(--text); font-size: 12px;
    text-align: center; outline: none;
}
.servo-input:focus { border-color: var(--accent); }
.gpio-mode-sel {
    padding: 5px 8px; font-size: 11px;
    background: rgba(255,255,255,0.04);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    color: var(--text); outline: none;
    cursor: pointer;
}
.gpio-state {
    font-size: 12px; font-weight: 600;
    padding: 4px 10px; border-radius: 20px;
}
.gpio-state.high { background: var(--success-bg); color: var(--success); }
.gpio-state.low { background: rgba(255,255,255,0.05); color: var(--text-muted); }

/* ── Terminal ───────────────────────────────────────── */
.term-wrap {
    display: flex; flex-direction: column;
    height: 100%;
    gap: 12px;
}
.term-head {
    display: flex; align-items: center; justify-content: space-between;
    gap: 12px; padding: 12px 14px;
    border: 1px solid var(--border);
    border-radius: var(--radius-lg);
    background: linear-gradient(180deg, rgba(255,255,255,0.05), rgba(255,255,255,0.02));
}
.term-head .term-title {
    display: flex; flex-direction: column;
    gap: 2px;
}
.term-head .term-title strong {
    font-size: 13px;
}
.term-head .term-title span {
    font-size: 11px; color: var(--text-muted);
}
.term-shell {
    display: flex; flex-direction: column;
    flex: 1; min-height: 0;
    border: 1px solid var(--border);
    border-radius: var(--radius-lg);
    overflow: hidden;
    background: linear-gradient(180deg, rgba(0,0,0,0.92), rgba(0,0,0,0.84));
    box-shadow: inset 0 1px 0 rgba(255,255,255,0.04);
}
.term-output {
    flex: 1; overflow-y: auto;
    padding: 14px 16px;
    font-family: var(--font-mono);
    font-size: 12px; color: #d1fae5;
    line-height: 1.55;
    min-height: 200px;
}
.term-output::-webkit-scrollbar { width: 4px; }
.term-output::-webkit-scrollbar-thumb { background: #334155; border-radius: 2px; }
.term-line { word-break: break-all; margin-bottom: 2px; white-space: pre-wrap; }
.term-line.err { color: var(--error); }
.term-line.ok { color: var(--success); }
.term-line.warn { color: var(--warning); }
.term-line.info { color: var(--info); }
.term-line.cmd { color: #aaa; }
.term-entry {
    display: flex; align-items: center; gap: 10px;
    padding: 12px 14px;
    border-top: 1px solid rgba(255,255,255,0.05);
    background: rgba(255,255,255,0.02);
}
.term-prompt {
    display: inline-flex; align-items: center; justify-content: center;
    min-width: 44px; height: 38px;
    padding: 0 12px;
    border-radius: 12px;
    border: 1px solid rgba(255,255,255,0.08);
    background: rgba(255,255,255,0.04);
    color: var(--accent);
    font-family: var(--font-mono);
    font-weight: 700;
    font-size: 12px;
}
.term-input {
    flex: 1; padding: 10px 14px;
    background: rgba(255,255,255,0.03);
    border: 1px solid rgba(255,255,255,0.08);
    border-radius: 12px;
    color: var(--text); font-family: var(--font-mono);
    font-size: 13px; outline: none;
}
.term-input:focus { border-color: var(--accent); background: rgba(255,255,255,0.05); }
.term-send {
    min-width: 92px;
    padding: 10px 18px;
    background: var(--accent);
    border: none; border-radius: var(--radius);
    color: white; font-size: 13px; font-weight: 600;
    cursor: pointer; transition: all 0.15s ease;
}
.term-send:hover { background: var(--accent-hover); }

/* ── File Manager ───────────────────────────────────── */
.fm-storage {
    background: rgba(255,255,255,0.03);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px; margin-bottom: 14px;
}
.fm-bar-wrap {
    width: 100%; height: 6px;
    background: rgba(255,255,255,0.06);
    border-radius: 3px; overflow: hidden;
    margin: 8px 0;
}
.fm-bar {
    height: 100%; border-radius: 3px;
    background: linear-gradient(90deg, var(--accent), var(--accent2));
    transition: width 0.5s ease;
}
.fm-stats { font-size: 11px; color: var(--text-muted); display: flex; justify-content: space-between; }
.fm-upload-zone {
    border: 2px dashed var(--border);
    border-radius: var(--radius);
    padding: 30px; text-align: center;
    margin-bottom: 14px;
    transition: all 0.2s ease;
    cursor: pointer;
}
.fm-upload-zone:hover, .fm-upload-zone.drag-over {
    border-color: var(--accent);
    background: rgba(99,102,241,0.05);
}
.fm-upload-zone p { color: var(--text-sec); font-size: 13px; }
.fm-upload-zone input { display: none; }
.file-list { display: flex; flex-direction: column; gap: 6px; }
.file-item {
    display: flex; align-items: center;
    padding: 10px 14px; gap: 12px;
    background: rgba(255,255,255,0.02);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    transition: all 0.15s ease;
}
.file-item:hover { background: rgba(255,255,255,0.04); border-color: rgba(255,255,255,0.15); }
.file-icon { font-size: 20px; flex-shrink: 0; }
.file-details { flex: 1; min-width: 0; }
.file-name { font-size: 13px; font-weight: 500; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.file-size { font-size: 11px; color: var(--text-muted); margin-top: 2px; }
.file-actions { display: flex; gap: 4px; flex-shrink: 0; }
.file-actions button {
    padding: 5px 10px; font-size: 11px;
    background: rgba(255,255,255,0.05);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    color: var(--text-sec); cursor: pointer;
    transition: all 0.15s ease;
}
.file-actions button:hover { background: rgba(255,255,255,0.1); color: var(--text); }

.fm-shell {
    display: grid;
    grid-template-columns: 280px 1fr;
    gap: 14px;
    height: 100%;
}
.fm-sidebar,
.fm-main,
.editor-shell,
.sched-shell,
.calc-shell,
.cal-shell {
    min-height: 0;
}
.fm-sidebar {
    display: flex;
    flex-direction: column;
    gap: 12px;
}
.fm-main {
    display: flex;
    flex-direction: column;
    gap: 12px;
    min-width: 0;
}
.fm-summary {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 10px;
}
.fm-stat {
    background: rgba(255,255,255,0.03);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 12px;
}
.fm-stat-val {
    font-size: 18px;
    font-weight: 700;
    color: var(--text);
}
.fm-stat-lbl {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.7px;
    color: var(--text-muted);
    margin-top: 4px;
}
.fm-upload-zone.compact {
    padding: 18px;
    margin-bottom: 0;
}
.fm-upload-zone .zone-title {
    font-size: 14px;
    font-weight: 600;
    margin-top: 8px;
}
.fm-upload-zone .zone-sub {
    font-size: 12px;
    color: var(--text-muted);
    margin-top: 4px;
}
.fm-files-header,
.editor-header,
.sched-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 10px;
}
.fm-files-header h4,
.editor-title,
.sched-title {
    font-size: 14px;
    font-weight: 700;
}
.fm-files-header span,
.editor-subtitle,
.sched-subtitle {
    font-size: 12px;
    color: var(--text-muted);
}

/* ── System Monitor ─────────────────────────────────── */
.stats-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(150px, 1fr));
    gap: 10px; margin-bottom: 16px;
}
.stat-card {
    background: rgba(255,255,255,0.03);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px;
}
.stat-val {
    font-size: 20px; font-weight: 700;
    color: var(--accent);
    margin-bottom: 4px;
}
.stat-lbl {
    font-size: 10px; text-transform: uppercase;
    letter-spacing: 0.8px; color: var(--text-muted);
}
.chart-area {
    background: rgba(255,255,255,0.02);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px; margin-bottom: 14px;
}
.chart-area h4 { font-size: 12px; color: var(--text-sec); margin-bottom: 10px; font-weight: 600; }
.chart-area canvas { width: 100%; height: 80px; display: block; }

/* ── WiFi Manager ───────────────────────────────────── */
.wifi-status-card {
    background: rgba(255,255,255,0.03);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 16px; margin-bottom: 14px;
}
.wifi-connected { border-left: 3px solid var(--success); }
.wifi-disconnected { border-left: 3px solid var(--error); }
.wifi-info-row { display: flex; justify-content: space-between; padding: 4px 0; font-size: 12px; }
.wifi-info-label { color: var(--text-muted); }
.wifi-info-val { color: var(--text); font-weight: 500; }
.wifi-network-item {
    display: flex; align-items: center;
    padding: 10px 14px; gap: 12px;
    background: rgba(255,255,255,0.02);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    margin-bottom: 6px; cursor: pointer;
    transition: all 0.15s ease;
}
.wifi-network-item:hover { background: rgba(255,255,255,0.05); border-color: var(--accent); }
.wifi-signal { display: flex; gap: 2px; align-items: flex-end; }
.wifi-signal-bar {
    width: 4px; border-radius: 1px;
    background: var(--text-muted);
    transition: background 0.2s ease;
}
.wifi-signal-bar.active { background: var(--success); }
.wifi-connect-form {
    padding: 16px;
    background: rgba(255,255,255,0.03);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    margin-top: 12px;
}
.wifi-connect-form input {
    width: 100%; padding: 10px 14px;
    background: rgba(255,255,255,0.04);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    color: var(--text); font-size: 13px;
    outline: none; margin-bottom: 10px;
}
.wifi-connect-form input:focus { border-color: var(--accent); }

/* ── Settings ───────────────────────────────────────── */
.settings-section {
    margin-bottom: 20px;
}
.settings-section h3 {
    font-size: 13px; font-weight: 600;
    color: var(--text); margin-bottom: 10px;
    padding-bottom: 6px;
    border-bottom: 1px solid var(--border);
}
.setting-row {
    display: flex; justify-content: space-between;
    align-items: center; padding: 10px 0;
    border-bottom: 1px solid rgba(255,255,255,0.03);
}
.setting-label { font-size: 12px; color: var(--text-sec); }
.setting-value { font-size: 12px; color: var(--text); font-weight: 500; }
.setting-input {
    padding: 6px 10px;
    background: rgba(255,255,255,0.04);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    color: var(--text); font-size: 12px;
    outline: none; width: 140px;
}
.setting-input:focus { border-color: var(--accent); }
.color-picker {
    width: 36px; height: 28px;
    border: 2px solid var(--border);
    border-radius: var(--radius-sm);
    cursor: pointer; background: none;
    padding: 0;
}

/* ── Text Editor ────────────────────────────────────── */
.editor-toolbar {
    display: flex; gap: 6px; align-items: center;
    margin-bottom: 10px; flex-wrap: wrap;
}
.editor-shell {
    display: grid;
    grid-template-columns: 240px 1fr;
    gap: 14px;
    height: 100%;
}
.editor-side,
.editor-main,
.sched-card-panel,
.sched-board,
.sched-summary {
    min-width: 0;
}
.editor-side {
    display: flex;
    flex-direction: column;
    gap: 12px;
}
.editor-main {
    display: flex;
    flex-direction: column;
    gap: 12px;
}
.editor-panel,
.sched-panel,
.fm-panel {
    background: rgba(255,255,255,0.03);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px;
}
.editor-meta-grid,
.sched-summary {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 10px;
}
.mini-stat {
    background: rgba(255,255,255,0.03);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 10px 12px;
}
.mini-stat .value {
    font-size: 18px;
    font-weight: 700;
    color: var(--text);
}
.mini-stat .label {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.7px;
    color: var(--text-muted);
    margin-top: 4px;
}
.editor-textarea {
    min-height: 420px;
    line-height: 1.7;
    box-shadow: inset 0 1px 0 rgba(255,255,255,0.02);
}
.editor-toolbar .app-btn,
.sched-actions .app-btn,
.fm-actions .app-btn {
    display: inline-flex;
    align-items: center;
    gap: 6px;
}
.editor-filename {
    font-size: 12px; color: var(--text-sec);
    padding: 6px 12px;
    background: rgba(255,255,255,0.04);
    border-radius: var(--radius-sm);
    border: 1px solid var(--border);
}
.editor-textarea {
    width: 100%; min-height: 300px;
    background: #0a0a1a;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px;
    font-family: var(--font-mono);
    font-size: 13px; color: var(--text);
    line-height: 1.6; resize: vertical;
    outline: none;
    tab-size: 4;
}
.editor-textarea:focus { border-color: var(--accent); }
.editor-status {
    display: flex; justify-content: space-between;
    margin-top: 8px; font-size: 11px;
    color: var(--text-muted);
}

/* ── Task Scheduler ─────────────────────────────────── */
.sched-add {
    background: rgba(255,255,255,0.03);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 16px;
    margin-bottom: 14px;
}
.sched-add h4 { font-size: 13px; font-weight: 600; margin-bottom: 10px; }
.sched-form-grid {
    display: grid;
    grid-template-columns: 60px minmax(0, 1fr) 60px minmax(0, 1fr) 60px minmax(0, 1fr);
    gap: 8px;
    align-items: center;
}
.sched-form-row {
    display: flex; gap: 8px; align-items: center;
    margin-bottom: 8px; flex-wrap: wrap;
}
.sched-form-grid label,
.sched-form-row label { font-size: 12px; color: var(--text-sec); }
.sched-form-row input, .sched-form-row select {
    padding: 7px 10px;
    background: rgba(255,255,255,0.04);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    color: var(--text); font-size: 12px;
    outline: none;
}
.sched-form-grid input,
.sched-form-grid select {
    width: 100%;
    min-width: 0;
}
.sched-form-row input:focus, .sched-form-row select:focus { border-color: var(--accent); }
.sched-form-grid input:focus, .sched-form-grid select:focus { border-color: var(--accent); }
.sched-interval-wrap {
    display: flex;
    align-items: center;
    gap: 8px;
    min-width: 0;
}
.sched-interval-wrap input {
    width: 90px;
    flex-shrink: 0;
}
.sched-interval-wrap span {
    color: var(--text-sec);
    font-size: 12px;
    white-space: nowrap;
}
.sched-list { display: flex; flex-direction: column; gap: 8px; }
.sched-card {
    display: flex; align-items: center;
    padding: 12px 14px; gap: 12px;
    background: rgba(255,255,255,0.02);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    transition: all 0.15s ease;
}
.sched-card-actions {
    display: flex;
    align-items: center;
    gap: 8px;
    flex-shrink: 0;
}
.sched-card:hover { background: rgba(255,255,255,0.04); }
.sched-info { flex: 1; }
.sched-name { font-size: 13px; font-weight: 500; }
.sched-detail { font-size: 11px; color: var(--text-muted); margin-top: 2px; }
.sched-board {
    display: flex;
    flex-direction: column;
    gap: 14px;
    height: 100%;
}
.sched-card-panel,
.sched-board .sched-panel {
    display: flex;
    flex-direction: column;
    gap: 12px;
}
.sched-board .sched-list {
    flex: 1;
    overflow: auto;
    padding-right: 4px;
}

@media (max-width: 1100px) {
    .fm-shell,
    .editor-shell,
    .sched-board {
        grid-template-columns: 1fr;
    }
}

@media (max-width: 900px) {
    .sched-form-grid {
        grid-template-columns: 1fr;
    }
    .sched-form-grid label {
        margin-top: 4px;
    }
    .sched-interval-wrap input {
        width: 100%;
    }
}

/* ── Calculator ─────────────────────────────────────── */
.calc-shell {
    display: grid;
    grid-template-rows: auto auto 1fr;
    gap: 12px;
    height: 100%;
}
.calc-display {
    background: rgba(0,0,0,0.35);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 16px;
    display: flex;
    flex-direction: column;
    justify-content: end;
    min-height: 110px;
}
.calc-expression {
    color: var(--text-muted);
    font-size: 12px;
    min-height: 18px;
    text-align: right;
    word-break: break-all;
}
.calc-result {
    color: var(--text);
    font-size: 28px;
    font-weight: 700;
    text-align: right;
    word-break: break-all;
}
.calc-grid {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 8px;
}
.calc-btn {
    padding: 14px 0;
    border-radius: var(--radius);
    border: 1px solid var(--border);
    background: rgba(255,255,255,0.04);
    color: var(--text);
    font-size: 14px;
    font-weight: 600;
}
.calc-btn:hover { background: rgba(255,255,255,0.1); }
.calc-btn.op { color: var(--accent-hover); }
.calc-btn.eq { background: var(--accent); color: white; }
.calc-btn.eq:hover { background: var(--accent-hover); }
.calc-btn.fn { color: var(--warning); }

/* ── Calendar ───────────────────────────────────────── */
.cal-shell {
    display: grid;
    grid-template-columns: 1fr;
    height: 100%;
}
.cal-panel {
    background: rgba(255,255,255,0.03);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px;
}
.cal-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 8px;
    margin-bottom: 12px;
}
.cal-title {
    font-size: 15px;
    font-weight: 700;
}
.cal-nav {
    display: flex;
    gap: 6px;
}
.cal-nav button { padding: 6px 10px; }
.cal-grid {
    display: grid;
    grid-template-columns: repeat(7, 1fr);
    gap: 6px;
}
.cal-dow {
    text-align: center;
    font-size: 11px;
    color: var(--text-muted);
    padding: 6px 0;
    text-transform: uppercase;
    letter-spacing: 0.6px;
}
.cal-day {
    min-height: 54px;
    border-radius: 10px;
    border: 1px solid var(--border);
    background: rgba(255,255,255,0.02);
    padding: 8px;
    font-size: 12px;
    color: var(--text);
}
.cal-day.muted { color: var(--text-muted); opacity: 0.65; }
.cal-day.today {
    border-color: var(--accent);
    box-shadow: 0 0 0 1px rgba(99,102,241,0.15) inset;
}
.cal-day-num { font-weight: 700; }

@media (max-width: 900px) {
    .cal-shell { grid-template-columns: 1fr; }
}

/* ── Shared Helpers ─────────────────────────────────── */
.empty-state {
    text-align: center; padding: 40px;
    color: var(--text-muted); font-size: 13px;
}
.loading {
    text-align: center; padding: 30px;
    color: var(--text-muted); font-size: 13px;
}
.badge { padding: 2px 8px; border-radius: 20px; font-size: 10px; font-weight: 600; }
.section-title { font-size: 14px; font-weight: 600; margin-bottom: 12px; }

/* ── Responsive — Tablet ────────────────────────────── */
@media (max-width: 768px) {
    :root { --taskbar-h: 46px; }
    .desktop-icons {
        grid-template-columns: repeat(4, 72px);
        gap: 4px;
    }
    .desktop-icon { padding: 8px 4px; }
    .desktop-icon .icon { width: 40px; height: 40px; font-size: 20px; }
    .desktop-icon .label { font-size: 10px; }
    .win {
        position: fixed !important;
        top: 0 !important; left: 0 !important;
        width: 100% !important;
        height: calc(100% - var(--taskbar-h)) !important;
        border-radius: 0; min-width: unset;
    }
    .win-resize { display: none; }
    .start-menu {
        width: 100%;
        border-radius: var(--radius-lg) var(--radius-lg) 0 0;
        max-height: 80vh;
    }
    .notif-panel { width: 100%; }
    .sm-grid { grid-template-columns: 1fr 1fr; }
    .gpio-grid { grid-template-columns: 1fr; }
    .stats-grid { grid-template-columns: repeat(2, 1fr); }
    .fm-summary,
    .editor-meta-grid,
    .sched-summary {
        grid-template-columns: 1fr;
    }
    .sched-add .sched-form-row {
        grid-template-columns: 1fr;
    }
    .tb-app-btn { padding: 0 8px; font-size: 11px; }
    .tb-app-btn span { display: none; }
    .context-menu { display: none !important; }
}

/* ── Responsive — Phone ─────────────────────────────── */
@media (max-width: 480px) {
    :root { --taskbar-h: 44px; }
    .desktop-icons {
        grid-template-columns: repeat(3, 68px);
        top: 10px; left: 10px;
    }
    .desktop-icon .icon { width: 36px; height: 36px; font-size: 18px; border-radius: 10px; }
    .win-body { padding: 12px; }
    .sm-grid { grid-template-columns: 1fr; }
    .stats-grid { grid-template-columns: 1fr 1fr; }
    .tb-start-btn { width: 36px; height: 32px; font-size: 14px; }
    .tb-clock { font-size: 11px; padding: 0 4px; }
    .tb-tray { gap: 4px; }
    .login-card { padding: 28px 20px; }
}
)rawliteral";

// ═══════════════════════════════════════════════════════════════════════════════
//  PROGMEM HTML — Part 2: Body Structure
// ═══════════════════════════════════════════════════════════════════════════════
const char HTML_BODY[] PROGMEM = R"rawliteral(
</style>
</head>
<body>

<!-- Boot Screen -->
<div id="bootScreen">
    <div class="boot-logo">WebOS-8266</div>
    <div class="boot-sub">v1.0.0 — Browser Based Desktop OS for ESP8266</div>
    <div class="boot-bar-wrap"><div class="boot-bar" id="bootBar"></div></div>
    <div class="boot-status" id="bootStatus">Initializing...</div>
</div>

<!-- Login Screen -->
<div id="loginScreen">
    <div class="login-card">
        <h2>WebOS-8266</h2>
        <p>Sign in to continue</p>
        <input type="text" id="loginUser" placeholder="Username" autocomplete="off">
        <input type="password" id="loginPass" placeholder="Password">
        <button onclick="doLogin()">Sign In</button>
        <div class="login-error" id="loginError"></div>
    </div>
</div>

<!-- Desktop -->
<div id="desktop">
    <div class="desktop-area" id="desktopArea">
        <div class="desktop-icons" id="desktopIcons"></div>
        <div class="windows-layer" id="windowsLayer"></div>
        <div class="snap-preview" id="snapPreview"></div>
    </div>

    <!-- Taskbar -->
    <div class="taskbar">
        <button class="tb-start-btn" id="tbStartBtn" onclick="toggleStartMenu()" title="Start Menu" aria-label="Start Menu">&#9776;</button>
        <div class="tb-sep"></div>
        <div class="tb-apps" id="tbApps"></div>
        <div class="tb-tray">
            <div class="tb-tray-item tb-tray-wifi" id="trayWifi" title="WiFi status" aria-label="WiFi status"></div>
            <div class="tb-tray-item" id="trayMem" title="Memory">--</div>
            <div class="tb-tray-item" id="trayNotif" onclick="toggleNotifPanel()" title="Notifications">
                Notifications<span class="tb-notif-badge" id="notifBadge" style="display:none">0</span>
            </div>
            <div class="tb-clock" id="tbClock">--:--</div>
        </div>
    </div>
</div>

<!-- Start Menu -->
<div class="start-menu" id="startMenu">
    <div class="sm-header">
        <h3>WebOS-8266</h3>
        <p id="smUser">admin</p>
    </div>
    <input class="sm-search" type="text" placeholder="Search apps..." oninput="filterStartMenu(this.value)" id="smSearch">
    <div class="sm-section-label">Applications</div>
    <div class="sm-grid" id="smGrid"></div>
    <div class="sm-footer">
        <button onclick="window.location.reload()">Refresh</button>
        <button class="sm-power" onclick="doReboot()">Reboot</button>
    </div>
</div>

<!-- Notification Panel -->
<div class="notif-panel" id="notifPanel">
    <div class="notif-header">
        <h3>Notifications</h3>
        <button class="notif-clear-btn" onclick="clearNotifs()">Clear All</button>
    </div>
    <div class="notif-list" id="notifList"></div>
</div>

<!-- Context Menu -->
<div class="context-menu" id="ctxMenu">
    <div class="ctx-item" onclick="refreshDesktop()">Refresh</div>
    <div class="ctx-sep"></div>
    <div class="ctx-item" onclick="openApp('settings')">Settings</div>
    <div class="ctx-item" onclick="openApp('monitor')">System Info</div>
    <div class="ctx-sep"></div>
    <div class="ctx-item" onclick="openApp('terminal')">Terminal</div>
</div>

<script>
)rawliteral";

// ═══════════════════════════════════════════════════════════════════════════════
//  PROGMEM JavaScript — Part 1
// ═══════════════════════════════════════════════════════════════════════════════
const char HTML_JS1[] PROGMEM = R"rawliteral(
// ═══════════════════════════════════════════════════════════
//  STATE
// ═══════════════════════════════════════════════════════════
let authToken = localStorage.getItem('mwos_token') || '';
let wins = {};
let zIdx = 100;
let focusedWinId = null;
let startMenuOpen = false;
let notifPanelOpen = false;
let cmdHistory = [];
let cmdHistIdx = -1;
let monitorInterval = null;
let heapHistory = [];

const APPS = {
    gpio:      {title:'GPIO Control',    icon:'bolt',     cat:'Hardware'},
    terminal:  {title:'Terminal',        icon:'terminal', cat:'Tools'},
    files:     {title:'File Manager',    icon:'folder',   cat:'Tools'},
    monitor:   {title:'System Monitor',  icon:'chart',    cat:'System'},
    wifi:      {title:'WiFi Manager',    icon:'wifi',     cat:'System'},
    settings:  {title:'Settings',        icon:'gear',     cat:'System'},
    editor:    {title:'Text Editor',     icon:'doc',      cat:'Tools'},
    calculator:{title:'Calculator',      icon:'calculator', cat:'Tools'},
    calendar:  {title:'Calendar',        icon:'calendar', cat:'Tools'},
    scheduler: {title:'Task Scheduler',  icon:'clock',    cat:'Hardware'}
};

const ICONS = {
    bolt: '<path d="M13 2 4 14h6l-1 8 9-12h-6z"/>',
    terminal: '<path d="M4 6h16v12H4z"/><path d="m7 10 3 2-3 2"/><path d="M12 14h5"/>',
    folder: '<path d="M3 7h6l2 2h10v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
    chart: '<path d="M4 20V4"/><path d="M4 20h16"/><path d="m7 15 3-4 3 2 4-6"/>',
    wifi: '<path d="M2 8a16 16 0 0 1 20 0"/><path d="M5 12a11 11 0 0 1 14 0"/><path d="M8 16a6 6 0 0 1 8 0"/><circle cx="12" cy="19" r="1"/>',
    gear: '<circle cx="12" cy="12" r="3"/><path d="M19 12a7 7 0 0 0-.1-1l2-1.5-2-3.5-2.4.8a7 7 0 0 0-1.7-1L14.5 3h-5L9.2 5.8a7 7 0 0 0-1.7 1l-2.4-.8-2 3.5L5 11a7 7 0 0 0 0 2l-1.9 1.5 2 3.5 2.4-.8a7 7 0 0 0 1.7 1l.3 2.8h5l.3-2.8a7 7 0 0 0 1.7-1l2.4.8 2-3.5L18.9 13c.1-.3.1-.7.1-1z"/>',
    doc: '<path d="M7 3h7l4 4v14H7z"/><path d="M14 3v4h4"/><path d="M10 12h6"/><path d="M10 16h6"/>',
    clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v6l4 2"/>',
    calculator: '<rect x="5" y="3" width="14" height="18" rx="2"/><path d="M8 7h8"/><path d="M8 11h2"/><path d="M12 11h2"/><path d="M16 11h-2"/><path d="M8 15h2"/><path d="M12 15h2"/><path d="M16 15h-2"/><path d="M8 19h2"/><path d="M12 19h2"/><path d="M16 19h-2"/>',
    calendar: '<rect x="3" y="5" width="18" height="16" rx="2"/><path d="M3 9h18"/><path d="M8 3v4"/><path d="M16 3v4"/><path d="M8 13h1"/><path d="M12 13h1"/><path d="M16 13h1"/><path d="M8 17h1"/><path d="M12 17h1"/>',
    refresh: '<path d="M20 11a8 8 0 1 0 1 4"/><path d="M20 4v7h-7"/>',
    power: '<path d="M12 2v10"/><path d="M7 5a7 7 0 1 0 10 0"/>',
    bell: '<path d="M6 8a6 6 0 1 1 12 0v5l2 2H4l2-2z"/><path d="M10 17a2 2 0 0 0 4 0"/>',
    upload: '<path d="M12 16V6"/><path d="m8 10 4-4 4 4"/><path d="M4 18h16"/>',
    download: '<path d="M12 6v10"/><path d="m8 12 4 4 4-4"/><path d="M4 18h16"/>',
    close: '<path d="M6 6l12 12"/><path d="M18 6 6 18"/>',
    monitor2: '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8"/>'
};

function icon(name, cls = '') {
    const p = ICONS[name] || ICONS.doc;
    const classAttr = cls ? ` class="${cls}"` : '';
    return `<svg${classAttr} viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${p}</svg>`;
}

const APP_LAYOUTS = {
    scheduler: { width: 840, height: 600, x: 48, y: 56 },
    calculator: { width: 380, height: 540, x: 120, y: 70 },
    calendar: { width: 760, height: 560, x: 90, y: 64 }
};

// Drag state
let isDragging = false, dragWin = null, dragOX = 0, dragOY = 0;
// Resize state
let isResizing = false, resizeWin = null, resizeOX = 0, resizeOY = 0, resizeOW = 0, resizeOH = 0;
// Mobile check
const isMobile = window.innerWidth <= 768;

// ═══════════════════════════════════════════════════════════
//  API HELPER
// ═══════════════════════════════════════════════════════════
async function api(url, opts = {}) {
    const headers = opts.headers || {};
    if (authToken) headers['X-Auth-Token'] = authToken;
    try {
        const r = await fetch(url, {...opts, headers});
        if (r.status === 401) { showLogin(); return null; }
        return r;
    } catch(e) { console.error('API error:', e); return null; }
}
async function apiJson(url, opts = {}) {
    const r = await api(url, opts);
    if (!r) return null;
    try { return await r.json(); } catch(e) { return null; }
}

// ═══════════════════════════════════════════════════════════
//  BOOT SEQUENCE
// ═══════════════════════════════════════════════════════════
async function bootSequence() {
    const bar = document.getElementById('bootBar');
    const status = document.getElementById('bootStatus');

    const steps = [
        {pct: 15, msg: 'Initializing hardware...'},
        {pct: 35, msg: 'Mounting filesystem...'},
        {pct: 55, msg: 'Connecting to network...'},
        {pct: 75, msg: 'Loading desktop environment...'},
        {pct: 90, msg: 'Starting services...'},
        {pct: 100, msg: 'Ready!'}
    ];

    for (const s of steps) {
        bar.style.width = s.pct + '%';
        status.textContent = s.msg;
        await sleep(400);
    }

    await sleep(300);
    document.getElementById('bootScreen').classList.add('hidden');
    await sleep(600);
    document.getElementById('bootScreen').style.display = 'none';

    // Check auth
    const info = await apiJson('/api/system/info');
    if (info && info.authEnabled) {
        if (!authToken) { showLogin(); return; }
        const check = await apiJson('/api/system/info');
        if (!check || check.error) { showLogin(); return; }
    }
    showDesktop();
}

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

// ═══════════════════════════════════════════════════════════
//  AUTH
// ═══════════════════════════════════════════════════════════
function showLogin() {
    document.getElementById('loginScreen').classList.add('active');
}

async function doLogin() {
    const user = document.getElementById('loginUser').value.trim();
    const pass = document.getElementById('loginPass').value;
    const errEl = document.getElementById('loginError');
    errEl.textContent = '';

    if (!user || !pass) { errEl.textContent = 'Please enter credentials'; return; }

    const r = await fetch('/api/auth/login', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({username: user, password: pass})
    });

    if (r.ok) {
        const data = await r.json();
        authToken = data.token;
        localStorage.setItem('mwos_token', authToken);
        document.getElementById('loginScreen').classList.remove('active');
        showDesktop();
    } else {
        errEl.textContent = 'Invalid username or password';
    }
}

// ═══════════════════════════════════════════════════════════
//  DESKTOP
// ═══════════════════════════════════════════════════════════
function showDesktop() {
    document.getElementById('desktop').classList.add('active');
    renderDesktopIcons();
    renderStartMenu();
    updateTray();
    setInterval(updateClock, 1000);
    setInterval(updateTray, 5000);
    setInterval(pollNotifications, 10000);
    updateClock();
    pollNotifications();
}

function renderDesktopIcons() {
    const c = document.getElementById('desktopIcons');
    c.innerHTML = '';
    for (const [id, app] of Object.entries(APPS)) {
        const el = document.createElement('div');
        el.className = 'desktop-icon';
        el.onclick = () => openApp(id);
        el.innerHTML = `<div class="icon">${icon(app.icon)}</div><div class="label">${app.title}</div>`;
        c.appendChild(el);
    }
}

function renderStartMenu() {
    const grid = document.getElementById('smGrid');
    grid.innerHTML = '';
    for (const [id, app] of Object.entries(APPS)) {
        const el = document.createElement('div');
        el.className = 'sm-item';
        el.dataset.name = app.title.toLowerCase();
        el.onclick = () => { openApp(id); toggleStartMenu(); };
        el.innerHTML = `<div class="sm-icon">${icon(app.icon)}</div><div class="sm-label">${app.title}</div>`;
        grid.appendChild(el);
    }
}

function filterStartMenu(q) {
    const items = document.querySelectorAll('.sm-item');
    const qLow = q.toLowerCase();
    items.forEach(it => {
        it.style.display = it.dataset.name.includes(qLow) ? '' : 'none';
    });
}

function toggleStartMenu() {
    startMenuOpen = !startMenuOpen;
    document.getElementById('startMenu').classList.toggle('open', startMenuOpen);
    document.getElementById('tbStartBtn').classList.toggle('active', startMenuOpen);
    if (startMenuOpen && notifPanelOpen) toggleNotifPanel();
    if (startMenuOpen) document.getElementById('smSearch').focus();
}

function toggleNotifPanel() {
    notifPanelOpen = !notifPanelOpen;
    document.getElementById('notifPanel').classList.toggle('open', notifPanelOpen);
    if (notifPanelOpen && startMenuOpen) toggleStartMenu();
}

function refreshDesktop() {
    closeContextMenu();
    window.location.reload();
}

// ═══════════════════════════════════════════════════════════
//  WINDOW MANAGER
// ═══════════════════════════════════════════════════════════
function openApp(id) {
    if (wins[id]) {
        if (wins[id].minimized) {
            wins[id].minimized = false;
            wins[id].el.classList.remove('minimized');
        }
        focusWindow(id);
        return;
    }
    createWindow(id);
}

function createWindow(id) {
    const app = APPS[id];
    if (!app) return;

    const el = document.createElement('div');
    el.className = 'win focused';
    el.id = 'win-' + id;
    el.style.zIndex = zIdx++;

    if (isMobile) {
        el.style.top = '0'; el.style.left = '0';
        el.style.width = '100%';
        el.style.height = '100%';
    } else {
        const layout = APP_LAYOUTS[id];
        const w = layout ? layout.width : Math.min(720, window.innerWidth - 100);
        const h = layout ? layout.height : Math.min(480, window.innerHeight - 140);
        el.style.width = w + 'px'; el.style.height = h + 'px';
        el.style.left = (layout ? layout.x : 60 + Object.keys(wins).length * 30) + 'px';
        el.style.top = (layout ? layout.y : 40 + Object.keys(wins).length * 30) + 'px';
    }

    el.innerHTML = `
        <div class="win-titlebar">
            <div class="win-titlebar-drag" data-wid="${id}">
                <span class="win-title-icon">${icon(app.icon)}</span>
                <span class="win-title">${app.title}</span>
            </div>
            <div class="win-controls">
                <button class="win-ctrl" onclick="minimizeWin('${id}')" title="Minimize">&#8722;</button>
                <button class="win-ctrl" onclick="maximizeWin('${id}')" title="Maximize">&#9633;</button>
                <button class="win-ctrl close" onclick="closeWin('${id}')" title="Close">&#10005;</button>
            </div>
        </div>
        <div class="win-body" id="wb-${id}"></div>
        ${isMobile ? '' : '<div class="win-resize" data-wid="' + id + '"></div>'}
    `;

    document.getElementById('windowsLayer').appendChild(el);

    wins[id] = { el, minimized: false, maximized: false };
    focusWindow(id);
    updateTaskbarApps();
    loadApp(id);

    // Drag events for titlebar
    const drag = el.querySelector('.win-titlebar-drag');
    drag.addEventListener('mousedown', e => startDrag(e, id));
    drag.addEventListener('touchstart', e => startDragTouch(e, id), {passive:false});

    // Resize events
    const rh = el.querySelector('.win-resize');
    if (rh) {
        rh.addEventListener('mousedown', e => startResize(e, id));
        rh.addEventListener('touchstart', e => startResizeTouch(e, id), {passive:false});
    }

    // Focus on click
    el.addEventListener('mousedown', () => focusWindow(id));
    el.addEventListener('touchstart', () => focusWindow(id), {passive:true});
}

function closeWin(id) {
    if (!wins[id]) return;
    const el = wins[id].el;
    el.classList.add('closing');
    if (id === 'monitor' && monitorInterval) { clearInterval(monitorInterval); monitorInterval = null; }
    setTimeout(() => {
        el.remove();
        delete wins[id];
        updateTaskbarApps();
        if (focusedWinId === id) focusedWinId = null;
    }, 200);
}

function minimizeWin(id) {
    if (!wins[id]) return;
    wins[id].minimized = true;
    wins[id].el.classList.add('minimized');
    updateTaskbarApps();
}

function maximizeWin(id) {
    if (!wins[id]) return;
    const w = wins[id];
    if (w.maximized) {
        w.el.classList.remove('maximized');
        w.maximized = false;
    } else {
        w.el.classList.remove('snapped-left', 'snapped-right');
        w.el.classList.add('maximized');
        w.maximized = true;
    }
}

function focusWindow(id) {
    if (focusedWinId && wins[focusedWinId]) {
        wins[focusedWinId].el.classList.remove('focused');
    }
    focusedWinId = id;
    if (wins[id]) {
        wins[id].el.classList.add('focused');
        wins[id].el.style.zIndex = zIdx++;
    }
    updateTaskbarApps();
}

// ═══════════════════════════════════════════════════════════
//  DRAG & RESIZE
// ═══════════════════════════════════════════════════════════
function startDrag(e, id) {
    if (wins[id].maximized || isMobile) return;
    isDragging = true; dragWin = id;
    dragOX = e.clientX - wins[id].el.offsetLeft;
    dragOY = e.clientY - wins[id].el.offsetTop;
    e.preventDefault();
}
function startDragTouch(e, id) {
    if (wins[id].maximized || isMobile) return;
    isDragging = true; dragWin = id;
    const t = e.touches[0];
    dragOX = t.clientX - wins[id].el.offsetLeft;
    dragOY = t.clientY - wins[id].el.offsetTop;
    e.preventDefault();
}

function startResize(e, id) {
    if (wins[id].maximized) return;
    isResizing = true; resizeWin = id;
    resizeOX = e.clientX; resizeOY = e.clientY;
    resizeOW = wins[id].el.offsetWidth;
    resizeOH = wins[id].el.offsetHeight;
    e.preventDefault(); e.stopPropagation();
}
function startResizeTouch(e, id) {
    if (wins[id].maximized) return;
    isResizing = true; resizeWin = id;
    const t = e.touches[0];
    resizeOX = t.clientX; resizeOY = t.clientY;
    resizeOW = wins[id].el.offsetWidth;
    resizeOH = wins[id].el.offsetHeight;
    e.preventDefault(); e.stopPropagation();
}

document.addEventListener('mousemove', e => {
    if (isDragging && dragWin && wins[dragWin]) {
        const el = wins[dragWin].el;
        const x = e.clientX - dragOX;
        const y = Math.max(0, e.clientY - dragOY);
        el.style.left = x + 'px'; el.style.top = y + 'px';
        el.classList.remove('snapped-left', 'snapped-right');
        showSnapPreview(e.clientX, e.clientY);
    }
    if (isResizing && resizeWin && wins[resizeWin]) {
        const el = wins[resizeWin].el;
        const w = Math.max(340, resizeOW + (e.clientX - resizeOX));
        const h = Math.max(220, resizeOH + (e.clientY - resizeOY));
        el.style.width = w + 'px'; el.style.height = h + 'px';
    }
});

document.addEventListener('touchmove', e => {
    const t = e.touches[0];
    if (isDragging && dragWin && wins[dragWin]) {
        const el = wins[dragWin].el;
        el.style.left = (t.clientX - dragOX) + 'px';
        el.style.top = Math.max(0, t.clientY - dragOY) + 'px';
    }
    if (isResizing && resizeWin && wins[resizeWin]) {
        const el = wins[resizeWin].el;
        el.style.width = Math.max(340, resizeOW + (t.clientX - resizeOX)) + 'px';
        el.style.height = Math.max(220, resizeOH + (t.clientY - resizeOY)) + 'px';
    }
}, {passive: false});

document.addEventListener('mouseup', e => {
    if (isDragging && dragWin) {
        applySnap(e.clientX, e.clientY, dragWin);
        hideSnapPreview();
    }
    isDragging = false; dragWin = null;
    isResizing = false; resizeWin = null;
});

document.addEventListener('touchend', () => {
    isDragging = false; dragWin = null;
    isResizing = false; resizeWin = null;
    hideSnapPreview();
});

function showSnapPreview(mx, my) {
    const sp = document.getElementById('snapPreview');
    const area = document.getElementById('desktopArea');
    const W = area.clientWidth, H = area.clientHeight;
    if (mx < 30) {
        sp.style.display = 'block';
        sp.style.left = '0'; sp.style.top = '0';
        sp.style.width = '50%'; sp.style.height = H + 'px';
    } else if (mx > window.innerWidth - 30) {
        sp.style.display = 'block';
        sp.style.left = '50%'; sp.style.top = '0';
        sp.style.width = '50%'; sp.style.height = H + 'px';
    } else if (my < 10) {
        sp.style.display = 'block';
        sp.style.left = '0'; sp.style.top = '0';
        sp.style.width = '100%'; sp.style.height = H + 'px';
    } else {
        sp.style.display = 'none';
    }
}

function hideSnapPreview() {
    document.getElementById('snapPreview').style.display = 'none';
}

function applySnap(mx, my, id) {
    if (!wins[id]) return;
    const el = wins[id].el;
    if (mx < 30) {
        el.classList.add('snapped-left'); wins[id].maximized = false;
    } else if (mx > window.innerWidth - 30) {
        el.classList.add('snapped-right'); wins[id].maximized = false;
    } else if (my < 10) {
        maximizeWin(id);
    }
}

// ═══════════════════════════════════════════════════════════
//  TASKBAR
// ═══════════════════════════════════════════════════════════
function updateTaskbarApps() {
    const c = document.getElementById('tbApps');
    c.innerHTML = '';
    for (const [id, w] of Object.entries(wins)) {
        const app = APPS[id];
        if (!app) continue;
        const btn = document.createElement('button');
        btn.className = 'tb-app-btn';
        if (id === focusedWinId && !w.minimized) btn.classList.add('active');
        if (w.minimized) btn.classList.add('minimized');
        btn.innerHTML = `${icon(app.icon, 'tb-app-icon')} <span>${app.title}</span>`;
        btn.onclick = () => {
            if (w.minimized) { minimizeWin(id); w.minimized = false; w.el.classList.remove('minimized'); focusWindow(id); }
            else if (id === focusedWinId) minimizeWin(id);
            else focusWindow(id);
        };
        c.appendChild(btn);
    }
}

function updateClock() {
    const now = new Date();
    const h = String(now.getHours()).padStart(2, '0');
    const m = String(now.getMinutes()).padStart(2, '0');
    document.getElementById('tbClock').textContent = h + ':' + m;
}

async function updateTray() {
    const info = await apiJson('/api/system/info');
    if (!info) return;
    const memEl = document.getElementById('trayMem');
    const wifiEl = document.getElementById('trayWifi');
    if (info.freeHeap) {
        const pct = Math.round((1 - info.freeHeap / 81920) * 100);
        memEl.textContent = pct + '%';
        memEl.title = 'Memory: ' + Math.round(info.freeHeap / 1024) + 'KB free';
    }
    if (info.wifiRSSI !== undefined) {
        const rssi = info.wifiRSSI;
        const level = rssi > -50 ? 'good' : rssi > -60 ? 'fair' : rssi > -70 ? 'weak' : 'off';
        wifiEl.className = 'tb-tray-item tb-tray-wifi ' + level;
        wifiEl.innerHTML = `<span class="tb-wifi-icon">${rssi > -80 ? '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M2 8a16 16 0 0 1 20 0"/><path d="M5 12a11 11 0 0 1 14 0"/><path d="M8 16a6 6 0 0 1 8 0"/><circle cx="12" cy="19" r="1.3"/></svg>' : '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M2 8a16 16 0 0 1 20 0"/><path d="M5 12a11 11 0 0 1 9.5-3.2"/><path d="M8.5 16a6 6 0 0 1 4.5-1.9"/><path d="M3 3l18 18"/></svg>'}</span>`;
        wifiEl.title = 'WiFi: ' + rssi + ' dBm';
    }
}

async function pollNotifications() {
    const data = await apiJson('/api/notifications');
    if (!data || !data.notifications) return;
    const list = document.getElementById('notifList');
    const badge = document.getElementById('notifBadge');
    const unread = data.notifications.filter(n => !n.read).length;
    badge.style.display = unread > 0 ? 'flex' : 'none';
    badge.textContent = unread;

    if (data.notifications.length === 0) {
        list.innerHTML = '<div class="notif-empty">No notifications</div>';
        return;
    }
    list.innerHTML = data.notifications.map(n => `
        <div class="notif-item ${n.type}">
            <div class="notif-msg">${n.message}</div>
            <div class="notif-time">${formatUptime(n.ts)}</div>
        </div>
    `).join('');
}

async function clearNotifs() {
    await api('/api/notifications/clear', {method: 'POST'});
    document.getElementById('notifList').innerHTML = '<div class="notif-empty">No notifications</div>';
    document.getElementById('notifBadge').style.display = 'none';
}

function formatUptime(ms) {
    const s = Math.floor(ms / 1000);
    if (s < 60) return s + 's ago';
    if (s < 3600) return Math.floor(s / 60) + 'm ago';
    return Math.floor(s / 3600) + 'h ago';
}

// ═══════════════════════════════════════════════════════════
//  CONTEXT MENU
// ═══════════════════════════════════════════════════════════
document.addEventListener('contextmenu', e => {
    if (isMobile) return;
    if (e.target.closest('.win') || e.target.closest('.taskbar') || e.target.closest('.start-menu')) return;
    e.preventDefault();
    const menu = document.getElementById('ctxMenu');
    menu.style.left = e.clientX + 'px'; menu.style.top = e.clientY + 'px';
    menu.classList.add('show');
});

document.addEventListener('click', e => {
    closeContextMenu();
    if (!e.target.closest('.start-menu') && !e.target.closest('.tb-start-btn') && startMenuOpen) {
        toggleStartMenu();
    }
    if (!e.target.closest('.notif-panel') && !e.target.closest('#trayNotif') && notifPanelOpen) {
        toggleNotifPanel();
    }
});

function closeContextMenu() {
    document.getElementById('ctxMenu').classList.remove('show');
}

// ═══════════════════════════════════════════════════════════
//  REBOOT
// ═══════════════════════════════════════════════════════════
async function doReboot() {
    if (!confirm('Reboot device?')) return;
    await api('/api/system/reboot', {method: 'POST'});
    document.body.innerHTML = '<div style="display:flex;align-items:center;justify-content:center;height:100vh;color:#94a3b8;font-family:sans-serif;background:#0a0a1a"><div style="text-align:center"><h2 style="color:#e2e8f0;margin-bottom:8px">Rebooting...</h2><p>Please wait and refresh the page.</p></div></div>';
}
)rawliteral";

// ═══════════════════════════════════════════════════════════════════════════════
//  PROGMEM JavaScript — Part 2 (Apps)
// ═══════════════════════════════════════════════════════════════════════════════
const char HTML_JS2[] PROGMEM = R"rawliteral(
// ═══════════════════════════════════════════════════════════
//  APP LOADER
// ═══════════════════════════════════════════════════════════
function loadApp(id) {
    const body = document.getElementById('wb-' + id);
    if (!body) return;
    body.innerHTML = '<div class="loading">Loading...</div>';

    switch(id) {
        case 'gpio': loadGPIO(body); break;
        case 'terminal': loadTerminal(body); break;
        case 'files': loadFiles(body); break;
        case 'monitor': loadMonitor(body); break;
        case 'wifi': loadWiFi(body); break;
        case 'settings': loadSettings(body); break;
        case 'editor': loadEditor(body); break;
        case 'calculator': loadCalculator(body); break;
        case 'calendar': loadCalendar(body); break;
        case 'scheduler': loadScheduler(body); break;
    }
}

// ═══════════════════════════════════════════════════════════
//  APP: GPIO CONTROL
// ═══════════════════════════════════════════════════════════
async function loadGPIO(body) {
    const data = await apiJson('/api/gpio/all');
    if (!data) { body.innerHTML = '<div class="empty-state">Failed to load GPIO</div>'; return; }

    let html = '<div class="app-toolbar">';
    html += '<button class="app-btn primary" onclick="gpioAllOn()">All ON</button>';
    html += '<button class="app-btn" onclick="gpioAllOff()">All OFF</button>';
    html += '<button class="app-btn" onclick="loadApp(\'gpio\')">Refresh</button>';
    html += '</div>';
    html += '<div class="gpio-grid">';

    data.forEach(g => {
        const modeClass = g.mode.toLowerCase();
        html += `<div class="gpio-card" id="gc-${g.pin}">`;
        html += `<div class="gpio-top"><span class="gpio-name">${g.name}</span><span class="gpio-badge ${modeClass}">${g.mode}</span></div>`;
        html += `<div class="gpio-label">${g.label || ''}</div>`;
        html += '<div class="gpio-controls">';

        if (g.mode === 'OUTPUT') {
            html += `<label class="toggle-sw"><input type="checkbox" ${g.state ? 'checked' : ''} onchange="gpioSet(${g.pin}, this.checked)"><div class="toggle-track"></div><div class="toggle-knob"></div></label>`;
            html += `<span style="font-size:12px;color:${g.state ? 'var(--success)' : 'var(--text-muted)'};font-weight:600">${g.state ? 'ON' : 'OFF'}</span>`;
        } else if (g.mode === 'INPUT') {
            html += `<span class="gpio-state ${g.state ? 'high' : 'low'}">${g.state ? 'HIGH' : 'LOW'}</span>`;
        } else if (g.mode === 'PWM') {
            html += `<input type="range" class="pwm-range" min="0" max="1023" value="${g.pwmValue || 0}" oninput="this.nextElementSibling.textContent=this.value" onchange="gpioPwm(${g.pin}, this.value)">`;
            html += `<span style="font-size:12px;min-width:35px;text-align:right">${g.pwmValue || 0}</span>`;
        } else if (g.mode === 'SERVO') {
            html += `<input type="number" class="servo-input" min="0" max="180" value="${g.servoAngle || 0}" onchange="gpioServo(${g.pin}, this.value)">`;
            html += '<span style="font-size:12px;color:var(--text-muted)">deg</span>';
        }

        html += `<select class="gpio-mode-sel" onchange="gpioMode(${g.pin}, this.value)">`;
        ['OUTPUT','INPUT','PWM','SERVO'].forEach(m => {
            html += `<option value="${m}" ${g.mode === m ? 'selected' : ''}>${m}</option>`;
        });
        html += '</select>';
        html += '</div></div>';
    });

    html += '</div>';
    body.innerHTML = html;
}

async function gpioSet(pin, state) {
    await api(`/api/gpio/set?pin=${pin}&state=${state ? 1 : 0}`);
    setTimeout(() => loadApp('gpio'), 100);
}
async function gpioPwm(pin, val) { await api(`/api/gpio/pwm?pin=${pin}&value=${val}`); }
async function gpioServo(pin, angle) { await api(`/api/gpio/servo?pin=${pin}&angle=${angle}`); }
async function gpioMode(pin, mode) {
    await api(`/api/gpio/mode?pin=${pin}&mode=${mode}`);
    setTimeout(() => loadApp('gpio'), 100);
}
async function gpioAllOn() {
    const data = await apiJson('/api/gpio/all');
    if (!data) return;
    for (const g of data) {
        if (g.mode === 'OUTPUT') await api(`/api/gpio/set?pin=${g.pin}&state=1`);
    }
    setTimeout(() => loadApp('gpio'), 200);
}
async function gpioAllOff() {
    const data = await apiJson('/api/gpio/all');
    if (!data) return;
    for (const g of data) {
        if (g.mode === 'OUTPUT') await api(`/api/gpio/set?pin=${g.pin}&state=0`);
    }
    setTimeout(() => loadApp('gpio'), 200);
}

// ═══════════════════════════════════════════════════════════
//  APP: TERMINAL
// ═══════════════════════════════════════════════════════════
function loadTerminal(body) {
    body.innerHTML = `
        <div class="term-wrap">
            <div class="term-head">
                <div class="term-title">
                    <strong>WebOS-8266 Terminal</strong>
                    <span>Command line for system, files, GPIO, and network tasks</span>
                </div>
                <div class="badge" style="background:var(--info-bg);color:var(--info)">Ready</div>
            </div>
            <div class="term-shell">
                <div class="term-output" id="termOut"><div class="term-line ok">WebOS-8266 Terminal v1.0 Ready</div><div class="term-line info">Type 'help' for compact command groups</div></div>
                <div class="term-entry">
                    <div class="term-prompt">$</div>
                    <input type="text" class="term-input" id="termIn" placeholder="Enter command..." autocomplete="off">
                <button class="term-send" onclick="termExec()">Run</button>
                </div>
            </div>
        </div>
    `;
    const inp = document.getElementById('termIn');
    inp.addEventListener('keydown', e => {
        if (e.key === 'Enter') termExec();
        else if (e.key === 'ArrowUp') {
            e.preventDefault();
            if (cmdHistIdx < cmdHistory.length - 1) { cmdHistIdx++; inp.value = cmdHistory[cmdHistory.length - 1 - cmdHistIdx]; }
        } else if (e.key === 'ArrowDown') {
            e.preventDefault();
            if (cmdHistIdx > 0) { cmdHistIdx--; inp.value = cmdHistory[cmdHistory.length - 1 - cmdHistIdx]; }
            else { cmdHistIdx = -1; inp.value = ''; }
        }
    });
    inp.focus();
}

async function termExec() {
    const inp = document.getElementById('termIn');
    const out = document.getElementById('termOut');
    const cmd = inp.value.trim();
    if (!cmd) return;

    cmdHistory.push(cmd); cmdHistIdx = -1;
    addTermLine('$ ' + cmd, 'cmd');

    if (cmd === 'clear') { out.innerHTML = ''; inp.value = ''; return; }

    try {
        const r = await api('/api/command', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({command: cmd})
        });
        if (r) {
            const data = await r.json();
            if (data.output === 'CLEAR_TERMINAL') { out.innerHTML = ''; }
            else {
                const cls = data.type || 'ok';
                const lines = data.output.split('\\n');
                lines.forEach(l => { if (l) addTermLine(l, cls); });
            }
        }
    } catch(e) { addTermLine('Error: ' + e.message, 'err'); }
    inp.value = '';
}

function addTermLine(text, cls) {
    const out = document.getElementById('termOut');
    if (!out) return;
    const div = document.createElement('div');
    div.className = 'term-line ' + (cls || '');
    div.textContent = text;
    out.appendChild(div);
    out.scrollTop = out.scrollHeight;
}

// ═══════════════════════════════════════════════════════════
//  APP: FILE MANAGER
// ═══════════════════════════════════════════════════════════
async function loadFiles(body) {
    const data = await apiJson('/api/files');
    if (!data) { body.innerHTML = '<div class="empty-state">Failed to load files</div>'; return; }

    const totalBytes = data.reduce((sum, f) => sum + f.size, 0);
    const totalKB = (totalBytes / 1024).toFixed(1);

    let html = `
        <div class="fm-shell">
            <div class="fm-sidebar">
                <div class="fm-panel">
                    <div class="fm-files-header">
                        <div>
                            <h4>Storage</h4>
                            <span>LittleFS overview</span>
                        </div>
                        <span class="badge" style="background:var(--info-bg);color:var(--info)">${data.length} files</span>
                    </div>
                    <div class="fm-summary">
                        <div class="fm-stat">
                            <div class="fm-stat-val">${totalKB}</div>
                            <div class="fm-stat-lbl">KB used</div>
                        </div>
                        <div class="fm-stat">
                            <div class="fm-stat-val">${Math.max(0, 100 - Math.round(totalBytes / 20480 * 100))}%</div>
                            <div class="fm-stat-lbl">Free space</div>
                        </div>
                    </div>
                </div>
                <div class="fm-upload-zone compact" id="fmUploadZone" onclick="document.getElementById('fmUploadInput').click()">
                    <p>${icon('upload')}</p>
                    <div class="zone-title">Upload files</div>
                    <div class="zone-sub">Drop here or click to browse</div>
                    <input type="file" id="fmUploadInput" onchange="uploadFile(this.files[0])">
                </div>
                <div class="fm-panel fm-actions">
                    <button class="app-btn" onclick="loadApp('files')">${icon('refresh')} Refresh</button>
                    <button class="app-btn primary" onclick="fmNewFile()">+ New File</button>
                </div>
            </div>
            <div class="fm-main">
                <div class="fm-panel">
                    <div class="fm-files-header">
                        <div>
                            <h4>Files</h4>
                            <span>Manage LittleFS content</span>
                        </div>
                    </div>
                    <div class="file-list">
    `;

    if (data.length === 0) {
        html += '<div class="empty-state">No files in filesystem</div>';
    } else {
        data.forEach(f => {
            const ext = f.name.split('.').pop().toLowerCase();
            const iconName = ({'txt':'doc','json':'doc','html':'monitor2','css':'gear','js':'gear','log':'doc','csv':'chart'})[ext] || 'doc';
            html += `
                <div class="file-item">
                    <div class="file-icon">${icon(iconName)}</div>
                    <div class="file-details">
                        <div class="file-name">${f.name}</div>
                        <div class="file-size">${(f.size / 1024).toFixed(2)} KB</div>
                    </div>
                    <div class="file-actions">
                        <button onclick="viewFile('${f.name}')">View</button>
                        <button onclick="downloadFile('${f.name}')">${icon('download')}</button>
                        <button onclick="renameFile('${f.name}')">Rename</button>
                        <button onclick="deleteFile('${f.name}')" style="color:var(--error)">${icon('close')}</button>
                    </div>
                </div>
            `;
        });
    }

    html += '</div></div></div></div>';
    body.innerHTML = html;

    // Drag & drop
    const zone = document.getElementById('fmUploadZone');
    zone.addEventListener('dragover', e => { e.preventDefault(); zone.classList.add('drag-over'); });
    zone.addEventListener('dragleave', () => zone.classList.remove('drag-over'));
    zone.addEventListener('drop', e => {
        e.preventDefault(); zone.classList.remove('drag-over');
        if (e.dataTransfer.files.length) uploadFile(e.dataTransfer.files[0]);
    });
}

async function uploadFile(file) {
    if (!file) return;
    const form = new FormData();
    form.append('file', file, file.name);
    await api('/upload', {method: 'POST', body: form});
    setTimeout(() => loadApp('files'), 300);
}

async function deleteFile(name) {
    if (!confirm('Delete ' + name + '?')) return;
    await api('/delete/' + name, {method: 'DELETE'});
    setTimeout(() => loadApp('files'), 200);
}

function downloadFile(name) {
    const a = document.createElement('a');
    a.href = '/download/' + name; a.download = name;
    document.body.appendChild(a); a.click(); a.remove();
}

async function viewFile(name) {
    const r = await api('/api/files/read?name=' + encodeURIComponent(name));
    if (!r) return;
    const text = await r.text();
    // Open in editor
    openApp('editor');
    setTimeout(() => {
        const ta = document.getElementById('editorTA');
        const fn = document.getElementById('editorFN');
        if (ta) ta.value = text;
        if (fn) fn.textContent = name;
        if (ta) updateEditorStats();
    }, 300);
}

async function renameFile(oldName) {
    const newName = prompt('New name for ' + oldName + ':', oldName);
    if (!newName || newName === oldName) return;
    await api('/api/files/rename', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({oldName, newName})
    });
    setTimeout(() => loadApp('files'), 200);
}

function fmNewFile() {
    const name = prompt('New file name:');
    if (!name) return;
    openApp('editor');
    setTimeout(() => {
        const ta = document.getElementById('editorTA');
        const fn = document.getElementById('editorFN');
        if (ta) ta.value = '';
        if (fn) fn.textContent = name;
    }, 300);
}

// ═══════════════════════════════════════════════════════════
//  APP: SYSTEM MONITOR
// ═══════════════════════════════════════════════════════════
async function loadMonitor(body) {
    body.innerHTML = `
        <div class="app-toolbar">
            <button class="app-btn primary" onclick="loadMonitor(document.getElementById('wb-monitor'))">${icon('refresh')} Refresh</button>
        </div>
        <div class="stats-grid" id="monStats"></div>
        <div class="chart-area">
            <h4>Memory History</h4>
            <canvas id="heapChart" height="80"></canvas>
        </div>
        <div class="chart-area">
            <h4>System Details</h4>
            <div id="monDetails"></div>
        </div>
    `;
    updateMonitor();
    if (monitorInterval) clearInterval(monitorInterval);
    monitorInterval = setInterval(updateMonitor, 3000);
}

async function updateMonitor() {
    const info = await apiJson('/api/system/info');
    if (!info) return;

    const statsEl = document.getElementById('monStats');
    const detailsEl = document.getElementById('monDetails');
    if (!statsEl) return;

    const heapKB = (info.freeHeap / 1024).toFixed(1);
    const usedPct = Math.round((1 - info.freeHeap / 81920) * 100);

    statsEl.innerHTML = `
        <div class="stat-card"><div class="stat-val">${heapKB} KB</div><div class="stat-lbl">Free Memory</div></div>
        <div class="stat-card"><div class="stat-val" style="color:${usedPct > 80 ? 'var(--error)' : 'var(--accent)'}">${usedPct}%</div><div class="stat-lbl">Memory Used</div></div>
        <div class="stat-card"><div class="stat-val">${info.wifiRSSI} dBm</div><div class="stat-lbl">WiFi Signal</div></div>
        <div class="stat-card"><div class="stat-val">${info.uptime || '...'}</div><div class="stat-lbl">Uptime</div></div>
        <div class="stat-card"><div class="stat-val">${info.ipAddress}</div><div class="stat-lbl">IP Address</div></div>
        <div class="stat-card"><div class="stat-val">v${info.version}</div><div class="stat-lbl">Version</div></div>
    `;

    if (detailsEl) {
        detailsEl.innerHTML = `
            <div class="setting-row"><span class="setting-label">Chip</span><span class="setting-value">${info.chip || 'ESP8266'}</span></div>
            <div class="setting-row"><span class="setting-label">Flash Size</span><span class="setting-value">${info.flashSize || '?'} KB</span></div>
            <div class="setting-row"><span class="setting-label">SDK Version</span><span class="setting-value">${info.sdk || '?'}</span></div>
            <div class="setting-row"><span class="setting-label">Core Version</span><span class="setting-value">${info.core || '?'}</span></div>
            <div class="setting-row"><span class="setting-label">CPU Frequency</span><span class="setting-value">${info.cpuFreq || '?'} MHz</span></div>
            <div class="setting-row"><span class="setting-label">WiFi Mode</span><span class="setting-value">${info.wifiMode || '?'}</span></div>
            <div class="setting-row"><span class="setting-label">Connected SSID</span><span class="setting-value">${info.ssid || 'N/A'}</span></div>
            <div class="setting-row"><span class="setting-label">MAC Address</span><span class="setting-value">${info.mac || '?'}</span></div>
            <div class="setting-row"><span class="setting-label">NTP Synced</span><span class="setting-value">${info.time || 'No'}</span></div>
        `;
    }

    // Update heap chart
    heapHistory.push(info.freeHeap);
    if (heapHistory.length > 30) heapHistory.shift();
    drawSparkline('heapChart', heapHistory, 81920);
}

function drawSparkline(canvasId, data, maxVal) {
    const canvas = document.getElementById(canvasId);
    if (!canvas || data.length < 2) return;
    canvas.width = canvas.offsetWidth * 2;
    canvas.height = 160;
    const ctx = canvas.getContext('2d');
    const w = canvas.width, h = canvas.height;
    const pad = 10;

    ctx.clearRect(0, 0, w, h);

    // Grid lines
    ctx.strokeStyle = 'rgba(255,255,255,0.04)';
    ctx.lineWidth = 1;
    for (let i = 0; i <= 4; i++) {
        const y = pad + (h - pad * 2) * (i / 4);
        ctx.beginPath(); ctx.moveTo(pad, y); ctx.lineTo(w - pad, y); ctx.stroke();
    }

    // Data line
    const gradient = ctx.createLinearGradient(0, 0, 0, h);
    gradient.addColorStop(0, 'rgba(99, 102, 241, 0.8)');
    gradient.addColorStop(1, 'rgba(139, 92, 246, 0.3)');

    ctx.beginPath();
    ctx.strokeStyle = '#6366f1';
    ctx.lineWidth = 3;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';

    data.forEach((v, i) => {
        const x = pad + ((w - pad * 2) / (data.length - 1)) * i;
        const y = pad + (h - pad * 2) * (1 - v / maxVal);
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();

    // Fill area under line
    const lastX = pad + ((w - pad * 2) / (data.length - 1)) * (data.length - 1);
    const firstX = pad;
    ctx.lineTo(lastX, h - pad);
    ctx.lineTo(firstX, h - pad);
    ctx.closePath();
    ctx.fillStyle = gradient;
    ctx.globalAlpha = 0.15;
    ctx.fill();
    ctx.globalAlpha = 1;
}

// ═══════════════════════════════════════════════════════════
//  APP: WiFi MANAGER
// ═══════════════════════════════════════════════════════════
async function loadWiFi(body) {
    body.innerHTML = '<div class="loading">Loading WiFi status...</div>';
    const status = await apiJson('/api/wifi/status');
    if (!status) { body.innerHTML = '<div class="empty-state">Failed to load WiFi</div>'; return; }

    let html = `
        <div class="wifi-status-card ${status.connected ? 'wifi-connected' : 'wifi-disconnected'}">
            <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px">
                <span style="font-size:14px;font-weight:600">${status.connected ? 'Connected' : 'Disconnected'}</span>
                <span class="badge" style="background:${status.connected ? 'var(--success-bg)' : 'var(--error-bg)'};color:${status.connected ? 'var(--success)' : 'var(--error)'}">
                    ${status.mode}
                </span>
            </div>
            <div class="wifi-info-row"><span class="wifi-info-label">SSID</span><span class="wifi-info-val">${status.ssid || 'N/A'}</span></div>
            <div class="wifi-info-row"><span class="wifi-info-label">IP Address</span><span class="wifi-info-val">${status.ip}</span></div>
            <div class="wifi-info-row"><span class="wifi-info-label">Subnet</span><span class="wifi-info-val">${status.subnet}</span></div>
            <div class="wifi-info-row"><span class="wifi-info-label">Gateway</span><span class="wifi-info-val">${status.gateway}</span></div>
            <div class="wifi-info-row"><span class="wifi-info-label">DNS</span><span class="wifi-info-val">${status.dns}</span></div>
            <div class="wifi-info-row"><span class="wifi-info-label">MAC</span><span class="wifi-info-val">${status.mac}</span></div>
            <div class="wifi-info-row"><span class="wifi-info-label">Signal</span><span class="wifi-info-val">${status.rssi} dBm</span></div>
        </div>
        <div class="app-toolbar">
            <button class="app-btn primary" onclick="wifiScan()">${icon('wifi')} Scan Networks</button>
            <button class="app-btn" onclick="loadApp('wifi')">${icon('refresh')} Refresh</button>
        </div>
        <div id="wifiNets"></div>
    `;
    body.innerHTML = html;
}

async function wifiScan() {
    const el = document.getElementById('wifiNets');
    if (!el) return;
    el.innerHTML = '<div class="loading">Scanning...</div>';
    const data = await apiJson('/api/wifi/scan');
    if (!data || !data.networks) { el.innerHTML = '<div class="empty-state">Scan failed</div>'; return; }

    let html = '';
    data.networks.forEach(net => {
        const rssi = net.rssi;
        const bars = [rssi > -80, rssi > -70, rssi > -60, rssi > -50];
        const barsHtml = bars.map((a, i) => `<div class="wifi-signal-bar ${a ? 'active' : ''}" style="height:${6 + i * 4}px"></div>`).join('');

        html += `
            <div class="wifi-network-item" onclick="wifiShowConnect('${net.ssid}')">
                <div class="wifi-signal">${barsHtml}</div>
                <div style="flex:1">
                    <div style="font-size:13px;font-weight:500">${net.ssid}</div>
                    <div style="font-size:11px;color:var(--text-muted)">${net.encryption} | Ch ${net.channel} | ${rssi} dBm</div>
                </div>
            </div>
        `;
    });

    if (data.networks.length === 0) html = '<div class="empty-state">No networks found</div>';

    html += `
        <div class="wifi-connect-form" id="wifiConnectForm" style="display:none">
            <h4 style="font-size:13px;margin-bottom:10px">Connect to Network</h4>
            <input type="text" id="wifiSSID" placeholder="SSID" readonly>
            <input type="password" id="wifiPW" placeholder="Password">
            <button class="app-btn primary" onclick="wifiConnect()" style="width:100%">Connect</button>
        </div>
    `;
    el.innerHTML = html;
}

function wifiShowConnect(ssid) {
    const form = document.getElementById('wifiConnectForm');
    if (!form) return;
    form.style.display = 'block';
    document.getElementById('wifiSSID').value = ssid;
    document.getElementById('wifiPW').value = '';
    document.getElementById('wifiPW').focus();
}

async function wifiConnect() {
    const ssid = document.getElementById('wifiSSID').value;
    const pass = document.getElementById('wifiPW').value;
    await api('/api/wifi/connect', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ssid, password: pass})
    });
    alert('Connecting to ' + ssid + '... Device may change IP. Please reconnect.');
}

// ═══════════════════════════════════════════════════════════
//  APP: SETTINGS
// ═══════════════════════════════════════════════════════════
async function loadSettings(body) {
    const info = await apiJson('/api/system/info');
    const accent = localStorage.getItem('mwos_accent') || '#6366f1';

    let html = `
        <div class="settings-section">
            <h3>Device</h3>
            <div class="setting-row"><span class="setting-label">Hostname</span><span class="setting-value">${info ? info.hostname || 'webos-8266' : '--'}</span></div>
            <div class="setting-row"><span class="setting-label">Version</span><span class="setting-value">v${info ? info.version : '--'}</span></div>
            <div class="setting-row"><span class="setting-label">Chip ID</span><span class="setting-value">${info ? info.chipId || '?' : '--'}</span></div>
        </div>
        <div class="settings-section">
            <h3>Appearance</h3>
            <div class="setting-row">
                <span class="setting-label">Accent Color</span>
                <input type="color" class="color-picker" value="${accent}" onchange="setAccentColor(this.value)">
            </div>
            <div class="setting-row">
                <span class="setting-label">Reset Theme</span>
                <button class="app-btn" onclick="resetTheme()">Reset</button>
            </div>
        </div>
        <div class="settings-section">
            <h3>Security</h3>
            <div class="setting-row"><span class="setting-label">Auth Status</span><span class="setting-value">${info && info.authEnabled ? 'Enabled' : 'Disabled'}</span></div>
            <div class="setting-row"><span class="setting-label">Change Password</span>
                <div style="display:flex;gap:6px">
                    <input type="password" class="setting-input" id="newPassInput" placeholder="New password" style="width:120px">
                    <button class="app-btn" onclick="changePassword()">Update</button>
                </div>
            </div>
        </div>
        <div class="settings-section">
            <h3>Network</h3>
            <div class="setting-row"><span class="setting-label">AP SSID</span><span class="setting-value">${info ? info.apSSID || 'WebOS-8266' : '--'}</span></div>
            <div class="setting-row"><span class="setting-label">AP Password</span><span class="setting-value">••••••••</span></div>
        </div>
        <div class="settings-section">
            <h3>About</h3>
            <div class="setting-row"><span class="setting-label">Flash Size</span><span class="setting-value">${info ? info.flashSize + ' KB' : '--'}</span></div>
            <div class="setting-row"><span class="setting-label">SDK</span><span class="setting-value">${info ? info.sdk : '--'}</span></div>
            <div class="setting-row"><span class="setting-label">Core</span><span class="setting-value">${info ? info.core : '--'}</span></div>
            <div class="setting-row"><span class="setting-label">CPU Freq</span><span class="setting-value">${info ? info.cpuFreq + ' MHz' : '--'}</span></div>
            <div class="setting-row">
                <span class="setting-label">System Logs</span>
                <button class="app-btn" onclick="showLogs()">View Logs</button>
            </div>
        </div>
    `;
    body.innerHTML = html;
}

function setAccentColor(color) {
    document.documentElement.style.setProperty('--accent', color);
    const hsl = hexToHSL(color);
    const lighter = `hsl(${hsl.h}, ${hsl.s}%, ${Math.min(hsl.l + 15, 90)}%)`;
    const glow = color + '40';
    document.documentElement.style.setProperty('--accent-hover', lighter);
    document.documentElement.style.setProperty('--accent-glow', glow);
    localStorage.setItem('mwos_accent', color);
}

function resetTheme() {
    localStorage.removeItem('mwos_accent');
    document.documentElement.style.removeProperty('--accent');
    document.documentElement.style.removeProperty('--accent-hover');
    document.documentElement.style.removeProperty('--accent-glow');
}

function hexToHSL(hex) {
    let r = parseInt(hex.slice(1, 3), 16) / 255;
    let g = parseInt(hex.slice(3, 5), 16) / 255;
    let b = parseInt(hex.slice(5, 7), 16) / 255;
    const max = Math.max(r, g, b), min = Math.min(r, g, b);
    let h, s, l = (max + min) / 2;
    if (max === min) { h = s = 0; }
    else {
        const d = max - min;
        s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
        if (max === r) h = ((g - b) / d + (g < b ? 6 : 0)) / 6;
        else if (max === g) h = ((b - r) / d + 2) / 6;
        else h = ((r - g) / d + 4) / 6;
    }
    return {h: Math.round(h * 360), s: Math.round(s * 100), l: Math.round(l * 100)};
}

async function changePassword() {
    const pw = document.getElementById('newPassInput').value;
    if (!pw || pw.length < 4) { alert('Password must be at least 4 characters'); return; }
    const r = await api('/api/settings/set', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({password: pw})
    });
    if (r && r.ok) alert('Password updated!');
    else alert('Failed to update password');
}

async function showLogs() {
    const data = await apiJson('/api/system/logs');
    if (!data || !data.logs) return;
    openApp('terminal');
    setTimeout(() => {
        const out = document.getElementById('termOut');
        if (!out) return;
        addTermLine('=== System Logs ===', 'info');
        data.logs.forEach(l => addTermLine(`[${l.level}] ${l.message}`, l.level === 'error' ? 'err' : l.level === 'warn' ? 'warn' : 'info'));
    }, 300);
}

// ═══════════════════════════════════════════════════════════
//  APP: TEXT EDITOR
// ═══════════════════════════════════════════════════════════
function loadEditor(body) {
    body.innerHTML = `
        <div class="editor-shell">
            <div class="editor-side">
                <div class="editor-panel">
                    <div class="editor-header">
                        <div>
                            <div class="editor-title">Document</div>
                            <div class="editor-subtitle" id="editorFN">untitled.txt</div>
                        </div>
                    </div>
                    <div class="editor-meta-grid">
                        <div class="mini-stat"><div class="value" id="editorChars">0</div><div class="label">Characters</div></div>
                        <div class="mini-stat"><div class="value" id="editorWords">0</div><div class="label">Words</div></div>
                        <div class="mini-stat"><div class="value" id="editorLines">1</div><div class="label">Lines</div></div>
                        <div class="mini-stat"><div class="value">TXT</div><div class="label">Format</div></div>
                    </div>
                </div>
                <div class="editor-panel">
                    <div class="editor-title" style="margin-bottom:8px">Actions</div>
                    <div class="app-toolbar" style="margin-bottom:0">
                        <button class="app-btn primary" onclick="editorSave()">${icon('doc')} Save</button>
                        <button class="app-btn" onclick="editorNew()">New</button>
                        <button class="app-btn" onclick="editorOpen()">Open</button>
                    </div>
                </div>
            </div>
            <div class="editor-main">
                <div class="editor-panel">
                    <div class="editor-toolbar" style="margin-bottom:12px">
                        <span class="editor-filename" id="editorFNChip">untitled.txt</span>
                        <span class="badge" style="background:var(--success-bg);color:var(--success)">Live</span>
                    </div>
                    <textarea class="editor-textarea" id="editorTA" placeholder="Start typing..." oninput="updateEditorStats()" spellcheck="false"></textarea>
                    <div class="editor-status">
                        <span id="editorChars2">0 characters</span>
                        <span id="editorLines2">1 line</span>
                        <span id="editorWords2">0 words</span>
                    </div>
                </div>
            </div>
        </div>
    `;
}

function updateEditorStats() {
    const ta = document.getElementById('editorTA');
    if (!ta) return;
    const text = ta.value;
    const charCount = text.length;
    const lineCount = text.split('\n').length;
    const wordCount = (text.trim() ? text.trim().split(/\s+/).length : 0);
    document.getElementById('editorChars').textContent = charCount;
    document.getElementById('editorLines').textContent = lineCount;
    document.getElementById('editorWords').textContent = wordCount;
    const chars2 = document.getElementById('editorChars2');
    const lines2 = document.getElementById('editorLines2');
    const words2 = document.getElementById('editorWords2');
    if (chars2) chars2.textContent = charCount + ' characters';
    if (lines2) lines2.textContent = lineCount + ' lines';
    if (words2) words2.textContent = wordCount + ' words';
    const chip = document.getElementById('editorFNChip');
    if (chip) chip.textContent = document.getElementById('editorFN').textContent;
}

async function editorSave() {
    const fn = document.getElementById('editorFN').textContent;
    const content = document.getElementById('editorTA').value;
    const r = await api('/api/files/write', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({name: fn, content})
    });
    if (r && r.ok) {
        addTermLine && addTermLine('File saved: ' + fn, 'ok');
        alert('Saved: ' + fn);
    } else { alert('Failed to save file'); }
}

function editorNew() {
    const fn = prompt('File name:', 'untitled.txt');
    if (!fn) return;
    document.getElementById('editorFN').textContent = fn;
    document.getElementById('editorTA').value = '';
    updateEditorStats();
}

async function editorOpen() {
    const fn = prompt('File name to open:');
    if (!fn) return;
    const r = await api('/api/files/read?name=' + encodeURIComponent(fn));
    if (r && r.ok) {
        const text = await r.text();
        document.getElementById('editorFN').textContent = fn;
        document.getElementById('editorTA').value = text;
        updateEditorStats();
    } else { alert('File not found: ' + fn); }
}

// ═══════════════════════════════════════════════════════════
//  APP: CALCULATOR
// ═══════════════════════════════════════════════════════════
function loadCalculator(body) {
    body.innerHTML = `
        <div class="calc-shell">
            <div class="calc-display">
                <div class="calc-expression" id="calcExpr"></div>
                <div class="calc-result" id="calcResult">0</div>
            </div>
            <div class="app-toolbar">
                <button class="app-btn" onclick="calcClear()">Clear</button>
                <button class="app-btn" onclick="calcBackspace()">Backspace</button>
            </div>
            <div class="calc-grid" id="calcGrid">
                <button class="calc-btn fn" data-key="(">(</button>
                <button class="calc-btn fn" data-key=")">)</button>
                <button class="calc-btn fn" data-key="%">%</button>
                <button class="calc-btn op" data-key="/">÷</button>

                <button class="calc-btn" data-key="7">7</button>
                <button class="calc-btn" data-key="8">8</button>
                <button class="calc-btn" data-key="9">9</button>
                <button class="calc-btn op" data-key="*">×</button>

                <button class="calc-btn" data-key="4">4</button>
                <button class="calc-btn" data-key="5">5</button>
                <button class="calc-btn" data-key="6">6</button>
                <button class="calc-btn op" data-key="-">−</button>

                <button class="calc-btn" data-key="1">1</button>
                <button class="calc-btn" data-key="2">2</button>
                <button class="calc-btn" data-key="3">3</button>
                <button class="calc-btn op" data-key="+">+</button>

                <button class="calc-btn fn" data-key="0" style="grid-column: span 2;">0</button>
                <button class="calc-btn fn" data-key=".">.</button>
                <button class="calc-btn eq" onclick="calcEquals()">=</button>
            </div>
        </div>
    `;

    const grid = document.getElementById('calcGrid');
    grid.addEventListener('click', e => {
        const btn = e.target.closest('button[data-key]');
        if (!btn) return;
        calcInput(btn.dataset.key);
    });

    updateCalcDisplay();
}

let calcExpression = '';
let calcDisplayValue = '0';

function calcInput(value) {
    calcExpression += value;
    calcDisplayValue = calcExpression;
    updateCalcDisplay();
}

function calcClear() {
    calcExpression = '';
    calcDisplayValue = '0';
    updateCalcDisplay();
}

function calcBackspace() {
    calcExpression = calcExpression.slice(0, -1);
    calcDisplayValue = calcExpression || '0';
    updateCalcDisplay();
}

function calcEquals() {
    try {
        const safe = calcExpression.replace(/[^0-9+\-*/().%]/g, '');
        if (!safe) return;
        const result = Function('return (' + safe + ')')();
        calcDisplayValue = String(Number.isFinite(result) ? result : 'Error');
        calcExpression = calcDisplayValue === 'Error' ? '' : calcDisplayValue;
    } catch (e) {
        calcDisplayValue = 'Error';
        calcExpression = '';
    }
    updateCalcDisplay();
}

function updateCalcDisplay() {
    const expr = document.getElementById('calcExpr');
    const result = document.getElementById('calcResult');
    if (expr) expr.textContent = calcExpression;
    if (result) result.textContent = calcDisplayValue;
}

// ═══════════════════════════════════════════════════════════
//  APP: CALENDAR
// ═══════════════════════════════════════════════════════════
let calendarCursor = new Date();

async function loadCalendar(body) {
    const now = new Date();
    if (!calendarCursor) calendarCursor = new Date(now.getFullYear(), now.getMonth(), 1);

    const monthLabel = calendarCursor.toLocaleString(undefined, { month: 'long', year: 'numeric' });

    body.innerHTML = `
        <div class="cal-shell">
            <div class="cal-panel">
                <div class="cal-header">
                    <div>
                        <div class="cal-title" id="calMonthLabel">${monthLabel}</div>
                        <div style="font-size:12px;color:var(--text-muted)">Month view</div>
                    </div>
                    <div class="cal-nav">
                        <button class="app-btn" onclick="calPrevMonth()">Prev</button>
                        <button class="app-btn" onclick="calToday()">Today</button>
                        <button class="app-btn" onclick="calNextMonth()">Next</button>
                    </div>
                </div>
                <div class="cal-grid" id="calGrid"></div>
            </div>
        </div>
    `;

    renderCalendarGrid();
}

function calPrevMonth() {
    calendarCursor = new Date(calendarCursor.getFullYear(), calendarCursor.getMonth() - 1, 1);
    loadApp('calendar');
}

function calNextMonth() {
    calendarCursor = new Date(calendarCursor.getFullYear(), calendarCursor.getMonth() + 1, 1);
    loadApp('calendar');
}

function calToday() {
    calendarCursor = new Date();
    loadApp('calendar');
}

function renderCalendarGrid() {
    const grid = document.getElementById('calGrid');
    const label = document.getElementById('calMonthLabel');
    if (!grid || !label) return;

    label.textContent = calendarCursor.toLocaleString(undefined, { month: 'long', year: 'numeric' });
    const year = calendarCursor.getFullYear();
    const month = calendarCursor.getMonth();
    const firstDay = new Date(year, month, 1);
    const startDay = (firstDay.getDay() + 6) % 7;
    const daysInMonth = new Date(year, month + 1, 0).getDate();
    const prevDays = new Date(year, month, 0).getDate();
    const today = new Date();

    const week = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
    let html = week.map(d => `<div class="cal-dow">${d}</div>`).join('');

    for (let i = startDay - 1; i >= 0; i--) {
        const day = prevDays - i;
        html += `<div class="cal-day muted"><div class="cal-day-num">${day}</div></div>`;
    }

    for (let day = 1; day <= daysInMonth; day++) {
        const isToday = today.getFullYear() === year && today.getMonth() === month && today.getDate() === day;
        html += `<div class="cal-day ${isToday ? 'today' : ''}"><div class="cal-day-num">${day}</div></div>`;
    }

    const totalCells = startDay + daysInMonth;
    const remaining = (7 - (totalCells % 7)) % 7;
    for (let day = 1; day <= remaining; day++) {
        html += `<div class="cal-day muted"><div class="cal-day-num">${day}</div></div>`;
    }

    grid.innerHTML = html;
}

// ═══════════════════════════════════════════════════════════
//  APP: TASK SCHEDULER
// ═══════════════════════════════════════════════════════════
async function loadScheduler(body) {
    const data = await apiJson('/api/scheduler/list');

    let html = `
        <div class="sched-board">
            <div class="sched-panel sched-card-panel">
                <div class="sched-header">
                    <div>
                        <div class="sched-title">Task Scheduler</div>
                        <div class="sched-subtitle">Create and manage automation tasks</div>
                    </div>
                    <button class="app-btn primary" onclick="schedScrollToAdd()">+ New Task</button>
                </div>
                <div class="sched-summary">
                    <div class="mini-stat"><div class="value">${data && data.tasks ? data.tasks.length : 0}</div><div class="label">Tasks</div></div>
                    <div class="mini-stat"><div class="value">Live</div><div class="label">Status</div></div>
                </div>
                <div class="sched-add" id="schedAddCard">
                    <h4>Add Scheduled Task</h4>
                    <div class="sched-form-grid">
                        <label>Name</label>
                        <input type="text" id="schedName" placeholder="Task name" style="flex:1">
                        <label>Pin</label>
                        <select id="schedPin">
                            <option value="0">GPIO0</option><option value="2">GPIO2</option>
                            <option value="4">GPIO4</option><option value="5">GPIO5</option>
                            <option value="12">GPIO12</option><option value="13">GPIO13</option>
                            <option value="14">GPIO14</option><option value="15">GPIO15</option>
                            <option value="16">GPIO16</option>
                        </select>
                        <label>Action</label>
                        <select id="schedAction">
                            <option value="on">Turn ON</option>
                            <option value="off">Turn OFF</option>
                            <option value="toggle">Toggle</option>
                        </select>
                        <label>Every</label>
                        <div class="sched-interval-wrap">
                            <input type="number" id="schedInterval" value="30" min="1">
                            <span>seconds</span>
                        </div>
                    </div>
                    <button class="app-btn primary" onclick="schedAdd()" style="margin-top:8px">+ Add Task</button>
                </div>
            </div>
            <div class="sched-panel">
                <div class="sched-header">
                    <div>
                        <div class="sched-title">Active Tasks</div>
                        <div class="sched-subtitle">Current scheduled automations</div>
                    </div>
                </div>
                <div class="sched-list" id="schedList">
    `;

    if (data && data.tasks && data.tasks.length > 0) {
        data.tasks.forEach((t, i) => {
            html += `
                <div class="sched-card">
                    <div class="sched-info">
                        <div class="sched-name">${t.name}</div>
                        <div class="sched-detail">GPIO${t.pin} → ${t.action} every ${t.interval}s</div>
                    </div>
                    <div class="sched-card-actions">
                        <label class="toggle-sw">
                            <input type="checkbox" ${t.enabled ? 'checked' : ''} onchange="schedToggle(${i}, this.checked)">
                            <div class="toggle-track"></div><div class="toggle-knob"></div>
                        </label>
                        <button class="app-btn danger" onclick="schedDelete(${i})">${icon('close')}</button>
                    </div>
                </div>
            `;
        });
    } else {
        html += '<div class="empty-state">No scheduled tasks</div>';
    }

    html += '</div></div></div>';
    body.innerHTML = html;
}

function schedScrollToAdd() {
    const card = document.getElementById('schedAddCard');
    if (card) card.scrollIntoView({ behavior: 'smooth', block: 'start' });
}

async function schedAdd() {
    const name = document.getElementById('schedName').value || 'Task';
    const pin = document.getElementById('schedPin').value;
    const action = document.getElementById('schedAction').value;
    const interval = document.getElementById('schedInterval').value;

    await api('/api/scheduler/add', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({name, pin: parseInt(pin), action, interval: parseInt(interval)})
    });
    setTimeout(() => loadApp('scheduler'), 200);
}

async function schedDelete(idx) {
    await api('/api/scheduler/delete', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({index: idx})
    });
    setTimeout(() => loadApp('scheduler'), 200);
}

async function schedToggle(idx, enabled) {
    await api('/api/scheduler/toggle', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({index: idx, enabled})
    });
}

// ═══════════════════════════════════════════════════════════
//  INIT + THEME RESTORATION
// ═══════════════════════════════════════════════════════════
let bootStarted = false;

function startBootOnce() {
    if (bootStarted) return;
    bootStarted = true;

    const saved = localStorage.getItem('mwos_accent');
    if (saved) setAccentColor(saved);

    bootSequence().catch(err => {
        console.error('Boot sequence failed:', err);
        const status = document.getElementById('bootStatus');
        if (status) status.textContent = 'Startup issue detected, opening interface...';

        setTimeout(() => {
            const boot = document.getElementById('bootScreen');
            if (boot) {
                boot.classList.add('hidden');
                setTimeout(() => { boot.style.display = 'none'; }, 250);
            }
            if (authToken) showDesktop();
            else showLogin();
        }, 250);
    });
}

if (document.readyState === 'interactive' || document.readyState === 'complete') {
    startBootOnce();
}
document.addEventListener('DOMContentLoaded', startBootOnce);
window.addEventListener('load', startBootOnce);
setTimeout(startBootOnce, 800);

// Keyboard shortcut: Escape to close focused window
document.addEventListener('keydown', e => {
    if (e.key === 'Escape') {
        if (startMenuOpen) toggleStartMenu();
        else if (notifPanelOpen) toggleNotifPanel();
    }
});
)rawliteral";

// ═══════════════════════════════════════════════════════════════════════════════
//  PROGMEM HTML — End
// ═══════════════════════════════════════════════════════════════════════════════
const char HTML_END[] PROGMEM = R"rawliteral(
</script>
</body>
</html>
)rawliteral";

// ═══════════════════════════════════════════════════════════════════════════════
//  HTTP ROOT HANDLER — Serves the desktop page
// ═══════════════════════════════════════════════════════════════════════════════
String getContentType(const String& path) {
    if (path.endsWith(".htm") || path.endsWith(".html")) return F("text/html");
    if (path.endsWith(".css")) return F("text/css");
    if (path.endsWith(".js")) return F("application/javascript");
    if (path.endsWith(".json")) return F("application/json");
    if (path.endsWith(".png")) return F("image/png");
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return F("image/jpeg");
    if (path.endsWith(".gif")) return F("image/gif");
    if (path.endsWith(".svg")) return F("image/svg+xml");
    if (path.endsWith(".ico")) return F("image/x-icon");
    return F("text/plain");
}

void handleRoot() {
    const size_t lenStart = strlen_P(HTML_START);
    const size_t lenCss = strlen_P(HTML_CSS);
    const size_t lenBody = strlen_P(HTML_BODY);
    const size_t lenJs1 = strlen_P(HTML_JS1);
    const size_t lenJs2 = strlen_P(HTML_JS2);
    const size_t lenEnd = strlen_P(HTML_END);
    const size_t totalLen = lenStart + lenCss + lenBody + lenJs1 + lenJs2 + lenEnd;

    auto writeAllP = [](WiFiClient& c, PGM_P ptr, size_t len) -> bool {
        size_t off = 0;
        uint16_t stalls = 0;
        while (off < len) {
            size_t w = c.write_P(ptr + off, len - off);
            if (w == 0) {
                stalls++;
                if (!c.connected() || stalls > 250) return false;
                delay(1);
                continue;
            }
            off += w;
            stalls = 0;
            yield();
        }
        return true;
    };

    WiFiClient client = server.client();
    client.print(F("HTTP/1.1 200 OK\r\n"));
    client.print(F("Content-Type: text/html; charset=utf-8\r\n"));
    client.print(F("Cache-Control: no-store\r\n"));
    client.print(F("Connection: close\r\n"));
    client.print(F("Content-Length: "));
    client.print(totalLen);
    client.print(F("\r\n\r\n"));

    if (!writeAllP(client, reinterpret_cast<PGM_P>(HTML_START), lenStart)) return;
    if (!writeAllP(client, reinterpret_cast<PGM_P>(HTML_CSS), lenCss)) return;
    if (!writeAllP(client, reinterpret_cast<PGM_P>(HTML_BODY), lenBody)) return;
    if (!writeAllP(client, reinterpret_cast<PGM_P>(HTML_JS1), lenJs1)) return;
    if (!writeAllP(client, reinterpret_cast<PGM_P>(HTML_JS2), lenJs2)) return;
    if (!writeAllP(client, reinterpret_cast<PGM_P>(HTML_END), lenEnd)) return;
    client.flush();
    client.stop();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  AUTH HANDLERS
// ═══════════════════════════════════════════════════════════════════════════════
void handleLogin() {
    if (!server.hasArg("plain")) { sendUnauthorized(); return; }
    String body = server.arg("plain");
    String user = extractJsonField(body, "username");
    String pass = extractJsonField(body, "password");

    if (user == adminUser && pass == adminPass) {
        authToken = String(millis()) + String(ESP.getChipId(), HEX);
        String json = "{\"token\":\"" + authToken + "\"}";
        server.send(200, F("application/json"), json);
        addLog("User '" + user + "' logged in", "info");
        addNotification("Login successful", "success");
    } else {
        server.send(401, F("application/json"), F("{\"error\":\"Invalid credentials\"}"));
        addLog("Failed login attempt for '" + user + "'", "warn");
    }
}

void handleLogout() {
    authToken = "";
    server.send(200, F("application/json"), F("{\"success\":true}"));
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SYSTEM API HANDLERS
// ═══════════════════════════════════════════════════════════════════════════════
void handleSystemInfo() {
    String json = "{";
    json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"wifiRSSI\":" + String(WiFi.RSSI()) + ",";
    json += "\"ipAddress\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\",";
    json += "\"uptime\":\"" + uptimeString() + "\",";
    json += "\"uptimeMs\":" + String(millis()) + ",";
    json += "\"version\":\"" MICROWEBOS_VERSION "\",";
    json += "\"chip\":\"ESP8266\",";
    json += "\"chipId\":\"" + String(ESP.getChipId(), HEX) + "\",";
    json += "\"flashSize\":" + String(ESP.getFlashChipSize() / 1024) + ",";
    json += "\"sdk\":\"" + String(ESP.getSdkVersion()) + "\",";
    json += "\"core\":\"" + ESP.getCoreVersion() + "\",";
    json += "\"cpuFreq\":" + String(ESP.getCpuFreqMHz()) + ",";
    json += "\"hostname\":\"" HOSTNAME "\",";
    json += "\"apSSID\":\"" + String(ap_ssid) + "\",";
    json += "\"ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"mac\":\"" + WiFi.macAddress() + "\",";
    json += "\"wifiMode\":\"" + String(WiFi.status() == WL_CONNECTED ? "Station" : "AP") + "\",";
    json += "\"time\":\"" + getTimeString() + "\",";
    json += "\"date\":\"" + getDateString() + "\",";
    json += "\"authEnabled\":" + String(authEnabled ? "true" : "false");
    json += "}";
    server.send(200, F("application/json"), json);
}

void handleSystemLogs() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    String json = "{\"logs\":[";
    for (int i = 0; i < logCount; i++) {
        int idx = (logHead + i) % MAX_LOG_ENTRIES;
        if (i > 0) json += ",";
        json += "{\"message\":\"" + escapeJson(sysLog[idx].message) + "\",";
        json += "\"level\":\"" + sysLog[idx].level + "\",";
        json += "\"ts\":" + String(sysLog[idx].ts) + "}";
    }
    json += "]}";
    server.send(200, F("application/json"), json);
}

void handleReboot() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    server.send(200, F("application/json"), F("{\"success\":true}"));
    addLog("System reboot initiated", "warn");
    delay(200);
    ESP.restart();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  GPIO API HANDLERS
// ═══════════════════════════════════════════════════════════════════════════════
void handleAllGPIO() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    String json = "[";
    for (int i = 0; i < numPins; i++) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"pin\":" + String(pins[i].pin) + ",";
        json += "\"name\":\"" + pins[i].name + "\",";
        json += "\"mode\":\"" + pins[i].mode + "\",";
        json += "\"label\":\"" + escapeJson(pins[i].label) + "\",";

        if (pins[i].mode == "INPUT") {
            pinMode(pins[i].pin, INPUT_PULLUP);
            json += "\"state\":" + String(digitalRead(pins[i].pin) == HIGH ? "true" : "false") + ",";
        } else {
            json += "\"state\":" + String(pins[i].state ? "true" : "false") + ",";
        }
        json += "\"pwmValue\":" + String(pins[i].pwmValue) + ",";
        json += "\"servoAngle\":" + String(pins[i].servoAngle);
        json += "}";
    }
    json += "]";
    server.send(200, F("application/json"), json);
}

void handleGPIOSet() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (server.hasArg("pin") && server.hasArg("state")) {
        int pin = server.arg("pin").toInt();
        bool state = server.arg("state").toInt();
        int idx = findPinIndex(pin);
        if (idx >= 0) {
            pins[idx].state = state;
            pins[idx].mode = "OUTPUT";
            pinMode(pin, OUTPUT);
            applyOutputState(pin, state);
            addLog("GPIO" + String(pin) + " set to " + (state ? "ON" : "OFF"), "info");
        }
        server.send(200, F("application/json"), F("{\"success\":true}"));
    } else {
        server.send(400, F("application/json"), F("{\"error\":\"Missing parameters\"}"));
    }
}

void handleGPIOToggle() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (server.hasArg("pin")) {
        int pin = server.arg("pin").toInt();
        int idx = findPinIndex(pin);
        if (idx >= 0 && pins[idx].mode == "OUTPUT") {
            pins[idx].state = !pins[idx].state;
            applyOutputState(pin, pins[idx].state);
        }
        server.send(200, F("application/json"), F("{\"success\":true}"));
    }
}

void handleGPIOPWM() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (server.hasArg("pin") && server.hasArg("value")) {
        int pin = server.arg("pin").toInt();
        int value = constrain(server.arg("value").toInt(), 0, 1023);
        int idx = findPinIndex(pin);
        if (idx >= 0) {
            pins[idx].mode = "PWM";
            pins[idx].pwmValue = value;
            pinMode(pin, OUTPUT);
            analogWrite(pin, value);
        }
        server.send(200, F("application/json"), F("{\"success\":true}"));
    }
}

void handleGPIOServo() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (server.hasArg("pin") && server.hasArg("angle")) {
        int pin = server.arg("pin").toInt();
        int angle = constrain(server.arg("angle").toInt(), 0, 180);
        int sIdx = findServoIndex(pin);

        if (sIdx >= 0) {
            if (!servosAttached[sIdx]) {
                servos[sIdx].attach(pin);
                servosAttached[sIdx] = true;
            }
            servos[sIdx].write(angle);
            int pIdx = findPinIndex(pin);
            if (pIdx >= 0) {
                pins[pIdx].mode = "SERVO";
                pins[pIdx].servoAngle = angle;
            }
        }
        server.send(200, F("application/json"), F("{\"success\":true}"));
    }
}

void handleGPIOMode() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (server.hasArg("pin") && server.hasArg("mode")) {
        int pin = server.arg("pin").toInt();
        String mode = server.arg("mode");
        int idx = findPinIndex(pin);
        if (idx >= 0) {
            pins[idx].mode = mode;
            if (mode == "OUTPUT") {
                pinMode(pin, OUTPUT);
                applyOutputState(pin, pins[idx].state);
            } else if (mode == "INPUT") {
                pinMode(pin, INPUT_PULLUP);
            } else if (mode == "PWM") {
                pinMode(pin, OUTPUT);
                analogWrite(pin, pins[idx].pwmValue);
            }
            // SERVO is handled on first servo command
            addLog("GPIO" + String(pin) + " mode changed to " + mode, "info");
        }
        server.send(200, F("application/json"), F("{\"success\":true}"));
    }
}

void handleGPIORead() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (server.hasArg("pin")) {
        int pin = server.arg("pin").toInt();
        int idx = findPinIndex(pin);
        if (idx >= 0) {
            pinMode(pin, INPUT_PULLUP);
            bool val = digitalRead(pin);
            server.send(200, F("application/json"),
                "{\"pin\":" + String(pin) + ",\"value\":" + String(val ? "true" : "false") + "}");
            return;
        }
        server.send(404, F("application/json"), F("{\"error\":\"Pin not found\"}"));
    }
}

void handleAnalogRead() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    int val = analogRead(A0);
    server.send(200, F("application/json"),
        "{\"pin\":\"A0\",\"value\":" + String(val) + "}");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  FILE API HANDLERS
// ═══════════════════════════════════════════════════════════════════════════════
void handleFiles() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    Dir dir = LittleFS.openDir("/");
    String json = "[";
    bool first = true;
    while (dir.next()) {
        if (!first) json += ",";
        first = false;
        json += "{\"name\":\"" + escapeJson(dir.fileName()) + "\",\"size\":" + String(dir.fileSize()) + "}";
    }
    json += "]";
    server.send(200, F("application/json"), json);
}

void handleUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        String filename = "/" + upload.filename;
        if (LittleFS.exists(filename)) LittleFS.remove(filename);
        uploadFile = LittleFS.open(filename, "w");
        addLog("File upload started: " + upload.filename, "info");
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) {
            uploadFile.close();
            addLog("File uploaded: " + upload.filename + " (" + String(upload.totalSize) + " bytes)", "info");
            addNotification("File uploaded: " + upload.filename, "success");
        }
    }
}

void handleUploadComplete() {
    server.send(200, F("application/json"), F("{\"success\":true}"));
}

void handleDelete() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    String uri = server.uri();
    String filename = "/" + uri.substring(8);  // Remove "/delete/"
    if (LittleFS.exists(filename)) {
        LittleFS.remove(filename);
        addLog("File deleted: " + filename, "info");
        server.send(200, F("application/json"), F("{\"success\":true}"));
    } else {
        server.send(404, F("application/json"), F("{\"error\":\"File not found\"}"));
    }
}

void handleDownload() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    String uri = server.uri();
    String path = "/" + uri.substring(10);  // Remove "/download/"
    if (LittleFS.exists(path)) {
        File file = LittleFS.open(path, "r");
        server.streamFile(file, F("application/octet-stream"));
        file.close();
    } else {
        server.send(404, F("text/plain"), F("File not found"));
    }
}

void handleFileRead() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (server.hasArg("name")) {
        String path = "/" + server.arg("name");
        if (LittleFS.exists(path)) {
            File f = LittleFS.open(path, "r");
            String content = f.readString();
            f.close();
            server.send(200, F("text/plain"), content);
        } else {
            server.send(404, F("text/plain"), F("File not found"));
        }
    }
}

void handleFileWrite() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (!server.hasArg("plain")) {
        server.send(400, F("application/json"), F("{\"error\":\"Missing body\"}"));
        return;
    }
    String body = server.arg("plain");
    String name = extractJsonField(body, "name");
    String content = extractJsonField(body, "content");

    if (name.length() == 0) {
        server.send(400, F("application/json"), F("{\"error\":\"Missing filename\"}"));
        return;
    }

    String path = "/" + name;
    File f = LittleFS.open(path, "w");
    if (f) {
        f.print(content);
        f.close();
        addLog("File written: " + name, "info");
        server.send(200, F("application/json"), F("{\"success\":true}"));
    } else {
        server.send(500, F("application/json"), F("{\"error\":\"Failed to write\"}"));
    }
}

void handleFileRename() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (!server.hasArg("plain")) {
        server.send(400, F("application/json"), F("{\"error\":\"Missing body\"}"));
        return;
    }
    String body = server.arg("plain");
    String oldName = "/" + extractJsonField(body, "oldName");
    String newName = "/" + extractJsonField(body, "newName");

    if (LittleFS.exists(oldName)) {
        LittleFS.rename(oldName, newName);
        addLog("File renamed: " + oldName + " -> " + newName, "info");
        server.send(200, F("application/json"), F("{\"success\":true}"));
    } else {
        server.send(404, F("application/json"), F("{\"error\":\"File not found\"}"));
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  WiFi API HANDLERS
// ═══════════════════════════════════════════════════════════════════════════════
void handleWiFiStatus() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    String json = "{";
    json += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    json += "\"mode\":\"" + String(WiFi.status() == WL_CONNECTED ? "Station" : "AP") + "\",";
    json += "\"ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\",";
    json += "\"subnet\":\"" + WiFi.subnetMask().toString() + "\",";
    json += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
    json += "\"dns\":\"" + WiFi.dnsIP().toString() + "\",";
    json += "\"mac\":\"" + WiFi.macAddress() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
    json += "}";
    server.send(200, F("application/json"), json);
}

void handleWiFiScan() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    int n = WiFi.scanNetworks();
    String json = "{\"networks\":[";
    for (int i = 0; i < n && i < 20; i++) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"ssid\":\"" + escapeJson(WiFi.SSID(i)) + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        json += "\"channel\":" + String(WiFi.channel(i)) + ",";
        String enc;
        switch (WiFi.encryptionType(i)) {
            case ENC_TYPE_WEP:  enc = "WEP"; break;
            case ENC_TYPE_TKIP: enc = "WPA"; break;
            case ENC_TYPE_CCMP: enc = "WPA2"; break;
            case ENC_TYPE_AUTO: enc = "Auto"; break;
            case ENC_TYPE_NONE: enc = "Open"; break;
            default: enc = "Unknown"; break;
        }
        json += "\"encryption\":\"" + enc + "\"";
        json += "}";
    }
    json += "]}";
    WiFi.scanDelete();
    server.send(200, F("application/json"), json);
}

void handleWiFiConnect() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (!server.hasArg("plain")) {
        server.send(400, F("application/json"), F("{\"error\":\"Missing body\"}"));
        return;
    }
    String body = server.arg("plain");
    String newSSID = extractJsonField(body, "ssid");
    String newPass = extractJsonField(body, "password");

    addLog("Attempting WiFi connection to " + newSSID, "info");
    server.send(200, F("application/json"), F("{\"success\":true,\"message\":\"Connecting...\"}"));

    delay(100);
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(newSSID.c_str(), newPass.c_str());
}

void handleWiFiDisconnect() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    WiFi.disconnect();
    addLog("WiFi disconnected by user", "info");
    server.send(200, F("application/json"), F("{\"success\":true}"));
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SCHEDULER API HANDLERS
// ═══════════════════════════════════════════════════════════════════════════════
void handleSchedulerList() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    String json = "{\"tasks\":[";
    bool first = true;
    for (int i = 0; i < MAX_SCHED_TASKS; i++) {
        if (!schedTasks[i].active) continue;
        if (!first) json += ",";
        first = false;
        json += "{";
        json += "\"name\":\"" + escapeJson(schedTasks[i].name) + "\",";
        json += "\"pin\":" + String(schedTasks[i].pin) + ",";
        json += "\"action\":\"" + schedTasks[i].action + "\",";
        json += "\"interval\":" + String(schedTasks[i].intervalMs / 1000) + ",";
        json += "\"enabled\":" + String(schedTasks[i].enabled ? "true" : "false");
        json += "}";
    }
    json += "]}";
    server.send(200, F("application/json"), json);
}

void handleSchedulerAdd() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (!server.hasArg("plain")) {
        server.send(400, F("application/json"), F("{\"error\":\"Missing body\"}"));
        return;
    }
    String body = server.arg("plain");
    String name = extractJsonField(body, "name");
    int pin = extractJsonField(body, "pin").toInt();
    String action = extractJsonField(body, "action");
    int interval = extractJsonField(body, "interval").toInt();

    // Find empty slot
    for (int i = 0; i < MAX_SCHED_TASKS; i++) {
        if (!schedTasks[i].active) {
            schedTasks[i].active = true;
            schedTasks[i].enabled = true;
            schedTasks[i].name = name;
            schedTasks[i].pin = pin;
            schedTasks[i].action = action;
            schedTasks[i].intervalMs = (unsigned long)interval * 1000;
            schedTasks[i].lastRun = millis();
            addLog("Scheduled task added: " + name, "info");
            addNotification("Task created: " + name, "info");
            server.send(200, F("application/json"), F("{\"success\":true}"));
            return;
        }
    }
    server.send(400, F("application/json"), F("{\"error\":\"Max tasks reached\"}"));
}

void handleSchedulerDelete() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (!server.hasArg("plain")) {
        server.send(400, F("application/json"), F("{\"error\":\"Missing body\"}"));
        return;
    }
    String body = server.arg("plain");
    int index = extractJsonField(body, "index").toInt();

    // Map visible index to actual slot
    int count = 0;
    for (int i = 0; i < MAX_SCHED_TASKS; i++) {
        if (schedTasks[i].active) {
            if (count == index) {
                addLog("Scheduled task deleted: " + schedTasks[i].name, "info");
                schedTasks[i].active = false;
                schedTasks[i].name = "";
                server.send(200, F("application/json"), F("{\"success\":true}"));
                return;
            }
            count++;
        }
    }
    server.send(404, F("application/json"), F("{\"error\":\"Task not found\"}"));
}

void handleSchedulerToggle() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (!server.hasArg("plain")) {
        server.send(400, F("application/json"), F("{\"error\":\"Missing body\"}"));
        return;
    }
    String body = server.arg("plain");
    int index = extractJsonField(body, "index").toInt();
    bool enabled = extractJsonField(body, "enabled") == "true";

    int count = 0;
    for (int i = 0; i < MAX_SCHED_TASKS; i++) {
        if (schedTasks[i].active) {
            if (count == index) {
                schedTasks[i].enabled = enabled;
                server.send(200, F("application/json"), F("{\"success\":true}"));
                return;
            }
            count++;
        }
    }
    server.send(404, F("application/json"), F("{\"error\":\"Task not found\"}"));
}

// ═══════════════════════════════════════════════════════════════════════════════
//  NOTIFICATION API HANDLERS
// ═══════════════════════════════════════════════════════════════════════════════
void handleNotifications() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    String json = "{\"notifications\":[";
    for (int i = notifCount - 1; i >= 0; i--) {
        int idx = (notifHead + i) % MAX_NOTIFICATIONS;
        if (i < notifCount - 1) json += ",";
        json += "{";
        json += "\"message\":\"" + escapeJson(notifications[idx].message) + "\",";
        json += "\"type\":\"" + notifications[idx].type + "\",";
        json += "\"ts\":" + String(notifications[idx].ts) + ",";
        json += "\"read\":" + String(notifications[idx].read ? "true" : "false");
        json += "}";
    }
    json += "]}";
    server.send(200, F("application/json"), json);
}

void handleNotificationsClear() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    notifCount = 0;
    notifHead = 0;
    server.send(200, F("application/json"), F("{\"success\":true}"));
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SETTINGS API HANDLER
// ═══════════════════════════════════════════════════════════════════════════════
void handleSettingsSet() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (!server.hasArg("plain")) {
        server.send(400, F("application/json"), F("{\"error\":\"Missing body\"}"));
        return;
    }
    String body = server.arg("plain");
    String newPass = extractJsonField(body, "password");
    if (newPass.length() >= 4) {
        adminPass = newPass;
        addLog("Admin password changed", "warn");
        addNotification("Password changed successfully", "warning");
    }
    server.send(200, F("application/json"), F("{\"success\":true}"));
}

// ═══════════════════════════════════════════════════════════════════════════════
//  TERMINAL COMMAND HANDLER
// ═══════════════════════════════════════════════════════════════════════════════
void handleCommand() {
    if (!checkAuth()) { sendUnauthorized(); return; }
    if (!server.hasArg("plain")) {
        server.send(400, F("application/json"), F("{\"output\":\"Missing command body\"}"));
        return;
    }

    String body = server.arg("plain");
    String command = extractJsonField(body, "command");
    command.trim();

    String output = "";
    String type = "ok";

    if (command == "help") {
        output = "=== WebOS-8266 Terminal ===\\n";
        output += "Core: help, clear, status, info, version, uptime\\n";
        output += "GPIO: pins, gpio P S, pwm P V, servo P A, led on/off\\n";
        output += "Files: ls, cat F, write F C, touch F, rm F, df\\n";
        output += "Network: wifi, ip, mac, rssi, scan\\n";
        output += "System: notify M, schedule, reboot, reset\\n";
        output += "Examples: gpio 2 on | pwm 5 512 | ls / | cat config.txt\\n";
        output += "Tip: use Arrow Up/Down for history";
        type = "info";
    }
    else if (command == "status") {
        output = "WebOS-8266 v" MICROWEBOS_VERSION "\\n";
        output += "Heap: " + String(ESP.getFreeHeap() / 1024) + "KB free\\n";
        output += "WiFi: " + String(WiFi.RSSI()) + "dBm | " + WiFi.SSID() + "\\n";
        output += "IP: " + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\\n";
        output += "Uptime: " + uptimeString() + "\\n";
        output += "Time: " + getTimeString();
    }
    else if (command == "info") {
        output = "=== Device Info ===\\n";
        output += "Chip: ESP8266 (ID: " + String(ESP.getChipId(), HEX) + ")\\n";
        output += "Flash: " + String(ESP.getFlashChipSize() / 1024) + " KB\\n";
        output += "SDK: " + String(ESP.getSdkVersion()) + "\\n";
        output += "Core: " + ESP.getCoreVersion() + "\\n";
        output += "CPU: " + String(ESP.getCpuFreqMHz()) + " MHz\\n";
        output += "Free heap: " + String(ESP.getFreeHeap()) + " bytes";
    }
    else if (command == "neofetch") {
        output = "\\n";
        output += "  __  ____      ______  ___  \\n";
        output += " |  \\\\/  \\\\ \\\\    / / __ \\\\/ __| \\n";
        output += " | |\\\\/| |\\\\ \\\\/\\\\/ / |  | \\\\__  \\n";
        output += " |_|  |_| \\\\_/\\\\_/|_|  |_|___/ \\n";
        output += "\\n";
        output += " OS:     WebOS-8266 v" MICROWEBOS_VERSION "\\n";
        output += " Host:   " HOSTNAME "\\n";
        output += " Chip:   ESP8266\\n";
        output += " CPU:    " + String(ESP.getCpuFreqMHz()) + " MHz\\n";
        output += " Memory: " + String(ESP.getFreeHeap() / 1024) + " KB free\\n";
        output += " Flash:  " + String(ESP.getFlashChipSize() / 1024) + " KB\\n";
        output += " WiFi:   " + WiFi.SSID() + " (" + String(WiFi.RSSI()) + " dBm)\\n";
        output += " IP:     " + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\\n";
        output += " Uptime: " + uptimeString();
        type = "info";
    }
    else if (command == "version") {
        output = "WebOS-8266 v" MICROWEBOS_VERSION;
    }
    else if (command == "uptime") {
        output = "Uptime: " + uptimeString();
    }
    else if (command == "hostname") {
        output = "Hostname: " HOSTNAME ".local";
    }
    else if (command == "reboot") {
        output = "Rebooting device...";
        type = "warn";
        String json = "{\"output\":\"" + output + "\",\"type\":\"" + type + "\"}";
        server.send(200, F("application/json"), json);
        delay(200);
        ESP.restart();
        return;
    }
    else if (command == "reset") {
        output = "Factory resetting... formatting LittleFS...";
        type = "warn";
        String json = "{\"output\":\"" + output + "\",\"type\":\"" + type + "\"}";
        server.send(200, F("application/json"), json);
        LittleFS.format();
        delay(200);
        ESP.restart();
        return;
    }
    else if (command == "heap") {
        output = "Free heap: " + String(ESP.getFreeHeap()) + " bytes (" + String(ESP.getFreeHeap() / 1024) + " KB)\\n";
        output += "Max free block: " + String(ESP.getMaxFreeBlockSize()) + " bytes\\n";
        output += "Heap fragmentation: " + String(ESP.getHeapFragmentation()) + "%";
    }
    else if (command == "free") {
        output = "          Total    Used     Free\\n";
        unsigned long total = 81920;
        unsigned long free_h = ESP.getFreeHeap();
        unsigned long used = total - free_h;
        output += "Heap:     " + String(total) + "  " + String(used) + "  " + String(free_h) + " bytes";
    }
    else if (command == "wifi") {
        output = "=== WiFi Info ===\\n";
        output += "Status: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "\\n";
        output += "SSID: " + WiFi.SSID() + "\\n";
        output += "RSSI: " + String(WiFi.RSSI()) + " dBm\\n";
        output += "IP: " + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\\n";
        output += "MAC: " + WiFi.macAddress() + "\\n";
        output += "Gateway: " + WiFi.gatewayIP().toString();
    }
    else if (command == "ip") {
        output = "IP: " + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
    }
    else if (command == "mac") {
        output = "MAC: " + WiFi.macAddress();
    }
    else if (command == "rssi") {
        int rssi = WiFi.RSSI();
        output = "RSSI: " + String(rssi) + " dBm";
        if (rssi > -50) output += " (Excellent)";
        else if (rssi > -60) output += " (Good)";
        else if (rssi > -70) output += " (Fair)";
        else output += " (Weak)";
    }
    else if (command == "scan") {
        int n = WiFi.scanNetworks();
        output = "Found " + String(n) + " networks:\\n";
        for (int i = 0; i < n && i < 15; i++) {
            output += "  " + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + " dBm, Ch " + String(WiFi.channel(i)) + ")\\n";
        }
        WiFi.scanDelete();
    }
    else if (command == "pins") {
        output = "=== GPIO Pin Status ===\\n";
        for (int i = 0; i < numPins; i++) {
            output += "  " + pins[i].name + " [" + pins[i].mode + "]";
            if (pins[i].mode == "OUTPUT") output += " = " + String(pins[i].state ? "ON" : "OFF");
            else if (pins[i].mode == "PWM") output += " = " + String(pins[i].pwmValue);
            else if (pins[i].mode == "SERVO") output += " = " + String(pins[i].servoAngle) + "deg";
            else if (pins[i].mode == "INPUT") {
                pinMode(pins[i].pin, INPUT_PULLUP);
                output += " = " + String(digitalRead(pins[i].pin) ? "HIGH" : "LOW");
            }
            output += " (" + pins[i].label + ")\\n";
        }
    }
    else if (command.startsWith("gpio ")) {
        int sp1 = command.indexOf(' ');
        int sp2 = command.indexOf(' ', sp1 + 1);
        if (sp2 > 0) {
            int pin = command.substring(sp1 + 1, sp2).toInt();
            String action = command.substring(sp2 + 1);
            action.trim();
            int idx = findPinIndex(pin);
            if (idx >= 0) {
                if (action == "on") {
                    pins[idx].state = true; pins[idx].mode = "OUTPUT";
                    pinMode(pin, OUTPUT); applyOutputState(pin, true);
                    output = "GPIO" + String(pin) + " turned ON";
                } else if (action == "off") {
                    pins[idx].state = false; pins[idx].mode = "OUTPUT";
                    pinMode(pin, OUTPUT); applyOutputState(pin, false);
                    output = "GPIO" + String(pin) + " turned OFF";
                } else if (action == "toggle") {
                    pins[idx].state = !pins[idx].state; pins[idx].mode = "OUTPUT";
                    applyOutputState(pin, pins[idx].state);
                    output = "GPIO" + String(pin) + " toggled to " + (pins[idx].state ? "ON" : "OFF");
                } else {
                    output = "Unknown action. Use: on, off, toggle";
                    type = "err";
                }
            } else {
                output = "Invalid pin: " + String(pin);
                type = "err";
            }
        } else {
            output = "Usage: gpio <pin> <on/off/toggle>";
            type = "warn";
        }
    }
    else if (command.startsWith("pwm ")) {
        int sp1 = command.indexOf(' ');
        int sp2 = command.indexOf(' ', sp1 + 1);
        if (sp2 > 0) {
            int pin = command.substring(sp1 + 1, sp2).toInt();
            int val = constrain(command.substring(sp2 + 1).toInt(), 0, 1023);
            int idx = findPinIndex(pin);
            if (idx >= 0) {
                pins[idx].mode = "PWM"; pins[idx].pwmValue = val;
                pinMode(pin, OUTPUT); analogWrite(pin, val);
                output = "GPIO" + String(pin) + " PWM set to " + String(val);
            } else { output = "Invalid pin"; type = "err"; }
        } else { output = "Usage: pwm <pin> <0-1023>"; type = "warn"; }
    }
    else if (command.startsWith("servo ")) {
        int sp1 = command.indexOf(' ');
        int sp2 = command.indexOf(' ', sp1 + 1);
        if (sp2 > 0) {
            int pin = command.substring(sp1 + 1, sp2).toInt();
            int angle = constrain(command.substring(sp2 + 1).toInt(), 0, 180);
            int sIdx = findServoIndex(pin);
            if (sIdx >= 0) {
                if (!servosAttached[sIdx]) { servos[sIdx].attach(pin); servosAttached[sIdx] = true; }
                servos[sIdx].write(angle);
                int pIdx = findPinIndex(pin);
                if (pIdx >= 0) { pins[pIdx].mode = "SERVO"; pins[pIdx].servoAngle = angle; }
                output = "GPIO" + String(pin) + " servo set to " + String(angle) + " degrees";
            } else { output = "Pin not configured for servo"; type = "err"; }
        } else { output = "Usage: servo <pin> <0-180>"; type = "warn"; }
    }
    else if (command == "led on") {
        int idx = findPinIndex(LED_BUILTIN_PIN);
        if (idx >= 0) { pins[idx].state = true; applyOutputState(LED_BUILTIN_PIN, true); }
        output = "Built-in LED turned ON";
    }
    else if (command == "led off") {
        int idx = findPinIndex(LED_BUILTIN_PIN);
        if (idx >= 0) { pins[idx].state = false; applyOutputState(LED_BUILTIN_PIN, false); }
        output = "Built-in LED turned OFF";
    }
    else if (command == "aread") {
        int val = analogRead(A0);
        output = "Analog pin A0: " + String(val) + " (voltage: " + String(val * 3.3 / 1023, 2) + "V)";
    }
    else if (command == "ls" || command == "files") {
        Dir dir = LittleFS.openDir("/");
        output = "=== Files in LittleFS ===\\n";
        int count = 0;
        unsigned long totalSize = 0;
        while (dir.next()) {
            output += "  " + dir.fileName() + "  (" + String(dir.fileSize()) + " bytes)\\n";
            totalSize += dir.fileSize();
            count++;
        }
        if (count == 0) output += "  (empty)";
        else output += "\\n" + String(count) + " file(s), " + String(totalSize) + " bytes total";
    }
    else if (command.startsWith("cat ")) {
        String fname = "/" + command.substring(4);
        fname.trim();
        if (LittleFS.exists(fname)) {
            File f = LittleFS.open(fname, "r");
            output = f.readString();
            f.close();
        } else { output = "File not found: " + fname; type = "err"; }
    }
    else if (command.startsWith("touch ")) {
        String fname = "/" + command.substring(6);
        fname.trim();
        File f = LittleFS.open(fname, "w");
        if (f) { f.close(); output = "Created: " + fname; }
        else { output = "Failed to create: " + fname; type = "err"; }
    }
    else if (command.startsWith("rm ")) {
        String fname = "/" + command.substring(3);
        fname.trim();
        if (LittleFS.exists(fname)) {
            LittleFS.remove(fname);
            output = "Deleted: " + fname;
        } else { output = "File not found: " + fname; type = "err"; }
    }
    else if (command.startsWith("write ")) {
        int sp1 = command.indexOf(' ');
        int sp2 = command.indexOf(' ', sp1 + 1);
        if (sp2 > 0) {
            String fname = "/" + command.substring(sp1 + 1, sp2);
            String content = command.substring(sp2 + 1);
            File f = LittleFS.open(fname, "w");
            if (f) { f.print(content); f.close(); output = "Written to " + fname; }
            else { output = "Failed to write"; type = "err"; }
        } else { output = "Usage: write <filename> <content>"; type = "warn"; }
    }
    else if (command == "df") {
        FSInfo fs_info;
        LittleFS.info(fs_info);
        output = "=== Disk Usage ===\\n";
        output += "Total:  " + String(fs_info.totalBytes) + " bytes (" + String(fs_info.totalBytes / 1024) + " KB)\\n";
        output += "Used:   " + String(fs_info.usedBytes) + " bytes (" + String(fs_info.usedBytes / 1024) + " KB)\\n";
        output += "Free:   " + String(fs_info.totalBytes - fs_info.usedBytes) + " bytes\\n";
        output += "Usage:  " + String((float)fs_info.usedBytes / fs_info.totalBytes * 100, 1) + "%";
    }
    else if (command == "clear") {
        output = "CLEAR_TERMINAL";
    }
    else if (command.startsWith("echo ")) {
        output = command.substring(5);
    }
    else if (command == "date") {
        output = getDateString() + " " + getTimeString();
    }
    else if (command == "whoami") {
        output = "admin@" HOSTNAME;
    }
    else if (command.startsWith("notify ")) {
        String msg = command.substring(7);
        addNotification(msg, "info");
        output = "Notification sent: " + msg;
    }
    else if (command == "schedule") {
        output = "=== Scheduled Tasks ===\\n";
        int count = 0;
        for (int i = 0; i < MAX_SCHED_TASKS; i++) {
            if (schedTasks[i].active) {
                output += "  [" + String(schedTasks[i].enabled ? "ON" : "OFF") + "] ";
                output += schedTasks[i].name + " | GPIO" + String(schedTasks[i].pin);
                output += " | " + schedTasks[i].action + " every " + String(schedTasks[i].intervalMs / 1000) + "s\\n";
                count++;
            }
        }
        if (count == 0) output += "  (no tasks)";
    }
    else if (command == "ps") {
        output = "=== Running Processes ===\\n";
        output += "  PID  NAME              STATUS\\n";
        output += "  1    system            running\\n";
        output += "  2    webserver         running\\n";
        output += "  3    wifi              running\\n";
        output += "  4    scheduler         running\\n";
        output += "  5    pin_watcher       running\\n";
        output += "  6    mdns              running\\n";
        output += "  7    ota_server        running\\n";
        output += "  8    ntp_client        " + String(ntpSynced ? "synced" : "waiting");
    }
    else if (command == "") {
        output = "";
    }
    else {
        output = "Unknown command: '" + command + "'. Type 'help' for available commands.";
        type = "err";
    }

    // Build JSON response
    String escapedOutput = escapeJson(output);
    String json = "{\"output\":\"" + escapedOutput + "\",\"type\":\"" + type + "\"}";
    server.send(200, F("application/json"), json);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SCHEDULER ENGINE
// ═══════════════════════════════════════════════════════════════════════════════
void runScheduler() {
    unsigned long now = millis();
    if (now - lastSchedulerTick < 1000) return;
    lastSchedulerTick = now;

    for (int i = 0; i < MAX_SCHED_TASKS; i++) {
        if (!schedTasks[i].active || !schedTasks[i].enabled) continue;
        if (now - schedTasks[i].lastRun >= schedTasks[i].intervalMs) {
            schedTasks[i].lastRun = now;
            int pin = schedTasks[i].pin;
            int idx = findPinIndex(pin);
            if (idx < 0) continue;

            if (schedTasks[i].action == "on") {
                pins[idx].state = true;
                pins[idx].mode = "OUTPUT";
                pinMode(pin, OUTPUT);
                applyOutputState(pin, true);
            } else if (schedTasks[i].action == "off") {
                pins[idx].state = false;
                pins[idx].mode = "OUTPUT";
                pinMode(pin, OUTPUT);
                applyOutputState(pin, false);
            } else if (schedTasks[i].action == "toggle") {
                pins[idx].state = !pins[idx].state;
                pins[idx].mode = "OUTPUT";
                applyOutputState(pin, pins[idx].state);
            }

            addLog("Scheduler: " + schedTasks[i].name + " executed", "info");
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PIN WATCHER ENGINE
// ═══════════════════════════════════════════════════════════════════════════════
void runPinWatchers() {
    unsigned long now = millis();
    if (now - lastWatcherTick < 1000) return;
    lastWatcherTick = now;

    for (int i = 0; i < MAX_PIN_WATCHERS; i++) {
        if (!pinWatchers[i].active) continue;
        int pin = pinWatchers[i].pin;
        int idx = findPinIndex(pin);
        if (idx < 0) continue;

        pinMode(pin, INPUT_PULLUP);
        bool state = digitalRead(pin);

        bool trigger = false;
        if (pinWatchers[i].condition == "high" && state == HIGH) trigger = true;
        if (pinWatchers[i].condition == "low" && state == LOW) trigger = true;

        if (trigger && !pinWatchers[i].triggered) {
            pinWatchers[i].triggered = true;
            addNotification("Pin Alert: GPIO" + String(pin) + " is " + pinWatchers[i].condition, "warning");
            addLog("PinWatch: GPIO" + String(pin) + " triggered (" + pinWatchers[i].condition + ")", "warn");
        } else if (!trigger) {
            pinWatchers[i].triggered = false;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  WiFi SETUP
// ═══════════════════════════════════════════════════════════════════════════════
bool setupWiFi() {
    Serial.print(F("[BOOT] Connecting to WiFi: "));
    Serial.println(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.hostname(HOSTNAME);
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(F("."));
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("\n[BOOT] WiFi connected!"));
        Serial.print(F("[BOOT] IP Address: "));
        Serial.println(WiFi.localIP());
        wifiConnected = true;
        addLog("WiFi connected to " + String(ssid), "info");
        addNotification("WiFi connected: " + WiFi.localIP().toString(), "success");
        return true;
    }

    Serial.println(F("\n[BOOT] WiFi failed, starting AP mode"));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);
    Serial.print(F("[BOOT] AP IP: "));
    Serial.println(WiFi.softAPIP());
    addLog("Started AP mode: " + String(ap_ssid), "warn");
    addNotification("AP Mode active: " + String(ap_ssid), "warning");
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PIN INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════════
void initPins() {
    for (int i = 0; i < numPins; i++) {
        if (pins[i].mode == "OUTPUT") {
            pinMode(pins[i].pin, OUTPUT);
            applyOutputState(pins[i].pin, pins[i].state);
        } else if (pins[i].mode == "INPUT") {
            pinMode(pins[i].pin, INPUT_PULLUP);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SCHEDULER/WATCHER INIT
// ═══════════════════════════════════════════════════════════════════════════════
void initScheduler() {
    for (int i = 0; i < MAX_SCHED_TASKS; i++) {
        schedTasks[i].active = false;
        schedTasks[i].enabled = false;
        schedTasks[i].pin = 0;
        schedTasks[i].action = "";
        schedTasks[i].value = 0;
        schedTasks[i].intervalMs = 0;
        schedTasks[i].lastRun = 0;
        schedTasks[i].name = "";
    }
}

void initPinWatchers() {
    for (int i = 0; i < MAX_PIN_WATCHERS; i++) {
        pinWatchers[i].active = false;
        pinWatchers[i].pin = 0;
        pinWatchers[i].condition = "";
        pinWatchers[i].threshold = 0;
        pinWatchers[i].triggered = false;
        pinWatchers[i].name = "";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════════════════
void setup() {
    delay(1000);
    Serial.begin(115200);
    Serial.println(F("\n"));
    Serial.println(F("╔══════════════════════════════════════════════╗"));
    Serial.println(F("║   WebOS-8266 v" MICROWEBOS_VERSION "                            ║"));
    Serial.println(F("║   Desktop OS for ESP8266                    ║"));
    Serial.println(F("║   github.com/webos-8266                     ║"));
    Serial.println(F("╚══════════════════════════════════════════════╝"));
    Serial.println();

    bootTime = millis();

    // Initialize hardware
    Serial.println(F("[BOOT] Initializing GPIO pins..."));
    initPins();
    addLog("GPIO pins initialized", "info");

    // Initialize scheduler and watchers
    Serial.println(F("[BOOT] Initializing scheduler..."));
    initScheduler();
    initPinWatchers();

    // Initialize LittleFS
    Serial.println(F("[BOOT] Mounting LittleFS..."));
    if (!LittleFS.begin()) {
        Serial.println(F("[BOOT] Formatting LittleFS..."));
        LittleFS.format();
        if (!LittleFS.begin()) {
            Serial.println(F("[BOOT] LittleFS mount failed!"));
            addLog("LittleFS mount failed", "error");
        }
    }
    Serial.println(F("[BOOT] LittleFS ready"));
    addLog("LittleFS mounted", "info");

    // Setup WiFi
    setupWiFi();

    // Setup NTP
    Serial.println(F("[BOOT] Configuring NTP..."));
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
    addLog("NTP configured", "info");

    // Setup mDNS
    if (MDNS.begin(HOSTNAME)) {
        Serial.printf("[BOOT] mDNS: %s.local\n", HOSTNAME);
        addLog("mDNS started: " + String(HOSTNAME) + ".local", "info");
    }

    // Setup OTA
    Serial.println(F("[BOOT] Configuring OTA updates..."));
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.onStart([]() {
        addLog("OTA update started", "warn");
        Serial.println(F("[OTA] Update starting..."));
    });
    ArduinoOTA.onEnd([]() {
        Serial.println(F("\n[OTA] Update complete!"));
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]: ", error);
        addLog("OTA error: " + String(error), "error");
    });
    ArduinoOTA.begin();

    // Collect headers for auth token
    server.collectHeaders("X-Auth-Token");

    // ═══════ Register HTTP Routes ═══════
    Serial.println(F("[BOOT] Registering API endpoints..."));

    // Root
    server.on("/", HTTP_GET, handleRoot);

    // Auth
    server.on("/api/auth/login", HTTP_POST, handleLogin);
    server.on("/api/auth/logout", HTTP_POST, handleLogout);

    // System
    server.on("/api/system/info", HTTP_GET, handleSystemInfo);
    server.on("/api/system/logs", HTTP_GET, handleSystemLogs);
    server.on("/api/system/reboot", HTTP_POST, handleReboot);

    // GPIO
    server.on("/api/gpio/all", HTTP_GET, handleAllGPIO);
    server.on("/api/gpio/set", HTTP_GET, handleGPIOSet);
    server.on("/api/gpio/toggle", HTTP_GET, handleGPIOToggle);
    server.on("/api/gpio/pwm", HTTP_GET, handleGPIOPWM);
    server.on("/api/gpio/servo", HTTP_GET, handleGPIOServo);
    server.on("/api/gpio/mode", HTTP_GET, handleGPIOMode);
    server.on("/api/gpio/read", HTTP_GET, handleGPIORead);
    server.on("/api/gpio/analog", HTTP_GET, handleAnalogRead);

    // Files
    server.on("/api/files", HTTP_GET, handleFiles);
    server.on("/api/files/read", HTTP_GET, handleFileRead);
    server.on("/api/files/write", HTTP_POST, handleFileWrite);
    server.on("/api/files/rename", HTTP_POST, handleFileRename);
    server.on("/upload", HTTP_POST, handleUploadComplete, handleUpload);

    // WiFi
    server.on("/api/wifi/status", HTTP_GET, handleWiFiStatus);
    server.on("/api/wifi/scan", HTTP_GET, handleWiFiScan);
    server.on("/api/wifi/connect", HTTP_POST, handleWiFiConnect);
    server.on("/api/wifi/disconnect", HTTP_POST, handleWiFiDisconnect);

    // Scheduler
    server.on("/api/scheduler/list", HTTP_GET, handleSchedulerList);
    server.on("/api/scheduler/add", HTTP_POST, handleSchedulerAdd);
    server.on("/api/scheduler/delete", HTTP_POST, handleSchedulerDelete);
    server.on("/api/scheduler/toggle", HTTP_POST, handleSchedulerToggle);

    // Notifications
    server.on("/api/notifications", HTTP_GET, handleNotifications);
    server.on("/api/notifications/clear", HTTP_POST, handleNotificationsClear);

    // Settings
    server.on("/api/settings/set", HTTP_POST, handleSettingsSet);

    // Terminal
    server.on("/api/command", HTTP_POST, handleCommand);

    // Delete and Download — use onNotFound for path-based routing
    server.onNotFound([]() {
        String uri = server.uri();
        if (uri.startsWith("/delete/") && server.method() == HTTP_DELETE) {
            handleDelete();
            return;
        }
        if (uri.startsWith("/download/") && server.method() == HTTP_GET) {
            handleDownload();
            return;
        }
        if (LittleFS.exists(uri)) {
            File file = LittleFS.open(uri, "r");
            server.streamFile(file, getContentType(uri));
            file.close();
            return;
        }
        server.send(404, F("text/html"),
            F("<html><body style='background:#0a0a1a;color:#e2e8f0;font-family:sans-serif;display:flex;align-items:center;justify-content:center;height:100vh;margin:0'>"
              "<div style='text-align:center'><h1 style='font-size:48px;margin:0;color:#6366f1'>404</h1>"
              "<p style='color:#94a3b8;margin-top:8px'>Page not found</p>"
              "<a href='/' style='color:#6366f1;text-decoration:none;margin-top:16px;display:inline-block'>Back to WebOS-8266</a></div></body></html>"));
    });

    // Start server
    server.begin();

    Serial.println(F("[BOOT] Web server started!"));
    Serial.println();
    Serial.println(F("╔══════════════════════════════════════════════╗"));
    Serial.print(F("║  Access: http://"));
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print(WiFi.localIP());
    } else {
        Serial.print(WiFi.softAPIP());
    }
    Serial.println(F("              ║"));
    Serial.println(F("║  Auth:   admin / admin                      ║"));
    Serial.println(F("║  mDNS:   http://webos-8266.local            ║"));
    Serial.println(F("╚══════════════════════════════════════════════╝"));
    Serial.println();

    addLog("WebOS-8266 v" MICROWEBOS_VERSION " started", "info");
    addNotification("System boot complete", "success");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════════════════════
void loop() {
    server.handleClient();
    MDNS.update();
    ArduinoOTA.handle();

    // Run scheduler engine
    runScheduler();

    // Run pin watchers
    runPinWatchers();

    // Check NTP sync status
    if (!ntpSynced) {
        time_t now = time(nullptr);
        struct tm* ti = localtime(&now);
        if (ti->tm_year > 100) {
            ntpSynced = true;
            addLog("NTP time synced: " + getDateString() + " " + getTimeString(), "info");
        }
    }

    // Auto-reconnect WiFi if disconnected
    static unsigned long lastWiFiCheck = 0;
    if (wifiConnected && WiFi.status() != WL_CONNECTED) {
        if (millis() - lastWiFiCheck > 30000) {
            lastWiFiCheck = millis();
            Serial.println(F("[WiFi] Reconnecting..."));
            WiFi.reconnect();
            addLog("WiFi reconnection attempt", "warn");
        }
    }

    delay(5);
}