import asyncio
import json
import logging

import websockets
from websockets.asyncio.server import Server, ServerConnection

logger = logging.getLogger("vibe-pi")


class StatusServer:
    """WebSocket server that pushes status data to connected ESP32 devices."""

    def __init__(self, host: str = "0.0.0.0", port: int = 8765):
        self.host = host
        self.port = port
        self.clients: set[ServerConnection] = set()
        self._server: Server | None = None

    async def start(self):
        self._server = await websockets.serve(
            self._handler,
            self.host,
            self.port,
        )
        logger.info(f"WebSocket server listening on ws://{self.host}:{self.port}")

    async def _handler(self, websocket: ServerConnection):
        self.clients.add(websocket)
        remote = websocket.remote_address
        logger.info(f"Device connected: {remote}")
        try:
            async for message in websocket:
                logger.debug(f"Received from {remote}: {message}")
        except websockets.ConnectionClosed:
            pass
        finally:
            self.clients.discard(websocket)
            logger.info(f"Device disconnected: {remote}")

    async def broadcast(self, data: dict):
        if not self.clients:
            return
        message = json.dumps(data, ensure_ascii=False)
        disconnected = set()
        for client in self.clients:
            try:
                await client.send(message)
            except websockets.ConnectionClosed:
                disconnected.add(client)
        self.clients -= disconnected

    async def stop(self):
        if self._server:
            self._server.close()
            await self._server.wait_closed()
