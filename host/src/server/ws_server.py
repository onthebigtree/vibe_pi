"""Protocol v2 WebSocket server with pairing, OTA, settings sync, device management."""

import asyncio
import json
import logging
import socket
import time
from dataclasses import dataclass, field

import websockets
from websockets.asyncio.server import Server, ServerConnection

from ..protocol import (MsgType, make_hello, make_pong, make_registered,
                        make_status, make_pair_confirm, make_pair_reject,
                        make_settings_sync, make_settings_ack, make_ota_available,
                        make_ota_start, make_reset_command, make_device_rename,
                        make_unpair, parse_msg)
from ..device_registry import DeviceRegistry
from ..pairing import PairingManager

logger = logging.getLogger("vibe-pi.server")

HOST_VERSION = "0.2.0"


@dataclass
class ConnectedDevice:
    websocket: ServerConnection
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
        self.connections: dict[ServerConnection, ConnectedDevice] = {}
        self._server: Server | None = None
        self._default_config: dict = {}

    def set_default_config(self, config: dict):
        self._default_config = config

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

        self._server = await websockets.serve(
            self._handler, self.host, self.port,
            ping_interval=20, ping_timeout=10,
            ssl=ssl_ctx,
        )
        logger.info(f"WebSocket server on ws://{self.host}:{self.port}")

    async def _handler(self, ws: ServerConnection):
        dev = ConnectedDevice(websocket=ws)
        self.connections[ws] = dev
        remote = ws.remote_address
        logger.info(f"Device connected from {remote}")

        hello = make_hello(HOST_VERSION, socket.gethostname(), self.collector_names)
        try:
            await ws.send(json.dumps(hello))
        except websockets.ConnectionClosed:
            self.connections.pop(ws, None)
            return

        try:
            async for raw in ws:
                await self._handle_message(dev, raw)
        except websockets.ConnectionClosed:
            pass
        finally:
            self.connections.pop(ws, None)
            logger.info(f"Device disconnected: {dev.device_id or remote}")

    async def _handle_message(self, dev: ConnectedDevice, raw: str | bytes):
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
                await dev.websocket.send(json.dumps(ack))
                logger.info(f"Device registered: {dev.device_id} (paired={is_paired})")

            case MsgType.PAIR_REQUEST:
                device_id = payload.get("device_id", "")
                pair_code = payload.get("pair_code", "")
                device_name = payload.get("device_name", "")
                self.pairing_mgr.on_pair_request(device_id, pair_code, device_name)
                logger.info(f"Pair request from {device_id}, code: {pair_code}")

            case MsgType.PING:
                dev.last_ping = time.time()
                await dev.websocket.send(json.dumps(make_pong()))

            case MsgType.SETTINGS_UPDATE:
                if dev.device_id:
                    self.registry.update_settings(dev.device_id, payload)
                await dev.websocket.send(json.dumps(make_settings_ack()))

            case MsgType.HEALTH_REPORT:
                logger.debug(f"Health from {dev.device_id}: heap={payload.get('free_heap')}, "
                            f"rssi={payload.get('wifi_rssi')}, crashes={payload.get('crash_count')}")

            case MsgType.OTA_PROGRESS:
                pct = payload.get("progress_pct", 0)
                logger.info(f"OTA progress {dev.device_id}: {pct}%")

            case MsgType.OTA_DONE:
                logger.info(f"OTA complete for {dev.device_id}: success={payload.get('success')}")

            case MsgType.OTA_FAILED:
                logger.warning(f"OTA failed for {dev.device_id}: {payload.get('error')}")

    # ── Outbound ──

    async def broadcast_status(self, tools: dict, system: dict, active_tool: str):
        if not self.connections:
            return
        msg = json.dumps(make_status(tools, system, active_tool), ensure_ascii=False)
        disconnected = []
        for ws, dev in self.connections.items():
            try:
                await ws.send(msg)
            except websockets.ConnectionClosed:
                disconnected.append(ws)
        for ws in disconnected:
            self.connections.pop(ws, None)

    async def send_to_device(self, device_id: str, msg: dict) -> bool:
        for ws, dev in self.connections.items():
            if dev.device_id == device_id:
                try:
                    await ws.send(json.dumps(msg))
                    return True
                except websockets.ConnectionClosed:
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
        if self._server:
            self._server.close()
            await self._server.wait_closed()
