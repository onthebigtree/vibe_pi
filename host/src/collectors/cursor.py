"""Cursor IDE status collector."""

import subprocess
import time
from typing import Any

from .base import BaseCollector


class CursorCollector(BaseCollector):
    name = "cursor"
    display_name = "Cursor"

    def __init__(self):
        self._last_check = 0.0
        self._cached_running = False

    async def collect(self) -> dict[str, Any] | None:
        return {
            "status": "active" if self._is_running() else "inactive",
            "model": "",
            "tokens_used": 0,
            "tokens_display": "0",
            "cost_usd": 0.0,
            "cost_display": "$0.00",
            "usage_pct": 0,
            "session_count": 0,
            "current_task": "",
            "uptime_min": 0,
        }

    def _is_running(self) -> bool:
        now = time.time()
        if now - self._last_check < 3.0:
            return self._cached_running
        self._last_check = now
        try:
            result = subprocess.run(
                ["pgrep", "-fl", "Cursor"],
                capture_output=True, text=True, timeout=2,
            )
            self._cached_running = result.returncode == 0
        except Exception:
            self._cached_running = False
        return self._cached_running
