"""OpenAI Codex CLI status collector — reads ~/.codex/sessions/<year>/<month>/<day>/*.jsonl.

Codex writes its OWN rate-limit + token state into each session's jsonl as
`token_count` events:

    {"type":"event_msg","payload":{"type":"token_count","info":{
        "total_token_usage":{"input_tokens":..,"cached_input_tokens":..,
            "output_tokens":..,"reasoning_output_tokens":..,"total_tokens":..},
        "last_token_usage":{...},"model_context_window":..},
      "rate_limits":{
        "primary":{"used_percent":15.0,"window_minutes":300,"resets_at":..},     # 5h
        "secondary":{"used_percent":6.0,"window_minutes":10080,"resets_at":..}}}} # 7d

So the authoritative 5h/7d utilization is FIRST-PARTY and live in the session —
we read it straight from there (the last token_count event), instead of a stale
second-hand cache. The vibe-island cache is kept only as a fallback.
"""

import asyncio
import json
import time
from pathlib import Path
from typing import Any

from . import process_cache
from .base import BaseCollector


class CodexCollector(BaseCollector):
    name = "codex"
    display_name = "Codex"

    # Pricing per million tokens (input / output), rough — update as needed.
    # cached input is billed cheaper; reasoning tokens bill as output.
    _PRICES = {
        "gpt-5":  {"input": 5.0,  "cached": 0.50, "output": 15.0},
        "o3":     {"input": 15.0, "cached": 1.50, "output": 60.0},
        "o1":     {"input": 15.0, "cached": 1.50, "output": 60.0},
        "gpt-4o": {"input": 2.5,  "cached": 0.25, "output": 10.0},
        "default": {"input": 3.0, "cached": 0.30, "output": 12.0},
    }

    # How much of the tail to scan for the last token_count event. token_count
    # lines are large; 512KB comfortably covers several of the most recent ones.
    _TAIL = 512 * 1024
    _SESSION_FRESH_MIN = 60  # only treat a session as "current" within this many minutes

    def __init__(self):
        self._codex_dir = Path.home() / ".codex"

    async def collect(self) -> dict[str, Any] | None:
        running = await process_cache.any_running("codex")
        result = {
            "status": "active" if running else "inactive",
            "model": "", "tokens_used": 0, "tokens_display": "0",
            "cost_usd": 0.0, "cost_display": "$0.00", "usage_pct": 0,
            "session_count": 0, "current_task": "", "uptime_min": 0,
            "task_state": "idle" if running else "inactive",
            "has_quota": False, "last_activity": 0.0,
        }

        # Filesystem walk + file read + parse — keep it off the event loop.
        session = await asyncio.to_thread(self._read_latest_session)
        if session:
            rl = session.pop("_rate_limits", None)
            result.update(session)
            age = session.get("uptime_min", 999)
            result["last_activity"] = time.time() - age * 60
            result["task_state"] = "working" if age < 1 else ("waiting" if age < 30 else "idle")

            # First-party 5h/7d windows straight from the session's rate_limits.
            if rl:
                result.update(rl)

        # Fallback: if the session had no rate_limits (older Codex, or none logged
        # yet), fall back to the vibe-island cache so the rings still mean something.
        if not result.get("has_quota"):
            cached = await asyncio.to_thread(self._read_rate_limits_cache)
            if cached:
                result.update(cached)

        return result

    # ── First-party: parse the session jsonl ────────────────────────
    def _read_latest_session(self) -> dict[str, Any] | None:
        sessions_dir = self._codex_dir / "sessions"
        if not sessions_dir.exists():
            return None
        latest, latest_mtime = None, 0.0
        try:
            for jsonl in sessions_dir.rglob("rollout-*.jsonl"):
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
        if age_min > self._SESSION_FRESH_MIN:
            return None
        return self._parse_session(latest, age_min)

    def _parse_session(self, path: Path, age_min: float) -> dict[str, Any] | None:
        try:
            with path.open("rb") as fp:
                fp.seek(0, 2)
                size = fp.tell()
                fp.seek(max(0, size - self._TAIL))
                tail = fp.read()
        except OSError:
            return None

        # Find the LAST token_count event (cumulative state) + the last model name.
        last_usage: dict | None = None
        last_rl: dict | None = None
        model = ""
        for raw in tail.split(b"\n"):
            raw = raw.strip()
            if not raw or b"token_count" not in raw and b'"model"' not in raw:
                continue
            try:
                e = json.loads(raw)
            except (json.JSONDecodeError, ValueError):
                continue
            # model can appear on turn/config lines
            m = _deep_get(e, "model")
            if isinstance(m, str) and m:
                model = m
            payload = e.get("payload") if isinstance(e, dict) else None
            if not isinstance(payload, dict) or payload.get("type") != "token_count":
                continue
            info = payload.get("info") or {}
            usage = info.get("total_token_usage") if isinstance(info, dict) else None
            if isinstance(usage, dict):
                last_usage = usage
            rl = payload.get("rate_limits")
            if isinstance(rl, dict):
                last_rl = rl

        out: dict[str, Any] = {
            "session_count": 1,
            "uptime_min": int(age_min),
        }
        if model:
            out["model"] = self._short_model(model)

        if last_usage:
            inp = int(last_usage.get("input_tokens") or 0)
            cached = int(last_usage.get("cached_input_tokens") or 0)
            out_tok = int(last_usage.get("output_tokens") or 0)
            reasoning = int(last_usage.get("reasoning_output_tokens") or 0)
            total = int(last_usage.get("total_tokens") or (inp + out_tok))
            # cached input is a subset of input_tokens; bill it at the cheaper rate.
            uncached = max(0, inp - cached)
            rates = self._PRICES.get(self._short_model(model), self._PRICES["default"])
            cost = (uncached * rates["input"]
                    + cached * rates["cached"]
                    + (out_tok + reasoning) * rates["output"]) / 1_000_000
            out.update({
                "tokens_used": total,
                "tokens_display": _fmt(total),
                "input_tokens": inp,
                "cached_input_tokens": cached,
                "output_tokens": out_tok,
                "reasoning_tokens": reasoning,
                "cost_usd": cost,
                "cost_display": f"${cost:.2f}",
            })

        if last_rl:
            primary = last_rl.get("primary") or {}      # 5h window (window_minutes 300)
            secondary = last_rl.get("secondary") or {}  # 7d window (window_minutes 10080)
            p5 = int(round(float(primary.get("used_percent") or 0)))
            p7 = int(round(float(secondary.get("used_percent") or 0)))
            out["_rate_limits"] = {
                "has_quota": True,
                "usage_5h_pct": p5,
                "usage_7d_pct": p7,
                "usage_5h_display": f"{p5}%",
                "usage_7d_display": f"{p7}%",
                "rl_5h_reset": int(primary.get("resets_at") or 0),
                "rl_7d_reset": int(secondary.get("resets_at") or 0),
                "usage_pct": p7,         # outer ring = 7d
                "rl_stale": False,       # straight from the live session = fresh
            }
        return out

    # ── Fallback: vibe-island second-hand cache ─────────────────────
    def _read_rate_limits_cache(self) -> dict | None:
        path = Path.home() / ".vibe-island" / "cache" / "usage-persist-openai.json"
        try:
            d = json.loads(path.read_text(encoding="utf-8"))
            mtime = path.stat().st_mtime
        except (OSError, ValueError):
            return None
        fh = d.get("five_hour") or {}
        sd = d.get("seven_day") or {}
        plan = (d.get("metadata") or {}).get("codex.planType") or ""
        fetched = d.get("fetched_at") or mtime
        p5 = int(fh.get("used_percentage") or 0)
        p7 = int(sd.get("used_percentage") or 0)
        return {
            "has_quota": True,
            "usage_5h_pct": p5,
            "usage_7d_pct": p7,
            "usage_5h_display": f"{p5}%",
            "usage_7d_display": f"{p7}%",
            "rl_5h_reset": int(fh.get("resets_at") or 0),
            "rl_7d_reset": int(sd.get("resets_at") or 0),
            "usage_pct": p7,
            "plan": plan.title() if plan else "",
            "rl_stale": (time.time() - float(fetched)) > 900,
        }

    @staticmethod
    def _short_model(m: str) -> str:
        m = (m or "").lower()
        if "gpt-5" in m:   # gpt-5, gpt-5.1-codex-max, etc.
            return "gpt-5"
        if "o3" in m:
            return "o3"
        if "o1" in m:
            return "o1"
        if "gpt-4o" in m:
            return "gpt-4o"
        return m[:10] if m else ""


def _deep_get(obj, key):
    """Return the first value for `key` found anywhere in a shallow dict/line."""
    if isinstance(obj, dict):
        if key in obj:
            return obj[key]
        for v in obj.values():
            r = _deep_get(v, key)
            if r is not None:
                return r
    return None


def _fmt(n: int) -> str:
    if n >= 1_000_000:
        return f"{n/1_000_000:.1f}M"
    if n >= 1_000:
        return f"{n/1_000:.1f}K"
    return str(int(n))
