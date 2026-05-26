"""mDNS service advertisement for device auto-discovery."""

import logging
import socket
from typing import Any

logger = logging.getLogger("vibe-pi.mdns")


class MDNSAdvertiser:
    """Advertises the Vibe Pi host agent via mDNS/Bonjour."""

    def __init__(self, port: int = 8765, host_version: str = "0.1.0"):
        self.port = port
        self.host_version = host_version
        self._zeroconf: Any = None
        self._info: Any = None

    async def start(self):
        try:
            from zeroconf import ServiceInfo, Zeroconf
        except ImportError:
            logger.warning("zeroconf not installed — mDNS discovery disabled. Install with: pip install zeroconf")
            return

        hostname = socket.gethostname()
        local_ip = _get_local_ip()

        self._info = ServiceInfo(
            "_vibepi._tcp.local.",
            f"VibePi Host ({hostname})._vibepi._tcp.local.",
            addresses=[socket.inet_aton(local_ip)],
            port=self.port,
            properties={
                "version": self.host_version,
                "protocol": "1",
                "hostname": hostname,
            },
            server=f"{hostname}.local.",
        )

        self._zeroconf = Zeroconf()
        self._zeroconf.register_service(self._info)
        logger.info(f"mDNS: advertising _vibepi._tcp on {local_ip}:{self.port}")

    async def stop(self):
        if self._zeroconf and self._info:
            self._zeroconf.unregister_service(self._info)
            self._zeroconf.close()
            logger.info("mDNS: service unregistered")


def _get_local_ip() -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"
