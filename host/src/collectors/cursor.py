"""Cursor IDE status collector."""

from typing import Any

from . import process_cache
from .base import BaseCollector


class CursorCollector(BaseCollector):
    name = "cursor"
    display_name = "Cursor"

    async def collect(self) -> dict[str, Any] | None:
        # Match the real Cursor process across platforms (macOS app bundle,
        # Windows Cursor.exe, Linux cursor binary) — not macOS CursorUIViewService.
        running = await process_cache.any_running(
            "Cursor.app/Contents/MacOS", "Cursor.exe", "/cursor/cursor")
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
            "task_state": "idle" if running else "inactive",
            "has_quota": False,   # no per-account 5h/7d source → device shows N/A
            "last_activity": 0.0,
        }
