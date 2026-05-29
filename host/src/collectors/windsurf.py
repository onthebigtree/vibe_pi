"""Windsurf (Codeium) IDE status collector."""

from typing import Any

from . import process_cache
from .base import BaseCollector


class WindsurfCollector(BaseCollector):
    name = "windsurf"
    display_name = "Windsurf"

    async def collect(self) -> dict[str, Any] | None:
        running = await process_cache.any_running("windsurf", "Windsurf")
        return {
            "status": "active" if running else "inactive",
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
