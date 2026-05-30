"""System resource metrics collector."""

import asyncio
import re
import subprocess
import sys
import time
from typing import Any

import psutil

from .base import BaseCollector


class SystemCollector(BaseCollector):
    name = "system"
    display_name = "System"

    def __init__(self):
        self._prev_net = psutil.net_io_counters()
        self._prev_net_time = 0.0

    async def collect(self) -> dict[str, Any] | None:
        # cpu_percent(interval=0.3) blocks for 300ms — run it off the event loop
        # so it doesn't stall the other collectors / the WS server.
        cpu = await asyncio.to_thread(psutil.cpu_percent, 0.3)
        mem = psutil.virtual_memory()
        gpu = await asyncio.to_thread(self._gpu_pct)        # -1 if unavailable
        temp = await asyncio.to_thread(self._temp_c)        # -1 if unavailable

        try:
            disk = int(psutil.disk_usage("/").percent)
        except OSError:
            disk = 0

        net = psutil.net_io_counters()
        now = time.time()
        dt = now - self._prev_net_time if self._prev_net_time > 0 else 1.0

        net_up_kbps = int((net.bytes_sent - self._prev_net.bytes_sent) / dt / 1024) if dt > 0 else 0
        net_down_kbps = int((net.bytes_recv - self._prev_net.bytes_recv) / dt / 1024) if dt > 0 else 0

        self._prev_net = net
        self._prev_net_time = now

        return {
            "cpu_pct": int(cpu),
            "gpu_pct": gpu,
            "mem_pct": int(mem.percent),
            "mem_used_gb": round(mem.used / (1024 ** 3), 1),
            "mem_total_gb": round(mem.total / (1024 ** 3), 1),
            "temp_c": temp,
            "disk_pct": disk,
            "net_up_kbps": max(0, net_up_kbps),
            "net_down_kbps": max(0, net_down_kbps),
        }

    @staticmethod
    def _gpu_pct() -> int:
        """GPU utilization %, or -1 if unavailable.

        macOS: IOAccelerator "Device Utilization %" via ioreg (no sudo).
        Linux/Windows: NVIDIA via nvidia-smi if present."""
        try:
            if sys.platform == "darwin":
                out = subprocess.run(
                    ["ioreg", "-r", "-c", "IOAccelerator", "-d", "1"],
                    capture_output=True, text=True, timeout=2).stdout
                vals = [int(m) for m in re.findall(r'"Device Utilization %"=(\d+)', out)]
                return max(vals) if vals else -1
            out = subprocess.run(
                ["nvidia-smi", "--query-gpu=utilization.gpu",
                 "--format=csv,noheader,nounits"],
                capture_output=True, text=True, timeout=2).stdout.strip()
            return int(out.splitlines()[0]) if out else -1
        except (OSError, ValueError, subprocess.SubprocessError):
            return -1

    @staticmethod
    def _temp_c() -> int:
        """CPU package temperature °C, or -1 if unavailable.

        Works out-of-box on Linux/Windows via psutil; macOS has no no-sudo
        source (install `osx-cpu-temp`/`istats` to enable)."""
        fn = getattr(psutil, "sensors_temperatures", None)
        if fn:
            try:
                temps = fn()
                for key in ("coretemp", "cpu_thermal", "k10temp", "acpitz", "cpu-thermal"):
                    if temps.get(key):
                        return round(temps[key][0].current)
                for arr in temps.values():
                    if arr:
                        return round(arr[0].current)
            except (OSError, RuntimeError):
                pass
        # macOS fallback: osx-cpu-temp if the user installed it.
        if sys.platform == "darwin":
            try:
                out = subprocess.run(["osx-cpu-temp"], capture_output=True,
                                     text=True, timeout=2).stdout
                m = re.search(r"([\d.]+)", out)
                if m:
                    return round(float(m.group(1)))
            except (OSError, ValueError, subprocess.SubprocessError):
                pass
        return -1
