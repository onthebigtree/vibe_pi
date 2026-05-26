"""Vibe Pi Host Agent — collects AI tool status and pushes to ESP32 display."""

import argparse
import asyncio
import logging
import logging.handlers
import signal
import sys
from pathlib import Path

from .config import AppConfig, init_config, load_config
from .collectors import ClaudeCodeCollector, CodexCollector, GeminiCLICollector, SystemCollector
from .collectors.base import BaseCollector
from .server.ws_server import StatusServer
from .server.mdns import MDNSAdvertiser
from .server.web_dashboard import WebDashboard

logger = logging.getLogger("vibe-pi")

__version__ = "0.1.0"


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
            cfg.logging.file, maxBytes=5 * 1024 * 1024, backupCount=3
        )
        fh.setFormatter(fmt)
        root.addHandler(fh)


async def run(cfg: AppConfig):
    collectors = build_collectors(cfg)
    collector_names = [c.name for c in collectors]

    server = StatusServer(
        host=cfg.server.host,
        port=cfg.server.port,
        collector_names=collector_names,
    )
    server.set_default_device_config({
        "poll_interval_ms": int(cfg.polling.interval * 1000),
        "brightness": cfg.display.brightness,
        "theme": cfg.display.theme,
        "active_pages": cfg.display.pages,
    })

    mdns = MDNSAdvertiser(port=cfg.server.port, host_version=__version__) if cfg.server.mdns else None
    dashboard = WebDashboard(ws_port=cfg.server.port)

    await server.start()
    if mdns:
        await mdns.start()
    await dashboard.start()

    logger.info(f"Vibe Pi Host Agent v{__version__}")
    logger.info(f"Active collectors: {', '.join(c.display_name for c in collectors)}")
    logger.info(f"Polling interval: {cfg.polling.interval}s")
    logger.info("Waiting for ESP32 device to connect...")

    try:
        while True:
            tools_data: dict = {}
            system_data: dict = {}
            active_tool = "idle"

            for collector in collectors:
                try:
                    data = await collector.collect()
                except Exception as e:
                    logger.warning(f"Collector {collector.name} error: {e}")
                    continue

                if data is None:
                    continue

                if collector.name == "system":
                    system_data = data
                else:
                    tools_data[collector.name] = data
                    if data.get("status") == "active":
                        active_tool = collector.name

            if server.device_count > 0:
                await server.broadcast_status(tools_data, system_data, active_tool)

            await asyncio.sleep(cfg.polling.interval)

    except asyncio.CancelledError:
        pass
    finally:
        await dashboard.stop()
        if mdns:
            await mdns.stop()
        await server.stop()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="vibe-pi-host",
        description="Vibe Pi Host Agent — AI coding tool status monitor",
    )
    parser.add_argument("-v", "--version", action="version", version=f"%(prog)s {__version__}")
    parser.add_argument("-c", "--config", type=Path, help="Config file path (TOML)")
    parser.add_argument("-p", "--port", type=int, help="WebSocket server port (default: 8765)")
    parser.add_argument("--init-config", action="store_true", help="Create default config file and exit")
    parser.add_argument("--debug", action="store_true", help="Enable debug logging")
    return parser.parse_args()


def main():
    args = parse_args()

    if args.init_config:
        path = init_config(args.config)
        print(f"Config file created at: {path}")
        sys.exit(0)

    cfg = load_config(args.config)

    if args.debug:
        cfg.logging.level = "DEBUG"
    if args.port:
        cfg.server.port = args.port

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
