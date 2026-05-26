"""System resource metrics collector."""

import psutil
from typing import Any

from .base import BaseCollector


class SystemCollector(BaseCollector):
    name = "system"
    display_name = "System"

    def __init__(self):
        self._prev_net = psutil.net_io_counters()
        self._prev_net_time = 0.0

    async def collect(self) -> dict[str, Any] | None:
        cpu = psutil.cpu_percent(interval=0.3)
        mem = psutil.virtual_memory()

        net = psutil.net_io_counters()
        import time
        now = time.time()
        dt = now - self._prev_net_time if self._prev_net_time > 0 else 1.0

        net_up_kbps = int((net.bytes_sent - self._prev_net.bytes_sent) / dt / 1024) if dt > 0 else 0
        net_down_kbps = int((net.bytes_recv - self._prev_net.bytes_recv) / dt / 1024) if dt > 0 else 0

        self._prev_net = net
        self._prev_net_time = now

        return {
            "cpu_pct": int(cpu),
            "mem_pct": int(mem.percent),
            "mem_used_gb": round(mem.used / (1024 ** 3), 1),
            "mem_total_gb": round(mem.total / (1024 ** 3), 1),
            "net_up_kbps": max(0, net_up_kbps),
            "net_down_kbps": max(0, net_down_kbps),
        }
