import asyncio
import logging
import signal
import sys

from .collectors import ClaudeCodeCollector, CodexCollector, SystemCollector
from .server.ws_server import StatusServer

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
)
logger = logging.getLogger("vibe-pi")

POLL_INTERVAL = 2.0


async def run():
    server = StatusServer(host="0.0.0.0", port=8765)
    await server.start()

    collectors = [
        ClaudeCodeCollector(),
        CodexCollector(),
        SystemCollector(),
    ]

    logger.info(f"Started with {len(collectors)} collectors, polling every {POLL_INTERVAL}s")
    logger.info("Waiting for ESP32 device to connect...")

    try:
        while True:
            all_status = {}
            active_tool = None

            for collector in collectors:
                data = await collector.collect()
                if data:
                    all_status[collector.name] = data
                    if data.get("status") == "active" and collector.name != "System":
                        active_tool = data

            payload = active_tool or {
                "tool": "idle",
                "status": "No active AI tools",
                "model": "",
                "tokens": "",
                "cost": "",
                "usage_pct": 0,
            }

            if "System" in all_status:
                payload["system"] = all_status["System"]

            await server.broadcast(payload)
            await asyncio.sleep(POLL_INTERVAL)

    except asyncio.CancelledError:
        pass
    finally:
        await server.stop()
        logger.info("Server stopped")


def main():
    loop = asyncio.new_event_loop()

    def shutdown():
        for task in asyncio.all_tasks(loop):
            task.cancel()

    loop.add_signal_handler(signal.SIGINT, shutdown)
    loop.add_signal_handler(signal.SIGTERM, shutdown)

    try:
        loop.run_until_complete(run())
    except KeyboardInterrupt:
        shutdown()
        loop.run_until_complete(asyncio.gather(*asyncio.all_tasks(loop), return_exceptions=True))
    finally:
        loop.close()


if __name__ == "__main__":
    main()
