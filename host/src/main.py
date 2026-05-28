"""Vibe Pi Host Agent — collects AI tool status and pushes to ESP32 display."""

import asyncio
import logging
import logging.handlers
import signal
import sys
from pathlib import Path

from .cli import build_parser, handle_devices, handle_ota, __version__
from .config import AppConfig, init_config, load_config
from .collectors import (ClaudeCodeCollector, CodexCollector, GeminiCLICollector,
                         CursorCollector, WindsurfCollector, SystemCollector)
from .collectors.base import BaseCollector
from .device_registry import DeviceRegistry
from .pairing import PairingManager
from .ota_server import OTAManager
from .server.ws_server import StatusServer
from .server.mdns import MDNSAdvertiser
from .server.web_dashboard import DASHBOARD_HTML
from .serial_transport import SerialTransport

logger = logging.getLogger("vibe-pi")


def build_collectors(cfg: AppConfig) -> list[BaseCollector]:
    collectors: list[BaseCollector] = []
    if cfg.collectors.claude_code:
        collectors.append(ClaudeCodeCollector(daily_budget=cfg.claude_code.daily_budget))
    if cfg.collectors.codex:
        collectors.append(CodexCollector())
    if cfg.collectors.gemini_cli:
        collectors.append(GeminiCLICollector())
    if cfg.collectors.cursor:
        collectors.append(CursorCollector())
    if cfg.collectors.windsurf:
        collectors.append(WindsurfCollector())
    if cfg.collectors.system:
        collectors.append(SystemCollector())

    try:
        from importlib.metadata import entry_points
        eps = entry_points(group="vibepi.collectors")
        for ep in eps:
            try:
                cls = ep.load()
                collectors.append(cls())
                logger.info(f"Plugin collector loaded: {ep.name}")
            except Exception as e:
                logger.warning(f"Failed to load plugin {ep.name}: {e}")
    except Exception:
        pass
    return collectors


def setup_logging(cfg: AppConfig):
    level = getattr(logging, cfg.logging.level.upper(), logging.INFO)
    fmt = logging.Formatter("%(asctime)s [%(name)s] %(levelname)s: %(message)s", datefmt="%H:%M:%S")

    root = logging.getLogger("vibe-pi")
    root.setLevel(level)

    console = logging.StreamHandler(sys.stdout)
    console.setFormatter(fmt)
    root.addHandler(console)

    if cfg.logging.file:
        fh = logging.handlers.RotatingFileHandler(
            cfg.logging.file, maxBytes=5 * 1024 * 1024, backupCount=3)
        fh.setFormatter(fmt)
        root.addHandler(fh)


async def run(cfg: AppConfig):
    collectors = build_collectors(cfg)
    collector_names = [c.name for c in collectors]

    registry = DeviceRegistry()
    pairing_mgr = PairingManager(registry)
    ota = OTAManager()

    server = StatusServer(
        host=cfg.server.host, port=cfg.server.port,
        collector_names=collector_names,
        registry=registry, pairing_mgr=pairing_mgr,
    )
    server.set_default_config({
        "poll_interval_ms": int(cfg.polling.interval * 1000),
        "brightness": cfg.display.brightness,
        "theme": cfg.display.theme,
        "active_pages": cfg.display.pages,
    })
    server.set_dashboard_html(DASHBOARD_HTML)
    server.set_ota_dir(ota.firmware_dir)

    mdns = MDNSAdvertiser(port=cfg.server.port, host_version=__version__) if cfg.server.mdns else None

    # Serial transport (USB)
    serial_tx = SerialTransport()
    serial_device_info: dict = {}

    async def on_serial_register(payload: dict):
        nonlocal serial_device_info
        serial_device_info = payload
        device_id = payload.get("device_id", "")
        registry.register_device(
            device_id,
            payload.get("firmware_version", ""),
            payload.get("hardware", ""),
            payload.get("mac", ""),
        )
        from .protocol import make_registered
        is_paired = registry.verify_token(device_id, payload.get("paired_token", ""))
        record = registry.get(device_id)
        config = record.settings if record and record.settings else server._default_config
        await serial_tx.send_json(make_registered(device_id, is_paired, config))
        logger.info(f"Serial device registered: {device_id}")

    async def on_serial_message(data: dict):
        from .protocol import MsgType, parse_msg, make_pong, make_settings_ack
        msg_type, payload = parse_msg(data)
        if msg_type == MsgType.PING:
            await serial_tx.send_json(make_pong())
        elif msg_type == MsgType.SETTINGS_UPDATE:
            if serial_tx.device_id:
                registry.update_settings(serial_tx.device_id, payload)
            await serial_tx.send_json(make_settings_ack())
        elif msg_type == MsgType.HEALTH_REPORT:
            logger.debug(f"Serial health: heap={payload.get('free_heap')}")

    serial_tx.set_register_handler(on_serial_register)
    serial_tx.set_message_handler(on_serial_message)

    await server.start(ssl_cert=cfg.server.ssl_cert, ssl_key=cfg.server.ssl_key)
    if mdns:
        await mdns.start()

    serial_ok = await serial_tx.start()

    logger.info(f"Vibe Pi Host Agent v{__version__} (protocol v2)")
    logger.info(f"Collectors: {', '.join(c.display_name for c in collectors)}")
    logger.info(f"Polling: {cfg.polling.interval}s")
    logger.info(f"Transports: WS={True}, Serial={serial_ok}")
    logger.info(f"Registered devices: {len(registry.get_all())}")
    logger.info("Waiting for device connections...")

    try:
        while True:
            tools_data: dict = {}
            system_data: dict = {}
            active_tool = "idle"

            async def safe_collect(c):
                try:
                    return c.name, await asyncio.wait_for(c.collect(), timeout=5.0)
                except asyncio.TimeoutError:
                    logger.warning(f"Collector {c.name} timed out")
                    return c.name, None
                except Exception as e:
                    logger.warning(f"Collector {c.name} error: {e}")
                    return c.name, None

            results = await asyncio.gather(*(safe_collect(c) for c in collectors))

            for name, data in results:
                if data is None:
                    continue
                if name == "system":
                    system_data = data
                else:
                    tools_data[name] = data
                    if data.get("status") == "active":
                        active_tool = name

            # Broadcast to WS devices
            if server.device_count > 0:
                await server.broadcast_status(tools_data, system_data, active_tool)

            # Send to serial device
            if serial_tx.connected and serial_tx.device_id:
                from .protocol import make_status
                await serial_tx.send_json(make_status(tools_data, system_data, active_tool))

            await asyncio.sleep(cfg.polling.interval)

    except asyncio.CancelledError:
        pass
    finally:
        await serial_tx.stop()
        if mdns:
            await mdns.stop()
        await server.stop()


def main():
    parser = build_parser()
    args = parser.parse_args()

    command = args.command or "run"

    if command == "init":
        path = init_config(args.config if hasattr(args, "config") else None)
        print(f"Config created: {path}")
        return

    if command == "devices":
        handle_devices(args)
        return

    if command == "ota":
        handle_ota(args)
        return

    if command == "pair":
        print(f"To confirm pairing for {args.device_id} with code {args.code}:")
        print("The host agent must be running. Use the web dashboard or API.")
        print(f"  curl -X POST http://localhost:8765/api/pairing/confirm \\")
        print(f"    -H 'Content-Type: application/json' \\")
        print(f"    -d '{{\"device_id\":\"{args.device_id}\",\"code\":\"{args.code}\"}}'")
        return

    cfg = load_config(getattr(args, "config", None))
    if getattr(args, "debug", False):
        cfg.logging.level = "DEBUG"

    setup_logging(cfg)

    loop = asyncio.new_event_loop()

    def shutdown():
        for task in asyncio.all_tasks(loop):
            task.cancel()

    loop.add_signal_handler(signal.SIGINT, shutdown)
    loop.add_signal_handler(signal.SIGTERM, shutdown)

    try:
        loop.run_until_complete(run(cfg))
    except KeyboardInterrupt:
        shutdown()
        loop.run_until_complete(asyncio.gather(*asyncio.all_tasks(loop), return_exceptions=True))
    finally:
        loop.close()


if __name__ == "__main__":
    main()
