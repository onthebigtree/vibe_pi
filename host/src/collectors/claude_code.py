"""Claude Code status collector.

Reads local Claude Code state from:
- ~/.claude/ directory structure
- Running process detection
- Session/project JSON files
"""

import json
import os
import subprocess
import time
from pathlib import Path
from typing import Any

from .base import BaseCollector


class ClaudeCodeCollector(BaseCollector):
    name = "claude_code"
    display_name = "Claude Code"

    def __init__(self, daily_budget: float = 5.0):
        self._home = Path.home()
        self._claude_dir = self._home / ".claude"
        self._daily_budget = daily_budget
        self._last_process_check = 0.0
        self._cached_running = False

    async def collect(self) -> dict[str, Any] | None:
        is_running = self._is_running()

        result = {
            "status": "active" if is_running else "inactive",
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

        session = self._find_active_session()
        if session:
            result.update(session)

        usage = self._read_usage_data()
        if usage:
            result.update(usage)

        return result

    def _is_running(self) -> bool:
        now = time.time()
        if now - self._last_process_check < 3.0:
            return self._cached_running
        self._last_process_check = now

        try:
            result = subprocess.run(
                ["pgrep", "-fl", "claude"],
                capture_output=True, text=True, timeout=2,
            )
            self._cached_running = result.returncode == 0
        except Exception:
            self._cached_running = False
        return self._cached_running

    def _find_active_session(self) -> dict[str, Any] | None:
        projects_dir = self._claude_dir / "projects"
        if not projects_dir.exists():
            return None

        latest_file = None
        latest_mtime = 0

        try:
            for project_dir in projects_dir.iterdir():
                if not project_dir.is_dir():
                    continue
                for f in project_dir.iterdir():
                    if f.suffix == ".json" and f.stat().st_mtime > latest_mtime:
                        latest_mtime = f.stat().st_mtime
                        latest_file = f
        except (PermissionError, OSError):
            return None

        if not latest_file:
            return None

        age_min = (time.time() - latest_mtime) / 60.0
        if age_min > 60:
            return None

        try:
            data = json.loads(latest_file.read_text())
            return {
                "model": data.get("model", ""),
                "current_task": data.get("task", data.get("summary", "")),
                "session_count": 1,
                "uptime_min": int(age_min) if age_min < 60 else 0,
            }
        except (json.JSONDecodeError, KeyError, OSError):
            return None

    def _read_usage_data(self) -> dict[str, Any] | None:
        search_paths = [
            self._claude_dir / "usage.json",
            self._claude_dir / "statsig" / "usage.json",
            self._claude_dir / "analytics" / "usage.json",
        ]

        for path in search_paths:
            if not path.exists():
                continue
            try:
                data = json.loads(path.read_text())
                tokens = data.get("total_tokens", data.get("tokens", 0))
                cost = data.get("total_cost", data.get("cost", 0.0))
                usage_pct = min(int((cost / self._daily_budget) * 100), 100) if self._daily_budget > 0 else 0

                return {
                    "tokens_used": tokens,
                    "tokens_display": _format_number(tokens, "tokens"),
                    "cost_usd": cost,
                    "cost_display": f"${cost:.2f}",
                    "usage_pct": usage_pct,
                }
            except (json.JSONDecodeError, KeyError, OSError):
                continue
        return None


def _format_number(n: int | float, unit: str = "") -> str:
    suffix = f" {unit}" if unit else ""
    if n >= 1_000_000:
        return f"{n / 1_000_000:.1f}M{suffix}"
    if n >= 1_000:
        return f"{n / 1_000:.1f}K{suffix}"
    return f"{int(n)}{suffix}"
