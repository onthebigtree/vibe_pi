"""Unified aiohttp server: WebSocket protocol v2 + web dashboard + OTA file serving.

Replaces the previous websockets-based server. Uses aiohttp which handles the
ESP32 WebSocketsClient 'arduino' subprotocol correctly.
"""

import asyncio
import json
import logging
import socket
import time
from dataclasses import dataclass, field
from pathlib import Path

from aiohttp import web, WSMsgType

from ..protocol import (MsgType, make_hello, make_pong, make_registered,
                        make_status, make_pair_confirm, make_pair_reject,
                        make_settings_sync, make_settings_ack,
                        make_ota_start, make_reset_command, make_device_rename,
                        make_unpair, parse_msg)
from ..device_registry import DeviceRegistry
from ..pairing import PairingManager

logger = logging.getLogger("vibe-pi.server")

HOST_VERSION = "0.2.0"


@dataclass
class ConnectedDevice:
    ws: web.WebSocketResponse
    device_id: str = ""
    firmware_version: str = ""
    hardware: str = ""
    mac: str = ""
    connected_at: float = field(default_factory=time.time)
    last_ping: float = field(default_factory=time.time)


class StatusServer:
    def __init__(self, host: str = "0.0.0.0", port: int = 8765,
                 collector_names: list[str] | None = None,
                 registry: DeviceRegistry | None = None,
                 pairing_mgr: PairingManager | None = None):
        self.host = host
        self.port = port
        self.collector_names = collector_names or []
        self.registry = registry or DeviceRegistry()
        self.pairing_mgr = pairing_mgr or PairingManager(self.registry)
        self.connections: dict[web.WebSocketResponse, ConnectedDevice] = {}
        self._runner: web.AppRunner | None = None
        self._default_config: dict = {}
        self._dashboard_html: str = ""
        self._ota_dir: Path | None = None

    def set_default_config(self, config: dict):
        self._default_config = config

    def set_dashboard_html(self, html: str):
        self._dashboard_html = html

    def set_ota_dir(self, path: Path):
        self._ota_dir = path

    def set_ota_manager(self, mgr):
        self._ota_mgr = mgr

    @property
    def device_count(self) -> int:
        return len(self.connections)

    def get_connected_devices(self) -> list[dict]:
        return [
            {"device_id": d.device_id, "firmware_version": d.firmware_version,
             "hardware": d.hardware, "mac": d.mac}
            for d in self.connections.values() if d.device_id
        ]

    async def start(self, ssl_cert: str = "", ssl_key: str = ""):
        ssl_ctx = None
        if ssl_cert and ssl_key:
            import ssl
            ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ssl_ctx.load_cert_chain(ssl_cert, ssl_key)
            logger.info("WSS: TLS enabled")

        app = web.Application()
        app.router.add_get("/ws", self._ws_handler)
        app.router.add_get("/", self._dashboard_handler)
        app.router.add_get("/api/health", self._health_handler)
        app.router.add_get("/api/devices", self._devices_handler)
        app.router.add_get("/api/status", self._status_api_handler)
        app.router.add_get("/api/pairing", self._pairing_handler)
        app.router.add_post("/api/pairing/confirm", self._pairing_confirm_handler)
        app.router.add_post("/api/devices/{device_id}/settings", self._settings_push_handler)
        app.router.add_post("/api/devices/{device_id}/reset", self._reset_handler)
        app.router.add_post("/api/devices/{device_id}/rename", self._rename_handler)
        app.router.add_post("/api/devices/{device_id}/ota", self._ota_push_handler)
        app.router.add_get("/api/ota/releases", self._ota_releases_handler)
        app.router.add_post("/api/ota/push", self._ota_push_release_handler)

        if self._ota_dir:
            self._ota_dir.mkdir(parents=True, exist_ok=True)
            app.router.add_static("/firmware", str(self._ota_dir))

        runner = web.AppRunner(app)
        await runner.setup()
        site = web.TCPSite(runner, self.host, self.port, ssl_context=ssl_ctx)
        await site.start()
        self._runner = runner

        scheme = "wss" if ssl_ctx else "ws"
        logger.info(f"Server on {scheme}://{self.host}:{self.port} "
                     f"(WS: /ws, Dashboard: /, API: /api/, OTA: /firmware/)")

    # ── WebSocket handler ──

    async def _ws_handler(self, request: web.Request) -> web.WebSocketResponse:
        ws = web.WebSocketResponse(protocols=["arduino"], heartbeat=20.0)
        await ws.prepare(request)

        dev = ConnectedDevice(ws=ws)
        self.connections[ws] = dev
        remote = request.remote
        logger.info(f"Device connected from {remote}")

        hello = make_hello(HOST_VERSION, socket.gethostname(), self.collector_names)
        await ws.send_json(hello)

        try:
            async for msg in ws:
                if msg.type == WSMsgType.TEXT:
                    await self._handle_message(dev, msg.data)
                elif msg.type == WSMsgType.ERROR:
                    logger.warning(f"WS error from {dev.device_id}: {ws.exception()}")
        finally:
            self.connections.pop(ws, None)
            logger.info(f"Device disconnected: {dev.device_id or remote}")

        return ws

    async def _handle_message(self, dev: ConnectedDevice, raw: str):
        try:
            data = json.loads(raw)
        except json.JSONDecodeError:
            return

        msg_type, payload = parse_msg(data)
        if msg_type is None:
            return

        match msg_type:
            case MsgType.REGISTER:
                dev.device_id = payload.get("device_id", "")
                dev.firmware_version = payload.get("firmware_version", "")
                dev.hardware = payload.get("hardware", "")
                dev.mac = payload.get("mac", "")

                self.registry.register_device(
                    dev.device_id, dev.firmware_version, dev.hardware, dev.mac)

                paired_token = payload.get("paired_token", "")
                is_paired = self.registry.verify_token(dev.device_id, paired_token) if paired_token else False

                record = self.registry.get(dev.device_id)
                config = record.settings if record and record.settings else self._default_config

                ack = make_registered(dev.device_id, is_paired, config)
                await dev.ws.send_json(ack)
                logger.info(f"Device registered: {dev.device_id} (paired={is_paired})")

            case MsgType.PAIR_REQUEST:
                device_id = payload.get("device_id", "")
                pair_code = payload.get("pair_code", "")
                device_name = payload.get("device_name", "")
                self.pairing_mgr.on_pair_request(device_id, pair_code, device_name)
                logger.info(f"Pair request from {device_id}, code: {pair_code}")

            case MsgType.PING:
                dev.last_ping = time.time()
                await dev.ws.send_json(make_pong())

            case MsgType.SETTINGS_UPDATE:
                if dev.device_id:
                    self.registry.update_settings(dev.device_id, payload)
                await dev.ws.send_json(make_settings_ack())

            case MsgType.HEALTH_REPORT:
                logger.debug(f"Health from {dev.device_id}: heap={payload.get('free_heap')}, "
                            f"rssi={payload.get('wifi_rssi')}, crashes={payload.get('crash_count')}")

            case MsgType.OTA_PROGRESS:
                logger.info(f"OTA progress {dev.device_id}: {payload.get('progress_pct', 0)}%")

            case MsgType.OTA_DONE:
                logger.info(f"OTA complete for {dev.device_id}: success={payload.get('success')}")

            case MsgType.OTA_FAILED:
                logger.warning(f"OTA failed for {dev.device_id}: {payload.get('error')}")

    # ── Dashboard & API handlers ──

    async def _dashboard_handler(self, request: web.Request):
        if not self._dashboard_html:
            return web.Response(text="<h1>Vibe Pi</h1><p>Dashboard not configured</p>",
                                content_type="text/html")
        return web.Response(text=self._dashboard_html, content_type="text/html")

    async def _health_handler(self, request: web.Request):
        return web.json_response({"status": "ok", "version": HOST_VERSION})

    async def _devices_handler(self, request: web.Request):
        connected = self.get_connected_devices()
        registered = [
            {"device_id": d.device_id, "name": d.name, "paired": d.paired,
             "firmware_version": d.firmware_version, "hardware": d.hardware,
             "last_seen": d.last_seen}
            for d in self.registry.get_all()
        ]
        return web.json_response({"connected": connected, "registered": registered})

    async def _status_api_handler(self, request: web.Request):
        return web.json_response({
            "connected_devices": self.device_count,
            "registered_devices": len(self.registry.get_all()),
        })

    async def _pairing_handler(self, request: web.Request):
        pending = self.pairing_mgr.get_pending()
        return web.json_response([
            {"device_id": p.device_id, "pair_code": p.pair_code,
             "device_name": p.device_name, "timestamp": p.timestamp}
            for p in pending
        ])

    async def _pairing_confirm_handler(self, request: web.Request):
        body = await request.json()
        device_id = body.get("device_id", "")
        code = body.get("code", "")
        ok = await self.confirm_pair(device_id, code)
        return web.json_response({"ok": ok})

    async def _settings_push_handler(self, request: web.Request):
        device_id = request.match_info["device_id"]
        settings = await request.json()
        ok = await self.sync_settings(device_id, settings)
        return web.json_response({"ok": ok})

    async def _reset_handler(self, request: web.Request):
        device_id = request.match_info["device_id"]
        body = await request.json()
        ok = await self.send_reset(device_id, body.get("level", 0), body.get("reason", ""))
        return web.json_response({"ok": ok})

    async def _rename_handler(self, request: web.Request):
        device_id = request.match_info["device_id"]
        body = await request.json()
        ok = await self.rename_device(device_id, body.get("name", ""))
        return web.json_response({"ok": ok})

    async def _ota_push_handler(self, request: web.Request):
        device_id = request.match_info["device_id"]
        body = await request.json()
        ok = await self.send_to_device(device_id, body)
        return web.json_response({"ok": ok})

    async def _ota_releases_handler(self, request: web.Request):
        if not getattr(self, "_ota_mgr", None):
            return web.json_response({"releases": []})
        return web.json_response({
            "releases": [
                {
                    "version": r.version, "filename": r.filename,
                    "size_bytes": r.size_bytes, "sha256": r.sha256,
                    "signature": r.signature, "channel": r.channel,
                    "changelog": r.changelog, "force": r.force,
                }
                for r in self._ota_mgr.get_all()
            ]
        })

    async def _ota_push_release_handler(self, request: web.Request):
        body = await request.json()
        device_id = body.get("device_id", "")
        version = body.get("version", "")
        if not self._ota_mgr:
            return web.json_response({"ok": False, "error": "ota disabled"})
        rel = self._ota_mgr._releases.get(version)
        if not rel:
            return web.json_response({"ok": False, "error": "version not found"})
        host_ip = socket.gethostbyname(socket.gethostname())
        url = f"http://{host_ip}:{self.port}/firmware/{rel.filename}"
        from ..protocol import make_ota_available
        msg = make_ota_available(
            rel.version, "", rel.size_bytes, rel.sha256,
            rel.changelog, "", url, rel.force, rel.channel, rel.signature)
        ok = await self.send_to_device(device_id, msg)
        return web.json_response({"ok": ok, "url": url})

    # ── Outbound to devices ──

    async def broadcast_status(self, tools: dict, system: dict, active_tool: str):
        if not self.connections:
            return
        msg = make_status(tools, system, active_tool)
        disconnected = []
        for ws, dev in list(self.connections.items()):
            try:
                await ws.send_json(msg)
            except (ConnectionResetError, RuntimeError, Exception):
                disconnected.append(ws)
        for ws in disconnected:
            self.connections.pop(ws, None)

    async def send_to_device(self, device_id: str, msg: dict) -> bool:
        for ws, dev in self.connections.items():
            if dev.device_id == device_id:
                try:
                    await ws.send_json(msg)
                    return True
                except Exception:
                    return False
        return False

    async def confirm_pair(self, device_id: str, code: str) -> bool:
        token = self.pairing_mgr.confirm_pair(device_id, code)
        if not token:
            await self.send_to_device(device_id, make_pair_reject("invalid_code"))
            return False
        await self.send_to_device(device_id, make_pair_confirm(
            device_id, token, socket.gethostname()))
        return True

    async def sync_settings(self, device_id: str, settings: dict) -> bool:
        self.registry.update_settings(device_id, settings)
        return await self.send_to_device(device_id, make_settings_sync(settings))

    async def push_ota(self, device_id: str, ota_msg: dict) -> bool:
        return await self.send_to_device(device_id, ota_msg)

    async def send_reset(self, device_id: str, level: int, reason: str = "") -> bool:
        return await self.send_to_device(device_id, make_reset_command(level, reason))

    async def rename_device(self, device_id: str, name: str) -> bool:
        self.registry.rename_device(device_id, name)
        return await self.send_to_device(device_id, make_device_rename(device_id, name))

    async def unpair_device(self, device_id: str) -> bool:
        self.registry.unpair_device(device_id)
        return await self.send_to_device(device_id, make_unpair(device_id))

    async def stop(self):
        for ws in list(self.connections.keys()):
            try:
                await ws.close()
            except Exception:
                pass
        self.connections.clear()
        if self._runner:
            await self._runner.cleanup()
