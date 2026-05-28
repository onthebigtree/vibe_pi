"""Web dashboard HTML — served by the unified aiohttp server."""

DASHBOARD_HTML = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Vibe Pi — Dashboard</title>
<style>
:root{--bg:#0a0a0a;--surface:#111;--border:#2a2a2a;--text:#e0e0e0;--muted:#666;
  --accent:#64B5F6;--success:#4CAF50;--warn:#FFA726;--err:#EF5350;
  --claude:#D97757;--codex:#10A37F;--gemini:#4285F4;--radius:12px}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,system-ui,sans-serif;background:var(--bg);color:var(--text);min-height:100vh}
.container{max-width:960px;margin:0 auto;padding:24px}
header{display:flex;align-items:center;justify-content:space-between;margin-bottom:8px}
h1{font-size:24px;font-weight:700}
.version{color:var(--muted);font-size:12px}
nav{display:flex;gap:4px;margin-bottom:24px;border-bottom:1px solid var(--border);padding-bottom:8px}
nav button{background:none;border:none;color:var(--muted);font-size:14px;padding:8px 16px;cursor:pointer;
  border-radius:8px 8px 0 0;transition:all .2s}
nav button:hover{color:var(--text);background:var(--surface)}
nav button.active{color:var(--accent);border-bottom:2px solid var(--accent)}
.tab{display:none}.tab.active{display:block}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;margin-bottom:20px}
.card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:16px}
.card h3{font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:1px;margin-bottom:8px}
.metric{font-size:28px;font-weight:700;margin-bottom:2px}
.metric.claude{color:var(--claude)}.metric.codex{color:var(--codex)}
.metric.gemini{color:var(--gemini)}.metric.accent{color:var(--accent)}
.label{font-size:12px;color:var(--muted)}
.badge{display:inline-block;padding:2px 8px;border-radius:20px;font-size:11px;font-weight:600}
.badge.active{background:#1a3a1a;color:var(--success)}
.badge.inactive{background:#1a1a1a;color:var(--muted)}
.badge.paired{background:#1a2a3a;color:var(--accent)}
.device{display:flex;align-items:center;gap:12px;padding:12px;background:var(--bg);
  border-radius:8px;margin-bottom:8px}
.device .dot{width:8px;height:8px;border-radius:50%}
.device .dot.online{background:var(--success)}.device .dot.offline{background:var(--muted)}
.device .info{flex:1}.device .info .name{font-weight:600;font-size:14px}
.device .info .detail{font-size:12px;color:var(--muted)}
.device .actions{display:flex;gap:4px}
.btn{background:var(--surface);border:1px solid var(--border);color:var(--text);padding:6px 12px;
  border-radius:6px;cursor:pointer;font-size:12px;transition:all .2s}
.btn:hover{border-color:var(--accent);color:var(--accent)}
.btn.danger:hover{border-color:var(--err);color:var(--err)}
.btn.primary{background:var(--accent);color:#000;border-color:var(--accent)}
.pair-card{background:var(--surface);border:1px solid var(--warn);border-radius:var(--radius);
  padding:16px;margin-bottom:12px;display:flex;align-items:center;justify-content:space-between}
.pair-card .code{font-size:24px;font-weight:700;letter-spacing:4px;color:var(--warn)}
table{width:100%;border-collapse:collapse}
th,td{text-align:left;padding:8px 12px;border-bottom:1px solid var(--border);font-size:13px}
th{color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:1px}
.empty{text-align:center;color:var(--muted);padding:32px;font-size:14px}
#status{font-size:11px;color:var(--muted);text-align:center;margin-top:24px}
input,select{background:var(--bg);border:1px solid var(--border);color:var(--text);
  padding:8px 12px;border-radius:6px;font-size:13px;width:100%}
.form-row{display:flex;gap:8px;align-items:center;margin-bottom:8px}
.form-row label{min-width:100px;font-size:13px;color:var(--muted)}
</style>
</head>
<body>
<div class="container">
<header>
  <h1>Vibe Pi</h1>
  <span class="version" id="ver">connecting...</span>
</header>

<nav>
  <button class="active" onclick="showTab('overview')">Overview</button>
  <button onclick="showTab('devices')">Devices</button>
  <button onclick="showTab('settings')">Settings</button>
  <button onclick="showTab('ota')">OTA</button>
</nav>

<!-- Overview Tab -->
<div class="tab active" id="tab-overview">
  <div class="grid" id="tools"></div>
  <div class="grid">
    <div class="card"><h3>CPU</h3><div class="metric accent" id="cpu">—</div><div class="label">Utilization</div></div>
    <div class="card"><h3>Memory</h3><div class="metric accent" id="mem">—</div><div class="label" id="mem-detail"></div></div>
    <div class="card"><h3>Network</h3><div class="metric accent" id="net-up">—</div><div class="label" id="net-detail">↑/↓</div></div>
  </div>
</div>

<!-- Devices Tab -->
<div class="tab" id="tab-devices">
  <div class="card" style="margin-bottom:16px">
    <h3>Pairing Requests</h3>
    <div id="pairing"><p class="empty">No pending pairing requests</p></div>
  </div>
  <div class="card">
    <h3>Connected Devices</h3>
    <div id="devices"></div>
  </div>
  <div class="card" style="margin-top:16px">
    <h3>Registered Devices</h3>
    <div id="registered"></div>
  </div>
</div>

<!-- Settings Tab -->
<div class="tab" id="tab-settings">
  <div class="card">
    <h3>Push Settings to Device</h3>
    <div id="settings-target" style="margin-bottom:12px">
      <div class="form-row"><label>Device</label><select id="settings-device"></select></div>
    </div>
    <div class="form-row"><label>Brightness</label><input type="range" id="s-brightness" min="0" max="100" value="80"><span id="s-brightness-val">80</span></div>
    <div class="form-row"><label>Theme</label><select id="s-theme"><option value="minimal">Minimal</option><option value="dark">Dark</option><option value="light">Light</option></select></div>
    <div class="form-row"><label>Language</label><select id="s-lang"><option value="en">English</option><option value="zh">中文</option></select></div>
    <div style="margin-top:12px"><button class="btn primary" onclick="pushSettings()">Push Settings</button></div>
  </div>
</div>

<!-- OTA Tab -->
<div class="tab" id="tab-ota">
  <div class="card">
    <h3>Firmware Updates</h3>
    <p class="label">Manage OTA updates via CLI: <code>vibe-pi-host ota publish FILE VER</code></p>
    <div id="ota-releases" style="margin-top:12px"><p class="empty">No firmware releases published</p></div>
  </div>
</div>

<p id="status">Connecting...</p>
</div>

<script>
const wsUrl = 'ws://'+location.host+'/ws';
let ws, lastStatus={}, connectedIds=new Set();

function connect(){
  ws = new WebSocket(wsUrl);
  ws.onopen = ()=>{
    document.getElementById('status').textContent='Connected — streaming live data';
    document.getElementById('ver').textContent='Host Agent';
    pollDevices();
  };
  ws.onclose = ()=>{
    document.getElementById('status').textContent='Disconnected — reconnecting...';
    setTimeout(connect, 3000);
  };
  ws.onmessage = (e)=>{
    const msg = JSON.parse(e.data);
    if(msg.type==='status') updateStatus(msg.payload);
  };
}
connect();

const toolColors={claude_code:'claude',codex:'codex',gemini_cli:'gemini',cursor:'accent',windsurf:'accent'};
const toolNames={claude_code:'Claude Code',codex:'Codex',gemini_cli:'Gemini CLI',cursor:'Cursor',windsurf:'Windsurf'};

function updateStatus(p){
  lastStatus=p;
  const el=document.getElementById('tools');
  let html='';
  for(const[k,v] of Object.entries(p.tools||{})){
    const cls=toolColors[k]||'accent';
    const name=toolNames[k]||k;
    const badge=v.status==='active'?'<span class="badge active">Active</span>':'<span class="badge inactive">Idle</span>';
    html+=`<div class="card"><h3>${name} ${badge}</h3>
      <div class="metric ${cls}">${v.tokens_display||v.tokens_total||'0'}</div>
      <div class="label">${v.cost_display||'$0.00'} · ${v.model||'—'}</div></div>`;
  }
  el.innerHTML=html||'<div class="card"><h3>No tools tracked</h3></div>';
  const s=p.system||{};
  document.getElementById('cpu').textContent=(s.cpu_pct||0)+'%';
  document.getElementById('mem').textContent=(s.mem_pct||0)+'%';
  document.getElementById('mem-detail').textContent=(s.mem_used_gb||0)+' / '+(s.mem_total_gb||0)+' GB';
  document.getElementById('net-up').textContent='↑'+(s.net_up_kbps||0)+' ↓'+(s.net_down_kbps||0);
  document.getElementById('net-detail').textContent='KB/s';
}

function showTab(name){
  document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));
  document.querySelectorAll('nav button').forEach(b=>b.classList.remove('active'));
  document.getElementById('tab-'+name).classList.add('active');
  document.querySelector(`nav button[onclick="showTab('${name}')"]`).classList.add('active');
  if(name==='devices') pollDevices();
}

async function pollDevices(){
  try{
    const r=await fetch('/api/devices');
    const d=await r.json();
    connectedIds=new Set((d.connected||[]).map(c=>c.device_id));
    renderDevices(d.connected||[], d.registered||[]);
    updateDeviceSelect(d.connected||[]);
    const pr=await fetch('/api/pairing');
    renderPairing(await pr.json());
  }catch(e){}
}

function renderDevices(connected, registered){
  const cel=document.getElementById('devices');
  if(!connected.length){cel.innerHTML='<p class="empty">No devices online</p>';
  }else{cel.innerHTML=connected.map(d=>`<div class="device">
    <div class="dot online"></div>
    <div class="info"><div class="name">${d.device_id}</div>
    <div class="detail">FW ${d.firmware_version||'?'} · ${d.hardware||''}</div></div>
    </div>`).join('');}

  const rel=document.getElementById('registered');
  if(!registered.length){rel.innerHTML='<p class="empty">No registered devices</p>';
  }else{rel.innerHTML='<table><tr><th>ID</th><th>Name</th><th>Status</th><th>FW</th><th>Actions</th></tr>'+
    registered.map(d=>{
      const online=connectedIds.has(d.device_id);
      return `<tr><td>${d.device_id}</td><td>${d.name||'—'}</td>
        <td><span class="badge ${online?'active':'inactive'}">${online?'Online':'Offline'}</span>
        ${d.paired?'<span class="badge paired">Paired</span>':''}</td>
        <td>${d.firmware_version||'—'}</td>
        <td><button class="btn" onclick="renameDevice('${d.device_id}')">Rename</button>
        <button class="btn danger" onclick="resetDevice('${d.device_id}')">Reset</button></td></tr>`;
    }).join('')+'</table>';}
}

function renderPairing(pending){
  const el=document.getElementById('pairing');
  if(!pending.length){el.innerHTML='<p class="empty">No pending pairing requests</p>';return;}
  el.innerHTML=pending.map(p=>`<div class="pair-card">
    <div><div class="name">${p.device_name||p.device_id}</div>
    <div class="code">${p.pair_code}</div></div>
    <button class="btn primary" onclick="confirmPair('${p.device_id}','${p.pair_code}')">Confirm</button>
    </div>`).join('');
}

function updateDeviceSelect(connected){
  const sel=document.getElementById('settings-device');
  sel.innerHTML=connected.map(d=>`<option value="${d.device_id}">${d.device_id}</option>`).join('')
    ||'<option value="">No devices online</option>';
}

async function confirmPair(deviceId,code){
  await fetch('/api/pairing/confirm',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({device_id:deviceId,code:code})});
  setTimeout(pollDevices,500);
}

async function pushSettings(){
  const deviceId=document.getElementById('settings-device').value;
  if(!deviceId) return;
  const settings={brightness:parseInt(document.getElementById('s-brightness').value),
    theme:document.getElementById('s-theme').value,
    language:document.getElementById('s-lang').value};
  await fetch(`/api/devices/${deviceId}/settings`,{method:'POST',
    headers:{'Content-Type':'application/json'},body:JSON.stringify(settings)});
}

async function renameDevice(deviceId){
  const name=prompt('New name for '+deviceId);
  if(!name) return;
  await fetch(`/api/devices/${deviceId}/rename`,{method:'POST',
    headers:{'Content-Type':'application/json'},body:JSON.stringify({name:name})});
  setTimeout(pollDevices,500);
}

async function resetDevice(deviceId){
  const level=prompt('Reset level (0-3)','0');
  if(level===null) return;
  await fetch(`/api/devices/${deviceId}/reset`,{method:'POST',
    headers:{'Content-Type':'application/json'},body:JSON.stringify({level:parseInt(level),reason:'web dashboard'})});
}

document.getElementById('s-brightness').oninput=function(){
  document.getElementById('s-brightness-val').textContent=this.value;
};

setInterval(pollDevices, 10000);
</script>
</body>
</html>"""
