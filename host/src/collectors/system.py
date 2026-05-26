import psutil
from typing import Any

from .base import BaseCollector


class SystemCollector(BaseCollector):
    """Collects system resource metrics."""

    name = "System"

    async def collect(self) -> dict[str, Any] | None:
        cpu = psutil.cpu_percent(interval=0.5)
        mem = psutil.virtual_memory()

        return {
            "tool": self.name,
            "cpu_pct": int(cpu),
            "mem_pct": int(mem.percent),
            "mem_used_gb": f"{mem.used / (1024 ** 3):.1f}",
            "mem_total_gb": f"{mem.total / (1024 ** 3):.1f}",
        }
