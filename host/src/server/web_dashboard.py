"""Lightweight web dashboard for host agent configuration and status preview."""

import json
import logging
import socket

logger = logging.getLogger("vibe-pi.web")

DASHBOARD_PORT = 8766

DASHBOARD_HTML = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Vibe Pi — Host Dashboard</title>
<style>
:root{--bg:#0a0a0a;--surface:#111;--border:#2a2a2a;--text:#e0e0e0;--muted:#666;
  --accent:#64B5F6;--success:#4CAF50;--warn:#FFA726;--err:#EF5350;
  --claude:#D97757;--codex:#10A37F;--gemini:#4285F4}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,system-ui,sans-serif;background:var(--bg);color:var(--text);
  padding:24px;max-width:800px;margin:0 auto}
h1{font-size:28px;margin-bottom:4px}
.sub{color:var(--muted);margin-bottom:32px;font-size:14px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:16px;margin-bottom:24px}
.card{background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:20px}
.card h3{font-size:14px;color:var(--muted);text-transform:uppercase;letter-spacing:1px;margin-bottom:12px}
.metric{font-size:32px;font-weight:700;margin-bottom:4px}
.metric.claude{color:var(--claude)}.metric.codex{color:var(--codex)}
.metric.gemini{color:var(--gemini)}.metric.accent{color:var(--accent)}
.label{font-size:13px;color:var(--muted)}
.badge{display:inline-block;padding:3px 10px;border-radius:20px;font-size:12px;font-weight:600}
.badge.active{background:#1a3a1a;color:var(--success)}.badge.inactive{background:#1a1a1a;color:var(--muted)}
.device{display:flex;align-items:center;gap:12px;padding:12px;background:var(--bg);
  border-radius:8px;margin-bottom:8px}
.device .dot{width:8px;height:8px;border-radius:50%;background:var(--success)}
.device .info{flex:1}.device .info .name{font-weight:600;font-size:14px}
.device .info .detail{font-size:12px;color:var(--muted)}
#status{font-size:12px;color:var(--muted);text-align:center;margin-top:24px}
</style>
</head>
<body>
<h1>Vibe Pi</h1>
<p class="sub">Host Agent Dashboard — Real-time AI tool monitoring</p>

<div class="grid" id="tools"></div>

<div class="card" style="margin-bottom:16px">
  <h3>Connected Devices</h3>
  <div id="devices"><p style="color:var(--muted);font-size:14px">No devices connected</p></div>
</div>

<div class="grid">
  <div class="card"><h3>CPU</h3><div class="metric accent" id="cpu">—</div><div class="label">Utilization</div></div>
  <div class="card"><h3>Memory</h3><div class="metric accent" id="mem">—</div><div class="label" id="mem-detail"></div></div>
  <div class="card"><h3>Network</h3><div class="label" id="net">—</div></div>
</div>

<p id="status">Connecting...</p>

<script>
const ws = new WebSocket('ws://'+location.hostname+':WS_PORT');
const toolColors = {claude_code:'claude',codex:'codex',gemini_cli:'gemini'};
const toolNames = {claude_code:'Claude Code',codex:'Codex',gemini_cli:'Gemini CLI'};

ws.onopen = ()=> document.getElementById('status').textContent='Connected — streaming live data';
ws.onclose = ()=> document.getElementById('status').textContent='Disconnected — refresh to reconnect';
ws.onmessage = (e)=>{
  const msg = JSON.parse(e.data);
  if(msg.type==='status') updateStatus(msg.payload);
  if(msg.type==='devices') updateDevices(msg.payload);
};

function updateStatus(p){
  const el=document.getElementById('tools');
  let html='';
  for(const[k,v] of Object.entries(p.tools||{})){
    const cls=toolColors[k]||'accent';
    const name=toolNames[k]||k;
    const badge=v.status==='active'?'<span class="badge active">Active</span>':'<span class="badge inactive">Idle</span>';
    html+=`<div class="card"><h3>${name} ${badge}</h3>
      <div class="metric ${cls}">${v.tokens_display||'0'}</div>
      <div class="label">${v.cost_display||'$0.00'} · ${v.model||'—'}</div></div>`;
  }
  el.innerHTML=html||'<div class="card"><h3>No tools tracked</h3></div>';

  const s=p.system||{};
  document.getElementById('cpu').textContent=(s.cpu_pct||0)+'%';
  document.getElementById('mem').textContent=(s.mem_pct||0)+'%';
  document.getElementById('mem-detail').textContent=(s.mem_used_gb||0)+' / '+(s.mem_total_gb||0)+' GB';
  document.getElementById('net').innerHTML=
    '↑ '+(s.net_up_kbps||0)+' KB/s &nbsp;&nbsp; ↓ '+(s.net_down_kbps||0)+' KB/s';
}

function updateDevices(devs){
  const el=document.getElementById('devices');
  if(!devs||devs.length===0){el.innerHTML='<p style="color:var(--muted);font-size:14px">No devices connected</p>';return;}
  el.innerHTML=devs.map(d=>`<div class="device"><div class="dot"></div>
    <div class="info"><div class="name">${d.device_id||'Unknown'}</div>
    <div class="detail">FW ${d.firmware_version||'?'} · ${d.hardware||''}</div></div></div>`).join('');
}
</script>
</body>
</html>"""


class WebDashboard:
    def __init__(self, ws_port: int = 8765, web_port: int = DASHBOARD_PORT):
        self.ws_port = ws_port
        self.web_port = web_port
        self._runner = None

    async def start(self):
        try:
            from aiohttp import web
        except ImportError:
            logger.info("Web dashboard disabled (install aiohttp: pip install 'vibe-pi-host[web]')")
            return

        app = web.Application()
        app.router.add_get("/", self._handle_index)
        app.router.add_get("/api/health", self._handle_health)

        runner = web.AppRunner(app)
        await runner.setup()
        site = web.TCPSite(runner, "0.0.0.0", self.web_port)
        await site.start()
        self._runner = runner

        ip = _get_local_ip()
        logger.info(f"Web dashboard: http://{ip}:{self.web_port}")

    async def _handle_index(self, request):
        from aiohttp import web
        html = DASHBOARD_HTML.replace("WS_PORT", str(self.ws_port))
        return web.Response(text=html, content_type="text/html")

    async def _handle_health(self, request):
        from aiohttp import web
        return web.json_response({"status": "ok", "version": "0.1.0"})

    async def stop(self):
        if self._runner:
            await self._runner.cleanup()


def _get_local_ip() -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"
