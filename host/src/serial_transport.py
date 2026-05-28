"""Serial transport — protocol v2 over USB CDC.

Connects to ESP32 via USB serial and exchanges the same JSON messages
as the WebSocket server. Auto-detects the device on USB.
"""

import asyncio
import json
import logging
import time
from pathlib import Path

logger = logging.getLogger("vibe-pi.serial")


def find_esp32_port() -> str | None:
    """Find ESP32-S3 USB CDC serial port."""
    import glob
    import sys

    if sys.platform == "darwin":
        candidates = glob.glob("/dev/cu.usbmodem*")
        # Filter out bootloader ports (usbmodem20XX_YY_ZZZ pattern)
        # Prefer the active application port (typically usbmodemNNNN where N != 20*)
        active = [p for p in candidates if "_" not in p]
        if active:
            candidates = active
    elif sys.platform == "linux":
        candidates = glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")
    else:
        candidates = [f"COM{i}" for i in range(1, 20)]

    for port in sorted(candidates):
        return port
    return None


class SerialTransport:
    """Bidirectional protocol v2 transport over USB serial."""

    def __init__(self, port: str | None = None, baudrate: int = 115200):
        self._port_path = port
        self._baudrate = baudrate
        self._serial = None
        self._connected = False
        self._device_id = ""
        self._reader_task: asyncio.Task | None = None
        self._on_message = None
        self._on_register = None

    @property
    def connected(self) -> bool:
        return self._connected

    @property
    def device_id(self) -> str:
        return self._device_id

    def set_message_handler(self, handler):
        self._on_message = handler

    def set_register_handler(self, handler):
        self._on_register = handler

    async def start(self) -> bool:
        port = self._port_path or find_esp32_port()
        if not port:
            logger.info("No ESP32 USB device found")
            return False

        try:
            import serial_asyncio
            self._boot_ready = asyncio.Event()
            self._reader, self._writer = await serial_asyncio.open_serial_connection(
                url=port, baudrate=self._baudrate)
            self._connected = True
            logger.info(f"Serial connected: {port}")
            self._reader_task = asyncio.create_task(self._read_loop())
            try:
                await asyncio.wait_for(self._boot_ready.wait(), timeout=15)
            except asyncio.TimeoutError:
                pass
            await self.send_hello()
            return True

        except ImportError:
            return await self._start_sync(port)
        except Exception as e:
            logger.warning(f"Serial connection failed: {e}")
            return False

    async def _start_sync(self, port: str) -> bool:
        """Fallback using synchronous pyserial with asyncio thread executor."""
        try:
            import serial as pyserial
            self._serial = pyserial.Serial(
                port, self._baudrate, timeout=0.1,
                dsrdtr=False, rtscts=False)
            self._serial.dtr = False
            self._serial.rts = False
            self._connected = True
            self._boot_ready = asyncio.Event()
            self._registered = asyncio.Event()
            logger.info(f"Serial connected: {port}")

            self._reader_task = asyncio.create_task(self._read_loop_sync())

            try:
                await asyncio.wait_for(self._boot_ready.wait(), timeout=15)
                logger.info("ESP32 boot complete")
            except asyncio.TimeoutError:
                logger.info("Boot wait timeout")

            # Retry hello until registered (ESP32 may be busy with WiFi/WS)
            self._hello_task = asyncio.create_task(self._hello_retry_loop())
            return True
        except Exception as e:
            logger.warning(f"Serial connection failed: {e}")
            return False

    async def _hello_retry_loop(self):
        """Send hello repeatedly until device registers."""
        for attempt in range(10):
            if self._registered.is_set() or not self._connected:
                return
            logger.debug(f"Sending hello (attempt {attempt + 1})")
            await self.send_hello()
            try:
                await asyncio.wait_for(self._registered.wait(), timeout=3)
                return
            except asyncio.TimeoutError:
                pass
        logger.warning("Failed to establish serial protocol after 10 attempts")

    async def send_hello(self):
        hello = {"v": 2, "type": "hello", "ts": 0, "payload": {"host_version": "0.2.0", "protocol_version": 2}}
        await self.send_json(hello)

    async def send_json(self, msg: dict):
        line = json.dumps(msg, ensure_ascii=False) + "\n"
        data = line.encode("utf-8")
        try:
            if hasattr(self, '_writer') and self._writer:
                logger.debug(f"TX async ({len(data)}B): {line[:80]}")
                self._writer.write(data)
                await self._writer.drain()
            elif self._serial:
                logger.debug(f"TX ({len(data)}B): {line[:80]}")
                loop = asyncio.get_event_loop()
                await loop.run_in_executor(None, self._write_sync, data)
            else:
                logger.warning("No serial handle for send!")
        except Exception as e:
            logger.warning(f"Serial send error: {e}")
            self._connected = False

    def _write_sync(self, data: bytes):
        self._serial.write(data)
        self._serial.flush()

    async def _read_loop(self):
        """Async reader for serial_asyncio."""
        try:
            while self._connected:
                line = await self._reader.readline()
                if line:
                    await self._process_line(line.decode("utf-8", errors="replace").strip())
        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.warning(f"Serial read error: {e}")
            self._connected = False

    async def _read_loop_sync(self):
        """Read loop using executor-based readline to avoid in_waiting issues on macOS."""
        loop = asyncio.get_event_loop()
        try:
            while self._connected:
                try:
                    line = await asyncio.wait_for(
                        loop.run_in_executor(None, self._serial.readline),
                        timeout=0.5)
                except asyncio.TimeoutError:
                    continue
                if line:
                    text = line.decode("utf-8", errors="replace").strip()
                    if text:
                        await self._process_line(text)
        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.warning(f"Serial read error: {e}")
            self._connected = False

    async def send_cmd(self, cmd: str, **kwargs) -> dict | None:
        """Send a serial config command and wait for the response."""
        msg = {"cmd": cmd, **kwargs}
        self._cmd_response = asyncio.Event()
        self._cmd_result = None
        await self.send_json(msg)
        try:
            await asyncio.wait_for(self._cmd_response.wait(), timeout=3)
            return self._cmd_result
        except asyncio.TimeoutError:
            logger.warning(f"CMD '{cmd}' timeout — no response in 3s")
            return None

    async def _process_line(self, line: str):
        if not line.startswith("{"):
            if line.startswith("["):
                logger.debug(f"ESP32: {line}")
                if "Setup complete" in line or "entering loop" in line:
                    if hasattr(self, '_boot_ready'):
                        self._boot_ready.set()
            return

        try:
            data = json.loads(line)
        except json.JSONDecodeError:
            return

        msg_type = data.get("type")
        if not msg_type:
            if data.get("ok") is not None:
                logger.debug(f"Serial cmd response: {line[:100]}")
                if hasattr(self, '_cmd_response') and not self._cmd_response.is_set():
                    self._cmd_result = data
                    self._cmd_response.set()
            return

        if msg_type == "register":
            payload = data.get("payload", {})
            self._device_id = payload.get("device_id", "")
            logger.info(f"Device registered via serial: {self._device_id}")
            if hasattr(self, '_registered'):
                self._registered.set()
            if self._on_register:
                await self._on_register(payload)
        elif self._on_message:
            await self._on_message(data)

    async def stop(self):
        self._connected = False
        for task in [self._reader_task, getattr(self, '_hello_task', None)]:
            if task:
                task.cancel()
                try:
                    await task
                except asyncio.CancelledError:
                    pass
        if hasattr(self, '_writer') and self._writer:
            self._writer.close()
        if self._serial:
            self._serial.close()
        logger.info("Serial transport stopped")
