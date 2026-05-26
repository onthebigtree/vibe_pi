import subprocess
from pathlib import Path
from typing import Any

from .base import BaseCollector


class CodexCollector(BaseCollector):
    """Collects OpenAI Codex CLI status."""

    name = "Codex"

    async def collect(self) -> dict[str, Any] | None:
        status = {
            "tool": self.name,
            "status": "inactive",
            "model": "",
            "tokens": "0",
            "cost": "$0.00",
            "usage_pct": 0,
        }

        if not self._is_running():
            return status

        status["status"] = "active"
        return status

    def _is_running(self) -> bool:
        try:
            result = subprocess.run(
                ["pgrep", "-f", "codex"],
                capture_output=True,
                text=True,
                timeout=2,
            )
            return result.returncode == 0
        except Exception:
            return False
