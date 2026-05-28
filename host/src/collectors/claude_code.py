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
        """Find the most recently modified JSONL session file across all projects."""
        projects_dir = self._claude_dir / "projects"
        if not projects_dir.exists():
            return None

        latest_file = None
        latest_mtime = 0.0

        try:
            for project_dir in projects_dir.iterdir():
                if not project_dir.is_dir():
                    continue
                for f in project_dir.glob("*.jsonl"):
                    try:
                        mtime = f.stat().st_mtime
                        if mtime > latest_mtime:
                            latest_mtime = mtime
                            latest_file = f
                    except OSError:
                        continue
        except (PermissionError, OSError):
            return None

        if not latest_file:
            return None

        age_min = (time.time() - latest_mtime) / 60.0
        if age_min > 60:
            return None

        return self._parse_jsonl_session(latest_file, age_min)

    def _parse_jsonl_session(self, path: Path, age_min: float) -> dict[str, Any] | None:
        """Read the JSONL session — aggregate usage from all assistant messages."""
        try:
            total_input = 0
            total_output = 0
            total_cache_read = 0
            total_cache_create = 0
            model = ""
            last_task = ""

            # Read efficiently — file may be 10+ MB
            with path.open("rb") as fp:
                fp.seek(0, 2)
                file_size = fp.tell()
                # Read last 256KB (recent messages only — older are summarized)
                read_size = min(file_size, 256 * 1024)
                fp.seek(file_size - read_size)
                tail = fp.read().decode("utf-8", errors="replace")

            for raw in tail.split("\n"):
                raw = raw.strip()
                if not raw or not raw.startswith("{"):
                    continue
                try:
                    entry = json.loads(raw)
                except json.JSONDecodeError:
                    continue
                msg = entry.get("message") or {}
                if not isinstance(msg, dict):
                    continue
                usage = msg.get("usage")
                if isinstance(usage, dict):
                    total_input += int(usage.get("input_tokens") or 0)
                    total_output += int(usage.get("output_tokens") or 0)
                    total_cache_read += int(usage.get("cache_read_input_tokens") or 0)
                    total_cache_create += int(usage.get("cache_creation_input_tokens") or 0)
                if msg.get("model"):
                    model = msg["model"]
                if entry.get("type") == "user":
                    content = msg.get("content")
                    if isinstance(content, str) and content:
                        last_task = content[:80]
                    elif isinstance(content, list) and content:
                        first = content[0]
                        if isinstance(first, dict) and first.get("type") == "text":
                            last_task = first.get("text", "")[:80]

            total_tokens = total_input + total_output + total_cache_create
            cost = self._estimate_cost(model, total_input, total_output,
                                        total_cache_read, total_cache_create)
            usage_pct = (min(int((cost / self._daily_budget) * 100), 100)
                         if self._daily_budget > 0 else 0)

            return {
                "model": _short_model_name(model),
                "current_task": last_task,
                "session_count": 1,
                "uptime_min": int(age_min) if age_min < 60 else 0,
                "tokens_used": total_tokens,
                "tokens_display": _format_number(total_tokens),
                "cost_usd": cost,
                "cost_display": f"${cost:.2f}",
                "usage_pct": usage_pct,
            }
        except OSError:
            return None

    def _read_usage_data(self) -> dict[str, Any] | None:
        # Aggregated usage now comes from the active JSONL — no separate file needed.
        return None

    @staticmethod
    def _estimate_cost(model: str, in_tok: int, out_tok: int,
                       cache_read: int, cache_create: int) -> float:
        """Rough cost estimate based on Anthropic pricing (USD per million tokens)."""
        # Default Opus pricing; switch on model substring
        rates = {"input": 15.0, "output": 75.0, "cache_read": 1.50, "cache_create": 18.75}
        m = (model or "").lower()
        if "sonnet" in m:
            rates = {"input": 3.0, "output": 15.0, "cache_read": 0.30, "cache_create": 3.75}
        elif "haiku" in m:
            rates = {"input": 0.80, "output": 4.0, "cache_read": 0.08, "cache_create": 1.0}
        return (in_tok * rates["input"]
                + out_tok * rates["output"]
                + cache_read * rates["cache_read"]
                + cache_create * rates["cache_create"]) / 1_000_000


def _short_model_name(model: str) -> str:
    if not model:
        return ""
    m = model.lower()
    if "opus" in m:
        return "opus-4"
    if "sonnet" in m:
        return "sonnet-4"
    if "haiku" in m:
        return "haiku-4"
    return model[:14]


def _format_number(n: int | float) -> str:
    if n >= 1_000_000:
        return f"{n / 1_000_000:.1f}M"
    if n >= 1_000:
        return f"{n / 1_000:.1f}K"
    return f"{int(n)}"
