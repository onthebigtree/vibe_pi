"""Cursor IDE status collector."""

from typing import Any

from . import process_cache
from .base import BaseCollector


class CursorCollector(BaseCollector):
    name = "cursor"
    display_name = "Cursor"

    async def collect(self) -> dict[str, Any] | None:
        # Match the actual Cursor.app bundle, not macOS CursorUIViewService.
        running = await process_cache.any_running("Cursor.app/Contents/MacOS")
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
