"""Vibe Pi Host Agent — collects AI tool status and pushes to ESP32 display."""

import asyncio
import logging
import logging.handlers
import signal
import sys
from pathlib import Path

from .cli import build_parser, handle_devices, handle_ota, __version__
from .config import AppConfig, init_config, load_config
from .collectors import ClaudeCodeCollector, CodexCollector, GeminiCLICollector, SystemCollector
from .collectors.base import BaseCollector
from .device_registry import DeviceRegistry
from .pairing import PairingManager
from .ota_server import OTAManager
from .server.ws_server import StatusServer
from .server.mdns import MDNSAdvertiser
from .server.web_dashboard import WebDashboard

logger = logging.getLogger("vibe-pi")


def build_collectors(cfg: AppConfig) -> list[BaseCollector]:
    collectors: list[BaseCollector] = []
    if cfg.collectors.claude_code:
        collectors.append(ClaudeCodeCollector(daily_budget=cfg.claude_code.daily_budget))
    if cfg.collectors.codex:
        collectors.append(CodexCollector())
    if cfg.collectors.gemini_cli:
        collectors.append(GeminiCLICollector())
    if cfg.collectors.system:
        collectors.append(SystemCollector())
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

    mdns = MDNSAdvertiser(port=cfg.server.port, host_version=__version__) if cfg.server.mdns else None
    dashboard = WebDashboard(ws_port=cfg.server.port)
    ota = OTAManager()

    await server.start()
    if mdns:
        await mdns.start()
    await dashboard.start()
    await ota.start()

    logger.info(f"Vibe Pi Host Agent v{__version__} (protocol v2)")
    logger.info(f"Collectors: {', '.join(c.display_name for c in collectors)}")
    logger.info(f"Polling: {cfg.polling.interval}s")
    logger.info(f"Registered devices: {len(registry.get_all())}")
    logger.info("Waiting for device connections...")

    try:
        while True:
            tools_data: dict = {}
            system_data: dict = {}
            active_tool = "idle"

            # Concurrent collection — all collectors run in parallel
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

            if server.device_count > 0:
                await server.broadcast_status(tools_data, system_data, active_tool)

            await asyncio.sleep(cfg.polling.interval)

    except asyncio.CancelledError:
        pass
    finally:
        await ota.stop()
        await dashboard.stop()
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
        # Interactive pairing requires running server — print instructions
        print(f"To confirm pairing for {args.device_id} with code {args.code}:")
        print("The host agent must be running. Pairing confirmation happens via the web dashboard.")
        return

    # Default: run
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
