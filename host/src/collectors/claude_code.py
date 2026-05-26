import json
import os
import subprocess
from pathlib import Path
from typing import Any

from .base import BaseCollector


class ClaudeCodeCollector(BaseCollector):
    """Collects Claude Code session status by reading local state files."""

    name = "Claude Code"

    def __init__(self):
        self._home = Path.home()
        self._claude_dir = self._home / ".claude"

    async def collect(self) -> dict[str, Any] | None:
        status = {
            "tool": self.name,
            "status": "inactive",
            "model": "",
            "tokens": "0",
            "cost": "$0.00",
            "usage_pct": 0,
            "sessions": 0,
        }

        if not self._is_running():
            return status

        status["status"] = "active"

        usage = self._read_usage()
        if usage:
            status.update(usage)

        session_info = self._read_active_session()
        if session_info:
            status.update(session_info)

        return status

    def _is_running(self) -> bool:
        try:
            result = subprocess.run(
                ["pgrep", "-f", "claude"],
                capture_output=True,
                text=True,
                timeout=2,
            )
            return result.returncode == 0
        except Exception:
            return False

    def _read_usage(self) -> dict[str, Any] | None:
        # CC Switch reads from ~/.claude/usage/ or similar paths
        # Claude Code stores session data in ~/.claude/projects/
        usage_patterns = [
            self._claude_dir / "usage.json",
            self._claude_dir / "statsig" / "usage.json",
        ]

        for path in usage_patterns:
            if path.exists():
                try:
                    data = json.loads(path.read_text())
                    return self._parse_usage(data)
                except (json.JSONDecodeError, KeyError):
                    continue
        return None

    def _parse_usage(self, data: dict) -> dict[str, Any]:
        total_tokens = data.get("total_tokens", 0)
        total_cost = data.get("total_cost", 0.0)

        # Estimate usage percentage (based on typical daily limits)
        usage_pct = min(int((total_cost / 5.0) * 100), 100)

        return {
            "tokens": self._format_tokens(total_tokens),
            "cost": f"${total_cost:.2f}",
            "usage_pct": usage_pct,
        }

    def _read_active_session(self) -> dict[str, Any] | None:
        projects_dir = self._claude_dir / "projects"
        if not projects_dir.exists():
            return None

        latest_session = None
        latest_mtime = 0

        for project_dir in projects_dir.iterdir():
            if not project_dir.is_dir():
                continue
            for session_file in project_dir.glob("*.json"):
                mtime = session_file.stat().st_mtime
                if mtime > latest_mtime:
                    latest_mtime = mtime
                    latest_session = session_file

        if not latest_session:
            return None

        try:
            data = json.loads(latest_session.read_text())
            return {
                "model": data.get("model", "unknown"),
                "sessions": 1,
            }
        except (json.JSONDecodeError, KeyError):
            return None

    @staticmethod
    def _format_tokens(n: int) -> str:
        if n >= 1_000_000:
            return f"{n / 1_000_000:.1f}M tokens"
        if n >= 1_000:
            return f"{n / 1_000:.1f}K tokens"
        return f"{n} tokens"
