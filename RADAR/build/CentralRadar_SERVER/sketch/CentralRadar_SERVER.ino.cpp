#include <Arduino.h>
#line 1 "/Users/lorenzopalmato/Desktop/Documenti/Progetti/RADAR/Software/CentralRadar_SERVER/CentralRadar_SERVER.ino"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// ================= WIFI AP =================
const char* AP_SSID = "RadarESP32";
const char* AP_PASS = "12345678";

WebServer server(80);
Preferences prefs;

// ================= RADAR NODES =================
#define MAX_RADARS 4
#define OFFLINE_TIMEOUT_MS 4000

#define MODE_AUTO   0
#define MODE_MANUAL 1
#define MODE_TARGET 2

struct RadarNode {
  int id;
  String name;

  int angle;
  long distance;
  bool valid;
  bool online;
  bool targetLocked;
  unsigned long lastSeen;

  bool running;
  int mode;
  int speedMs;
  int manualAngle;
  int targetRangeCm;
};

RadarNode radars[MAX_RADARS] = {
  {1, "Radar 1", 90, -1, false, false, false, 0, true, MODE_AUTO,   25, 90, 50},
  {2, "Radar 2", 90, -1, false, false, false, 0, true, MODE_AUTO,   25, 90, 50},
  {3, "Radar 3", 90, -1, false, false, false, 0, true, MODE_AUTO,   25, 90, 50},
  {4, "Radar 4", 90, -1, false, false, false, 0, true, MODE_AUTO,   25, 90, 50}
};

// ================= HTML =================

const char MAIN_page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Radar Central Station V5 LIVE</title>

<style>
  body {
    margin: 0;
    background: #06100d;
    color: #00ff88;
    font-family: Arial, sans-serif;
    text-align: center;
  }

  h1 {
    font-size: 24px;
    margin: 12px 0 4px 0;
  }

  #status {
    font-size: 14px;
    color: #9cffc8;
    margin-bottom: 8px;
  }

  .topline {
    width: 92%;
    max-width: 720px;
    margin: 8px auto;
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 8px;
  }

  .stat {
    border: 1px solid rgba(0,255,136,0.35);
    background: rgba(0,255,136,0.07);
    border-radius: 8px;
    padding: 8px;
    text-align: left;
  }

  .stat b {
    display: block;
    color: #caffdf;
    font-size: 11px;
    margin-bottom: 4px;
  }

  .stat span {
    color: #ffffff;
    font-size: 18px;
    font-weight: bold;
  }

  canvas {
    background: radial-gradient(circle at bottom, #02170c 0%, #020403 70%);
    border: 2px solid #00ff88;
    border-radius: 8px;
    max-width: 95vw;
    height: auto;
  }

  .tabs, .controls, .panel, .overview {
    width: 92%;
    max-width: 720px;
    margin: 10px auto;
  }

  button {
    background: #00ff88;
    color: #001b0d;
    border: none;
    border-radius: 8px;
    padding: 9px 12px;
    margin: 3px;
    font-weight: bold;
    font-size: 13px;
  }

  button.active {
    background: #00b7ff;
    color: #001018;
  }

  button.off {
    background: #ff4444;
    color: white;
  }

  button.target {
    background: #ffd23f;
    color: #241800;
  }

  .controls, .panel {
    padding: 12px;
    border: 1px solid #00ff88;
    border-radius: 8px;
    background: rgba(0,255,136,0.06);
    text-align: left;
  }

  .overview {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 8px;
  }

  .radar-card {
    position: relative;
    border: 1px solid rgba(0,255,136,0.35);
    background: rgba(0,255,136,0.07);
    border-radius: 8px;
    padding: 10px;
    text-align: left;
    min-height: 94px;
  }

  .radar-card.selected {
    border-color: #00b7ff;
    box-shadow: inset 0 0 0 1px #00b7ff;
  }

  .radar-card.alarm {
    border-color: #ffd23f;
    background: rgba(255,210,63,0.12);
  }

  .radar-card.offline {
    border-color: rgba(255,68,68,0.45);
    background: rgba(255,68,68,0.08);
  }

  .card-head {
    display: flex;
    align-items: center;
    gap: 7px;
    font-weight: bold;
    color: #ffffff;
    font-size: 14px;
  }

  .alarm-led {
    width: 13px;
    height: 13px;
    border-radius: 50%;
    background: #123226;
    border: 1px solid rgba(255,255,255,0.25);
  }

  .alarm-led.on {
    background: #ffd23f;
    box-shadow: 0 0 14px #ffd23f;
  }

  .alarm-led.danger {
    background: #ff4444;
    box-shadow: 0 0 14px #ff4444;
  }

  .card-main {
    margin-top: 9px;
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 6px;
    color: #b9ffd5;
    font-size: 12px;
  }

  .card-main strong {
    display: block;
    color: #ffffff;
    font-size: 17px;
    margin-top: 2px;
  }

  .badge {
    display: inline-block;
    margin-top: 8px;
    padding: 3px 6px;
    border-radius: 6px;
    background: rgba(0,183,255,0.18);
    color: #9fe9ff;
    font-size: 11px;
    font-weight: bold;
  }

  .rename-btn {
    float: right;
    margin: 6px 0 0 6px;
    padding: 5px 7px;
    background: rgba(255,255,255,0.14);
    color: #ffffff;
    font-size: 11px;
  }

  .row {
    margin: 10px 0;
  }

  label {
    display: block;
    font-size: 14px;
    margin-bottom: 4px;
  }

  input[type=range] {
    width: 100%;
  }

  .node {
    padding: 9px 4px;
    border-bottom: 1px solid rgba(0,255,136,0.25);
  }

  .node:last-child {
    border-bottom: none;
  }

  .online {
    color: #00ff88;
    font-weight: bold;
  }

  .offline {
    color: #ff4444;
    font-weight: bold;
  }

  .lock {
    color: #ffd23f;
    font-weight: bold;
  }

  .small {
    color: #7cffbd;
    font-size: 13px;
    margin: 8px 0 18px 0;
  }

  @media (max-width: 620px) {
    .topline {
      grid-template-columns: repeat(3, 1fr);
    }

    .overview {
      grid-template-columns: repeat(2, 1fr);
    }

    .stat span {
      font-size: 16px;
    }
  }
</style>
</head>

<body>
  <h1>ESP32-S3 CENTRAL V5 LIVE</h1>
  <div id="status">Caricamento...</div>

  <div class="topline">
    <div class="stat"><b>ONLINE</b><span id="onlineStat">0/4</span></div>
    <div class="stat"><b>ALLARMI</b><span id="alarmStat">0</span></div>
    <div class="stat"><b>TARGET LOCK</b><span id="lockStat">0</span></div>
  </div>

  <div class="tabs">
    <button id="tab0" class="active" onclick="selectRadar(0)">Tutti</button>
    <button id="tab1" onclick="selectRadar(1)">Radar 1</button>
    <button id="tab2" onclick="selectRadar(2)">Radar 2</button>
    <button id="tab3" onclick="selectRadar(3)">Radar 3</button>
    <button id="tab4" onclick="selectRadar(4)">Radar 4</button>
  </div>

  <div class="overview" id="overviewPanel"></div>

  <canvas id="radar" width="360" height="220"></canvas>

  <div class="controls">
    <div style="text-align:center;">
      <button id="btnRun" type="button" onclick="toggleRun()">STOP</button>
      <button id="btnAuto" type="button" onclick="setMode(0)">AUTO</button>
      <button id="btnManual" type="button" onclick="setMode(1)">MANUALE</button>
      <button id="btnTarget" type="button" class="target" onclick="setMode(2)">TARGET</button>
    </div>

    <div class="row">
      <label>Velocità scansione: <span id="speedVal">25</span> ms</label>
      <input id="speed" type="range" min="15" max="120" value="25"
        oninput="setSpeedLive(this.value)"
        onchange="setSpeed(this.value)">
    </div>

    <div class="row">
      <label>Angolo manuale: <span id="manualVal">90</span>°</label>
      <input id="manualAngle" type="range" min="10" max="170" value="90"
        oninput="setManualAngleLive(this.value)"
        onchange="setManualAngle(this.value)">
    </div>

    <div class="row">
      <label>Range Target: <span id="targetVal">50</span> cm</label>
      <input id="targetRange" type="range" min="20" max="150" value="50"
        oninput="setTargetRangeLive(this.value)"
        onchange="setTargetRange(this.value)">
    </div>
  </div>

  <div class="panel" id="nodesPanel">
    Nessun dato...
  </div>

  <div class="small">
    Wi-Fi: RadarESP32<br>
    IP: http://192.168.4.1
  </div>

<script>
const canvas = document.getElementById("radar");
const ctx = canvas.getContext("2d");
const statusDiv = document.getElementById("status");
const nodesPanel = document.getElementById("nodesPanel");
const overviewPanel = document.getElementById("overviewPanel");
const onlineStatEl = document.getElementById("onlineStat");
const alarmStatEl = document.getElementById("alarmStat");
const lockStatEl = document.getElementById("lockStat");
const btnRunEl = document.getElementById("btnRun");
const btnAutoEl = document.getElementById("btnAuto");
const btnManualEl = document.getElementById("btnManual");
const btnTargetEl = document.getElementById("btnTarget");
const speedEl = document.getElementById("speed");
const speedValEl = document.getElementById("speedVal");
const manualAngleEl = document.getElementById("manualAngle");
const manualValEl = document.getElementById("manualVal");
const targetRangeEl = document.getElementById("targetRange");
const targetValEl = document.getElementById("targetVal");

const cx = canvas.width / 2;
const cy = canvas.height - 18;
const rMax = 175;

let selectedRadar = 0;
let radarData = [
  { id: 1, name: "Radar 1", angle: 90, distance: -1, valid: false, online: false, targetLocked: false, running: true, mode: 0, speed: 25, manualAngle: 90, targetRange: 50 },
  { id: 2, name: "Radar 2", angle: 90, distance: -1, valid: false, online: false, targetLocked: false, running: true, mode: 0, speed: 25, manualAngle: 90, targetRange: 50 },
  { id: 3, name: "Radar 3", angle: 90, distance: -1, valid: false, online: false, targetLocked: false, running: true, mode: 0, speed: 25, manualAngle: 90, targetRange: 50 },
  { id: 4, name: "Radar 4", angle: 90, distance: -1, valid: false, online: false, targetLocked: false, running: true, mode: 0, speed: 25, manualAngle: 90, targetRange: 50 }
];
let trail = [];
let sendTimers = {};
let pendingQueries = {};
let editingUntil = 0;
let dataBusy = false;

const TRAIL_MS = 3000;
const RANGE_MAX = 200;

function degToRad(deg) {
  return deg * Math.PI / 180.0;
}

function modeName(m) {
  if (m === 0) return "AUTO";
  if (m === 1) return "MANUALE";
  if (m === 2) return "TARGET";
  return "?";
}

function isAlarm(n) {
  return n.online && n.valid && n.distance > 0 && n.distance <= n.targetRange;
}

function distanceLabel(n) {
  if (!n.valid || n.distance <= 0) return "--";
  return n.distance + " cm";
}

function htmlEscape(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function getSelectedNode() {
  if (selectedRadar === 0) return radarData[0];
  return radarData.find(n => n.id === selectedRadar) || radarData[0];
}

function getTargetIds() {
  if (selectedRadar === 0) return [0];
  return [selectedRadar];
}

function selectRadar(id) {
  selectedRadar = id;

  for (let i = 0; i <= 4; i++) {
    const btn = document.getElementById("tab" + i);
    if (btn) btn.className = (i === id) ? "active" : "";
  }

  syncControls();
  renderOverview();
}

function syncTabNames() {
  for (const n of radarData) {
    const tab = document.getElementById("tab" + n.id);
    if (tab) tab.textContent = n.name;
  }
}

function renameRadar(event, id) {
  event.stopPropagation();

  const n = radarData.find(node => node.id === id);
  if (!n) return;

  const nextName = prompt("Nuovo nome radar", n.name);
  if (nextName === null) return;

  const cleanName = nextName.trim().slice(0, 32);
  if (!cleanName) return;

  n.name = cleanName;
  editingUntil = Date.now() + 1200;
  syncTabNames();
  renderOverview();
  renderNodes();

  fetch("/rename?id=" + id + "&name=" + encodeURIComponent(cleanName))
    .catch(() => {});
}

function updateLocalParam(params) {
  const ids = selectedRadar === 0 ? [1, 2, 3, 4] : [selectedRadar];

  for (const id of ids) {
    const n = radarData.find(node => node.id === id);
    if (!n) continue;

    if (params.running !== undefined) n.running = !!params.running;
    if (params.mode !== undefined) n.mode = params.mode;
    if (params.speed !== undefined) n.speed = params.speed;
    if (params.manualAngle !== undefined) n.manualAngle = params.manualAngle;
    if (params.targetRange !== undefined) n.targetRange = params.targetRange;
  }
}

async function setParam(query, localParams) {
  if (localParams) {
    editingUntil = Date.now() + 700;
    updateLocalParam(localParams);
    syncControls();
  }

  for (const id of getTargetIds()) {
    fetch("/set?id=" + id + "&" + query).catch(() => {});
  }
}

function setParamLive(key, query, localParams, waitMs) {
  pendingQueries[key] = query;

  if (localParams) {
    editingUntil = Date.now() + 700;
    updateLocalParam(localParams);
    syncControls();
  }

  if (sendTimers[key]) clearTimeout(sendTimers[key]);

  sendTimers[key] = setTimeout(() => {
    const pending = pendingQueries[key];
    sendTimers[key] = null;
    pendingQueries[key] = null;
    if (pending) setParam(pending);
  }, waitMs);
}

function toggleRun() {
  const n = getSelectedNode();
  if (!n) return;
  const next = n.running ? 0 : 1;
  setParam("running=" + next, { running: next === 1 });
}

function setMode(m) {
  setParam("mode=" + m + "&running=1", { mode: m, running: true });
}

function setSpeed(v) {
  const value = Number(v);
  setParam("speed=" + value, { speed: value });
}

function setSpeedLive(v) {
  const value = Number(v);
  setParamLive("speed", "speed=" + value, { speed: value }, 90);
}

function setManualAngle(v) {
  const value = Number(v);
  setParam("manual=" + value + "&mode=1&running=1", {
    manualAngle: value,
    mode: 1,
    running: true
  });
}

function setManualAngleLive(v) {
  const value = Number(v);
  setParamLive("manual", "manual=" + value + "&mode=1&running=1", {
    manualAngle: value,
    mode: 1,
    running: true
  }, 70);
}

function setTargetRange(v) {
  const value = Number(v);
  setParam("target=" + value, { targetRange: value });
}

function setTargetRangeLive(v) {
  const value = Number(v);
  setParamLive("target", "target=" + value, { targetRange: value }, 90);
}

function syncControls() {
  const n = getSelectedNode();
  if (!n) return;

  btnRunEl.textContent = n.running ? "STOP" : "START";
  btnRunEl.className = n.running ? "" : "off";

  btnAutoEl.className = n.mode === 0 ? "active" : "";
  btnManualEl.className = n.mode === 1 ? "active" : "";
  btnTargetEl.className = n.mode === 2 ? "active target" : "target";

  speedEl.value = n.speed;
  speedValEl.textContent = n.speed;

  manualAngleEl.value = n.manualAngle;
  manualValEl.textContent = n.manualAngle;

  targetRangeEl.value = n.targetRange;
  targetValEl.textContent = n.targetRange;
}

function drawGrid() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  for (let r = 45; r <= rMax; r += 45) {
    ctx.strokeStyle = "rgba(0,255,136,0.35)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(cx, cy, r, Math.PI, 2 * Math.PI);
    ctx.stroke();
  }

  for (let a = 0; a <= 180; a += 15) {
    let rad = degToRad(a);
    let x = cx + Math.cos(rad) * rMax;
    let y = cy - Math.sin(rad) * rMax;

    ctx.strokeStyle = (a % 30 === 0) ? "rgba(0,255,136,0.45)" : "rgba(0,255,136,0.18)";
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(x, y);
    ctx.stroke();
  }

  ctx.strokeStyle = "#00ff88";
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(cx - rMax, cy);
  ctx.lineTo(cx + rMax, cy);
  ctx.stroke();

  ctx.fillStyle = "rgba(0,255,136,0.75)";
  ctx.font = "12px Arial";
  ctx.fillText("0°", cx + rMax - 12, cy - 5);
  ctx.fillText("90°", cx - 13, cy - rMax + 14);
  ctx.fillText("180°", cx - rMax - 8, cy - 5);
}

function nodeColor(id, alpha) {
  const colors = [
    `rgba(0,255,136,${alpha})`,
    `rgba(0,183,255,${alpha})`,
    `rgba(255,210,63,${alpha})`,
    `rgba(255,80,255,${alpha})`
  ];
  return colors[(id - 1) % colors.length];
}

function drawSweep(angle, id, locked) {
  let rad = degToRad(angle);
  let x = cx + Math.cos(rad) * rMax;
  let y = cy - Math.sin(rad) * rMax;

  ctx.strokeStyle = locked ? "rgba(255,210,63,1)" : nodeColor(id, 0.9);
  ctx.lineWidth = locked ? 4 : 2;
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(x, y);
  ctx.stroke();
}

function drawTrail() {
  const now = Date.now();
  trail = trail.filter(p => now - p.t < TRAIL_MS);

  for (const p of trail) {
    if (selectedRadar !== 0 && selectedRadar !== p.id) continue;

    let age = now - p.t;
    let alpha = 1.0 - age / TRAIL_MS;

    let rad = degToRad(p.angle);
    let rr = Math.min(p.distance, RANGE_MAX) / RANGE_MAX * rMax;

    let x = cx + Math.cos(rad) * rr;
    let y = cy - Math.sin(rad) * rr;

    ctx.fillStyle = p.locked ? `rgba(255,210,63,${alpha})` : nodeColor(p.id, alpha);
    ctx.beginPath();
    ctx.arc(x, y, p.locked ? 8 : 6, 0, 2 * Math.PI);
    ctx.fill();
  }
}

function drawRadar() {
  drawGrid();
  drawTrail();

  for (const n of radarData) {
    if (!n.online) continue;
    if (selectedRadar !== 0 && selectedRadar !== n.id) continue;
    drawSweep(n.angle, n.id, n.targetLocked);
  }
}

function renderOverview() {
  let html = "";
  let alarmCount = 0;
  let lockCount = 0;
  let onlineCount = 0;

  for (const n of radarData) {
    const alarm = isAlarm(n);
    if (alarm) alarmCount++;
    if (n.targetLocked) lockCount++;
    if (n.online) onlineCount++;

    const cardClass =
      "radar-card" +
      (selectedRadar === n.id ? " selected" : "") +
      (!n.online ? " offline" : "") +
      (alarm ? " alarm" : "");
    const ledClass =
      "alarm-led" +
      (n.targetLocked ? " danger" : "") +
      (!n.targetLocked && alarm ? " on" : "");

    html += `
      <div class="${cardClass}" onclick="selectRadar(${n.id})">
        <div class="card-head">
          <span class="${ledClass}"></span>
          <span>${htmlEscape(n.name)}</span>
        </div>
        <div class="card-main">
          <div>Distanza<strong>${distanceLabel(n)}</strong></div>
          <div>Angolo<strong>${n.angle}°</strong></div>
          <div>Stato<strong>${n.online ? "ON" : "OFF"}</strong></div>
          <div>Modo<strong>${modeName(n.mode)}</strong></div>
        </div>
        <span class="badge">${n.targetLocked ? "LOCK" : alarm ? "RILEVA" : n.running ? "SCAN" : "STOP"}</span>
        <button class="rename-btn" type="button" onclick="renameRadar(event, ${n.id})">Nome</button>
      </div>
    `;
  }

  overviewPanel.innerHTML = html;
  onlineStatEl.textContent = onlineCount + "/4";
  alarmStatEl.textContent = alarmCount;
  lockStatEl.textContent = lockCount;
}

function renderNodes() {
  let html = "";

  for (const n of radarData) {
    const cls = n.online ? "online" : "offline";
    const state = n.online ? "ONLINE" : "OFFLINE";
    const alarm = isAlarm(n) ? ' <span class="lock">ALLARME</span>' : "";
    const lock = n.targetLocked ? ' <span class="lock">TARGET LOCK</span>' : "";

    html += `
      <div class="node">
        <b>${htmlEscape(n.name)}</b>
        <span class="${cls}">${state}</span>${alarm}${lock}<br>
        Modo: ${modeName(n.mode)} |
        Angolo: ${n.angle}° |
        Distanza: ${distanceLabel(n)}<br>
        Speed: ${n.speed} ms |
        Manuale: ${n.manualAngle}° |
        Target: ${n.targetRange} cm
      </div>
    `;
  }

  nodesPanel.innerHTML = html;
}

async function updateData() {
  if (dataBusy) return;
  dataBusy = true;

  try {
    const res = await fetch("/data");
    const data = await res.json();

    if (Date.now() > editingUntil) {
      radarData = data.radars;
    } else {
      for (const incoming of data.radars) {
        const current = radarData.find(n => n.id === incoming.id);
        if (!current) continue;

        current.angle = incoming.angle;
        current.distance = incoming.distance;
        current.valid = incoming.valid;
        current.online = incoming.online;
        current.targetLocked = incoming.targetLocked;
      }
    }

    syncTabNames();

    const alarmCount = radarData.filter(isAlarm).length;
    const lockCount = radarData.filter(n => n.targetLocked).length;
    statusDiv.textContent = "Central Station attiva | Nodi online: " + data.onlineCount + " | Allarmi: " + alarmCount + " | Lock: " + lockCount;

    const now = Date.now();

    for (const n of radarData) {
      if (n.online && n.valid && n.distance > 0) {
        trail.push({
          id: n.id,
          angle: n.angle,
          distance: n.distance,
          locked: n.targetLocked,
          t: now
        });
      }
    }

    syncControls();
    renderOverview();
    renderNodes();
    drawRadar();

  } catch (e) {
    statusDiv.textContent = "Connessione persa...";
  } finally {
    dataBusy = false;
  }
}

drawGrid();
syncTabNames();
syncControls();
renderOverview();
setInterval(updateData, 200);
</script>
</body>
</html>
)rawliteral";

// ================= HELPERS =================

RadarNode* getRadarById(int id) {
  for (int i = 0; i < MAX_RADARS; i++) {
    if (radars[i].id == id) return &radars[i];
  }
  return nullptr;
}

void updateOnlineStatus() {
  unsigned long now = millis();

  for (int i = 0; i < MAX_RADARS; i++) {
    if (radars[i].lastSeen == 0) {
      radars[i].online = false;
    } else if (now - radars[i].lastSeen > OFFLINE_TIMEOUT_MS) {
      radars[i].online = false;
      radars[i].targetLocked = false;
    } else {
      radars[i].online = true;
    }
  }
}

String jsonEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  return s;
}

String cleanRadarName(String name, int fallbackId) {
  name.trim();

  if (name.length() == 0) {
    return "Radar " + String(fallbackId);
  }

  if (name.length() > 32) {
    name = name.substring(0, 32);
  }

  String cleaned = "";
  for (int i = 0; i < name.length(); i++) {
    uint8_t c = (uint8_t)name.charAt(i);
    if (c >= 32) cleaned += (char)c;
  }

  if (cleaned.length() == 0) {
    return "Radar " + String(fallbackId);
  }

  return cleaned;
}

void loadRadarNames() {
  prefs.begin("radar-names", false);

  for (int i = 0; i < MAX_RADARS; i++) {
    String key = "name" + String(radars[i].id);
    String saved = prefs.getString(key.c_str(), radars[i].name);
    radars[i].name = cleanRadarName(saved, radars[i].id);
  }
}

void saveRadarName(int id, String name) {
  String key = "name" + String(id);
  prefs.putString(key.c_str(), name);
}

// ================= WEB HANDLERS =================

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send_P(200, "text/html", MAIN_page);
}

void handlePing() {
  server.send(200, "text/plain", "pong");
}

void handleRename() {
  if (!server.hasArg("id") || !server.hasArg("name")) {
    server.send(400, "text/plain", "Missing id or name");
    return;
  }

  int id = server.arg("id").toInt();
  RadarNode* node = getRadarById(id);

  if (node == nullptr) {
    server.send(404, "text/plain", "Unknown radar id");
    return;
  }

  String name = cleanRadarName(server.arg("name"), id);
  node->name = name;
  saveRadarName(id, name);

  server.send(200, "text/plain", "OK");
}

void handleUpdate() {
  if (!server.hasArg("id")) {
    server.send(400, "text/plain", "Missing id");
    return;
  }

  int id = server.arg("id").toInt();
  RadarNode* node = getRadarById(id);

  if (node == nullptr) {
    server.send(404, "text/plain", "Unknown radar id");
    return;
  }

  if (server.hasArg("angle")) {
    node->angle = constrain(server.arg("angle").toInt(), 0, 180);
  }

  if (server.hasArg("distance")) {
    node->distance = server.arg("distance").toInt();
  }

  if (server.hasArg("valid")) {
    node->valid = server.arg("valid").toInt() == 1;
  }

  if (server.hasArg("locked")) {
    node->targetLocked = server.arg("locked").toInt() == 1;
  }

  node->online = true;
  node->lastSeen = millis();

  server.send(200, "text/plain", "OK");
}

void handleSet() {
  if (!server.hasArg("id")) {
    server.send(400, "text/plain", "Missing id");
    return;
  }

  int id = server.arg("id").toInt();

  if (id == 0) {
    for (int i = 0; i < MAX_RADARS; i++) {
      if (server.hasArg("running")) {
        radars[i].running = server.arg("running").toInt() == 1;
      }

      if (server.hasArg("mode")) {
        radars[i].mode = constrain(server.arg("mode").toInt(), 0, 2);
      }

      if (server.hasArg("speed")) {
        radars[i].speedMs = constrain(server.arg("speed").toInt(), 15, 120);
      }

      if (server.hasArg("manual")) {
        radars[i].manualAngle = constrain(server.arg("manual").toInt(), 10, 170);
      }

      if (server.hasArg("target")) {
        radars[i].targetRangeCm = constrain(server.arg("target").toInt(), 20, 150);
      }
    }

    server.send(200, "text/plain", "OK");
    return;
  }

  RadarNode* node = getRadarById(id);

  if (node == nullptr) {
    server.send(404, "text/plain", "Unknown radar id");
    return;
  }

  if (server.hasArg("running")) {
    node->running = server.arg("running").toInt() == 1;
  }

  if (server.hasArg("mode")) {
    node->mode = constrain(server.arg("mode").toInt(), 0, 2);
  }

  if (server.hasArg("speed")) {
    node->speedMs = constrain(server.arg("speed").toInt(), 15, 120);
  }

  if (server.hasArg("manual")) {
    node->manualAngle = constrain(server.arg("manual").toInt(), 10, 170);
  }

  if (server.hasArg("target")) {
    node->targetRangeCm = constrain(server.arg("target").toInt(), 20, 150);
  }

  server.send(200, "text/plain", "OK");
}

void handleCmd() {
  if (!server.hasArg("id")) {
    server.send(400, "text/plain", "Missing id");
    return;
  }

  int id = server.arg("id").toInt();
  RadarNode* node = getRadarById(id);

  if (node == nullptr) {
    server.send(404, "text/plain", "Unknown radar id");
    return;
  }

  String cmd = "";
  cmd += "running=" + String(node->running ? 1 : 0) + ";";
  cmd += "mode=" + String(node->mode) + ";";
  cmd += "speed=" + String(node->speedMs) + ";";
  cmd += "manual=" + String(node->manualAngle) + ";";
  cmd += "target=" + String(node->targetRangeCm) + ";";

  server.send(200, "text/plain", cmd);
}

void handleData() {
  updateOnlineStatus();

  int onlineCount = 0;
  for (int i = 0; i < MAX_RADARS; i++) {
    if (radars[i].online) onlineCount++;
  }

  String json = "{";
  json += "\"onlineCount\":" + String(onlineCount) + ",";
  json += "\"radars\":[";

  for (int i = 0; i < MAX_RADARS; i++) {
    if (i > 0) json += ",";

    json += "{";
    json += "\"id\":" + String(radars[i].id) + ",";
    json += "\"name\":\"" + jsonEscape(radars[i].name) + "\",";
    json += "\"angle\":" + String(radars[i].angle) + ",";
    json += "\"distance\":" + String(radars[i].distance) + ",";
    json += "\"valid\":" + String(radars[i].valid ? "true" : "false") + ",";
    json += "\"online\":" + String(radars[i].online ? "true" : "false") + ",";
    json += "\"targetLocked\":" + String(radars[i].targetLocked ? "true" : "false") + ",";
    json += "\"running\":" + String(radars[i].running ? "true" : "false") + ",";
    json += "\"mode\":" + String(radars[i].mode) + ",";
    json += "\"speed\":" + String(radars[i].speedMs) + ",";
    json += "\"manualAngle\":" + String(radars[i].manualAngle) + ",";
    json += "\"targetRange\":" + String(radars[i].targetRangeCm);
    json += "}";
  }

  json += "]}";

  server.send(200, "application/json", json);
}

// ================= WIFI =================

void startWiFi() {
  WiFi.disconnect(true);
  delay(200);

  WiFi.mode(WIFI_AP);
  delay(200);

  bool ok = WiFi.softAP(AP_SSID, AP_PASS);

  Serial.println();
  Serial.println("=== ESP32-S3 CENTRAL STATION V4 ===");
  Serial.print("AP OK: ");
  Serial.println(ok ? "SI" : "NO");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASS);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
}

// ================= SETUP =================

void setup() {
  Serial.begin(115200);
  delay(500);

  loadRadarNames();
  startWiFi();

  server.on("/", handleRoot);
  server.on("/ping", handlePing);
  server.on("/rename", handleRename);
  server.on("/update", handleUpdate);
  server.on("/set", handleSet);
  server.on("/cmd", handleCmd);
  server.on("/data", handleData);

  server.begin();

  Serial.println("Web server avviato");
  Serial.println("Apri: http://192.168.4.1");
}

// ================= LOOP =================

void loop() {
  server.handleClient();
}

