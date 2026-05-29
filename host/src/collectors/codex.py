"""OpenAI Codex CLI status collector — reads ~/.codex/sessions/<year>/<month>/<day>/*.jsonl."""

import asyncio
import json
import re
import time
from pathlib import Path
from typing import Any

from . import process_cache
from .base import BaseCollector


class CodexCollector(BaseCollector):
    name = "codex"
    display_name = "Codex"

    # GPT-5 / o3 / o1 pricing per million tokens (rough, update as needed)
    _PRICES = {
        "gpt-5": {"input": 5.0, "output": 15.0},
        "o3": {"input": 15.0, "output": 60.0},
        "o1": {"input": 15.0, "output": 60.0},
        "gpt-4o": {"input": 2.5, "output": 10.0},
        "default": {"input": 3.0, "output": 12.0},
    }

    def __init__(self):
        self._codex_dir = Path.home() / ".codex"
        self._token_re = re.compile(rb'"total_tokens":\s*(\d+)')
        self._input_re = re.compile(rb'"input_tokens":\s*(\d+)')
        self._output_re = re.compile(rb'"output_tokens":\s*(\d+)')
        self._model_re = re.compile(rb'"model":\s*"([^"]+)"')

    async def collect(self) -> dict[str, Any] | None:
        running = await process_cache.any_running("codex")
        result = {
            "status": "active" if running else "inactive",
            "model": "", "tokens_used": 0, "tokens_display": "0",
            "cost_usd": 0.0, "cost_display": "$0.00", "usage_pct": 0,
            "session_count": 0, "current_task": "", "uptime_min": 0,
        }
        # Filesystem walk + file read + regex — keep it off the event loop.
        session = await asyncio.to_thread(self._find_latest_session)
        if session:
            result.update(session)
        return result

    def _find_latest_session(self) -> dict[str, Any] | None:
        sessions_dir = self._codex_dir / "sessions"
        if not sessions_dir.exists():
            return None
        latest, latest_mtime = None, 0.0
        try:
            for jsonl in sessions_dir.rglob("*.jsonl"):
                try:
                    m = jsonl.stat().st_mtime
                    if m > latest_mtime:
                        latest_mtime, latest = m, jsonl
                except OSError:
                    continue
        except OSError:
            return None
        if not latest:
            return None
        age_min = (time.time() - latest_mtime) / 60.0
        if age_min > 60:
            return None
        return self._parse_jsonl(latest, age_min)

    def _parse_jsonl(self, path: Path, age_min: float) -> dict[str, Any] | None:
        try:
            with path.open("rb") as fp:
                fp.seek(0, 2)
                size = fp.tell()
                fp.seek(max(0, size - 256 * 1024))
                tail = fp.read()
            total_input = sum(int(m.group(1)) for m in self._input_re.finditer(tail))
            total_output = sum(int(m.group(1)) for m in self._output_re.finditer(tail))
            total = total_input + total_output
            if total == 0:
                # Fall back to any total_tokens fields seen
                total = sum(int(m.group(1)) for m in self._token_re.finditer(tail))
            model_match = self._model_re.search(tail)
            model = model_match.group(1).decode("utf-8", "replace") if model_match else ""
            rates = self._PRICES.get(self._short_model(model).lower(), self._PRICES["default"])
            cost = (total_input * rates["input"] + total_output * rates["output"]) / 1_000_000
            return {
                "model": self._short_model(model),
                "tokens_used": total,
                "tokens_display": _fmt(total),
                "cost_usd": cost,
                "cost_display": f"${cost:.2f}",
                "usage_pct": min(int(total / 100_000 * 100), 100),  # 100K tokens = 100%
                "session_count": 1,
                "uptime_min": int(age_min),
            }
        except (OSError, ValueError):
            return None

    @staticmethod
    def _short_model(m: str) -> str:
        m = m.lower()
        if "gpt-5" in m: return "gpt-5"
        if "o3" in m: return "o3"
        if "o1" in m: return "o1"
        if "gpt-4o" in m: return "gpt-4o"
        return m[:10] if m else ""


def _fmt(n: int) -> str:
    if n >= 1_000_000: return f"{n/1_000_000:.1f}M"
    if n >= 1_000: return f"{n/1_000:.1f}K"
    return str(int(n))
