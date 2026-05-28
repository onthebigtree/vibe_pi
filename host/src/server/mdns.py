"""mDNS service advertisement for device auto-discovery."""

import logging
import socket
from typing import Any

logger = logging.getLogger("vibe-pi.mdns")


class MDNSAdvertiser:
    def __init__(self, port: int = 8765, host_version: str = "0.2.0"):
        self.port = port
        self.host_version = host_version
        self._zeroconf: Any = None
        self._info: Any = None

    async def start(self):
        try:
            from zeroconf.asyncio import AsyncZeroconf
            from zeroconf import ServiceInfo
        except ImportError:
            logger.warning("zeroconf not installed — mDNS disabled")
            return

        hostname = socket.gethostname()
        all_ips = _get_all_lan_ips()

        self._info = ServiceInfo(
            "_vibepi._tcp.local.",
            f"VibePi Host ({hostname})._vibepi._tcp.local.",
            addresses=[socket.inet_aton(ip) for ip in all_ips],
            port=self.port,
            properties={
                "version": self.host_version,
                "protocol": "2",
                "hostname": hostname,
            },
            server=f"{hostname}.local.",
        )

        self._zeroconf = AsyncZeroconf()
        await self._zeroconf.async_register_service(self._info)
        logger.info(f"mDNS: advertising _vibepi._tcp on {', '.join(all_ips)}:{self.port}")

    async def stop(self):
        if self._zeroconf and self._info:
            await self._zeroconf.async_unregister_service(self._info)
            await self._zeroconf.async_close()
            logger.info("mDNS: service unregistered")


def _get_all_lan_ips() -> list[str]:
    ips = []
    try:
        import ifaddr
        for adapter in ifaddr.get_adapters():
            for ip_info in adapter.ips:
                if isinstance(ip_info.ip, str):
                    ip = ip_info.ip
                    if ip.startswith("192.168.") or ip.startswith("10.") or ip.startswith("172."):
                        ips.append(ip)
    except ImportError:
        pass

    if not ips:
        ips.append(_get_local_ip_udp())

    return ips


def _get_local_ip_udp() -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"
