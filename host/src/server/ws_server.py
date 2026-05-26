"""Protocol-aware WebSocket server with device management."""

import asyncio
import json
import logging
import socket
import time
from dataclasses import dataclass, field

import websockets
from websockets.asyncio.server import Server, ServerConnection

from ..protocol import MsgType, make_hello, make_pong, make_registered, make_status, parse_msg

logger = logging.getLogger("vibe-pi.server")

HOST_VERSION = "0.1.0"


@dataclass
class DeviceInfo:
    websocket: ServerConnection
    device_id: str = ""
    firmware_version: str = ""
    hardware: str = ""
    mac: str = ""
    connected_at: float = field(default_factory=time.time)
    last_ping: float = field(default_factory=time.time)
    config: dict = field(default_factory=dict)


class StatusServer:
    def __init__(self, host: str = "0.0.0.0", port: int = 8765, collector_names: list[str] | None = None):
        self.host = host
        self.port = port
        self.collector_names = collector_names or []
        self.devices: dict[ServerConnection, DeviceInfo] = {}
        self._server: Server | None = None
        self._default_device_config: dict = {}

    def set_default_device_config(self, config: dict):
        self._default_device_config = config

    @property
    def device_count(self) -> int:
        return len(self.devices)

    async def start(self):
        self._server = await websockets.serve(
            self._handler,
            self.host,
            self.port,
            ping_interval=20,
            ping_timeout=10,
        )
        logger.info(f"WebSocket server listening on ws://{self.host}:{self.port}")

    async def _handler(self, websocket: ServerConnection):
        device = DeviceInfo(websocket=websocket)
        self.devices[websocket] = device
        remote = websocket.remote_address
        logger.info(f"Device connected from {remote}")

        hello = make_hello(
            host_version=HOST_VERSION,
            hostname=socket.gethostname(),
            collectors=self.collector_names,
        )
        try:
            await websocket.send(json.dumps(hello))
        except websockets.ConnectionClosed:
            self.devices.pop(websocket, None)
            return

        try:
            async for raw_message in websocket:
                await self._handle_message(device, raw_message)
        except websockets.ConnectionClosed:
            pass
        finally:
            did = device.device_id or remote
            self.devices.pop(websocket, None)
            logger.info(f"Device disconnected: {did}")

    async def _handle_message(self, device: DeviceInfo, raw: str | bytes):
        try:
            data = json.loads(raw)
        except json.JSONDecodeError:
            logger.warning("Received invalid JSON from device")
            return

        msg_type, payload = parse_msg(data)
        if msg_type is None:
            logger.warning(f"Unknown message type: {data.get('type')}")
            return

        match msg_type:
            case MsgType.REGISTER:
                device.device_id = payload.get("device_id", "unknown")
                device.firmware_version = payload.get("firmware_version", "")
                device.hardware = payload.get("hardware", "")
                device.mac = payload.get("mac", "")
                logger.info(f"Device registered: {device.device_id} (fw={device.firmware_version})")

                ack = make_registered(device.device_id, self._default_device_config)
                await device.websocket.send(json.dumps(ack))

            case MsgType.PING:
                device.last_ping = time.time()
                await device.websocket.send(json.dumps(make_pong()))

            case MsgType.SETTINGS_UPDATE:
                device.config.update(payload)
                logger.info(f"Device {device.device_id} settings updated: {payload}")

            case _:
                logger.debug(f"Unhandled message type from device: {msg_type}")

    async def broadcast_status(self, tools: dict, system: dict, active_tool: str):
        if not self.devices:
            return

        msg = json.dumps(make_status(tools, system, active_tool), ensure_ascii=False)
        disconnected = []

        for ws, device in self.devices.items():
            try:
                await ws.send(msg)
            except websockets.ConnectionClosed:
                disconnected.append(ws)

        for ws in disconnected:
            self.devices.pop(ws, None)

    async def stop(self):
        if self._server:
            self._server.close()
            await self._server.wait_closed()
            logger.info("WebSocket server stopped")
