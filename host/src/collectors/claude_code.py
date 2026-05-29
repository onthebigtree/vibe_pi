"""Claude Code status collector.

Finds the active Claude Code session JSONL under ~/.claude/projects/ and
aggregates token usage / cost. The active session is read *incrementally* (only
bytes appended since last poll). Separately, account-wide usage over rolling 5h
and 7d windows is computed across all recent sessions on a slow cadence, and a
coarse task state (working / waiting / inactive) is derived from activity recency.
"""

import asyncio
import json
import time
from datetime import datetime
from pathlib import Path
from typing import Any

from . import process_cache
from .base import BaseCollector


def _iso_to_epoch(s: str) -> float | None:
    try:
        return datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp()
    except (ValueError, AttributeError):
        return None


class ClaudeCodeCollector(BaseCollector):
    name = "claude_code"
    display_name = "Claude Code"

    _WINDOW_TAIL = 1_000_000   # bytes of each session's tail scanned for windows
    _WINDOWS_TTL = 120.0       # recompute 5h/7d aggregates at most this often

    # Outer-ring denominator = weekly token budget, by plan tier. Detected from
    # ~/.claude.json oauthAccount.{organizationRateLimitTier|userRateLimitTier}.
    # IMPORTANT: our token sum includes cache_create/read and runs ~10-60x larger
    # than Anthropic's official metering (e.g. Max5 ≈ 88K, Max20 ≈ 220K tokens per
    # 5h window). So these are CALIBRATED to our own basis (scaled by the official
    # Pro:Max5:Max20 ratio ≈ 1 : 5 : 12.5), not the published quotas. Tune freely.
    _PLAN_WEEKLY = {
        "Pro":     6_000_000,
        "Max 5x": 30_000_000,
        "Max 20x": 75_000_000,
    }
    _DEFAULT_WEEKLY = 40_000_000

    def __init__(self, daily_budget: float = 5.0):
        self._home = Path.home()
        self._claude_dir = self._home / ".claude"
        self._daily_budget = daily_budget
        self._cost_history: list[tuple[float, float]] = []
        # Active-session discovery cache.
        self._active_path: Path | None = None
        self._last_scan = 0.0
        self._scan_interval = 15.0
        # Incremental-parse state for the active file.
        self._acc_path: str | None = None
        self._acc = self._fresh_acc()
        # 5h/7d window aggregate cache.
        self._windows_cache: dict = self._zero_windows()
        self._windows_ts = 0.0
        # Detect plan tier once (it effectively never changes mid-run).
        self._plan, self._weekly_limit = self._detect_plan()

    def _detect_plan(self) -> tuple[str, int]:
        """Return (plan_display, weekly_token_budget) from the local OAuth account."""
        tier = ""
        try:
            d = json.loads((self._home / ".claude.json").read_text(encoding="utf-8"))
            acc = d.get("oauthAccount") or {}
            tier = (acc.get("userRateLimitTier")
                    or acc.get("organizationRateLimitTier") or "").lower()
        except (OSError, ValueError):
            pass
        if "max_20" in tier or "max20" in tier:
            plan = "Max 20x"
        elif "max_5" in tier or "max5" in tier:
            plan = "Max 5x"
        elif "pro" in tier:
            plan = "Pro"
        else:
            plan = ""
        return plan, self._PLAN_WEEKLY.get(plan, self._DEFAULT_WEEKLY)

    @staticmethod
    def _fresh_acc() -> dict:
        return {
            "offset": 0, "input": 0, "output": 0,
            "cache_read": 0, "cache_create": 0,
            "model": "", "topic": "", "last_task": "", "turns": 0,
            "last_ts": 0.0,
        }

    @staticmethod
    def _zero_windows() -> dict:
        return {
            "usage_5h_tokens": 0, "usage_5h_cost": 0.0, "usage_5h_display": "$0.00",
            "usage_7d_tokens": 0, "usage_7d_cost": 0.0, "usage_7d_display": "$0.00",
        }

    async def collect(self) -> dict[str, Any] | None:
        # Number of running Claude Code CLI sessions = "tasks running".
        tasks = await process_cache.count_basename("claude")
        running = tasks > 0
        result = {
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
            "task_state": "inactive",
            "task_state_display": "Inactive",
            "tasks": tasks,
        }
        # File discovery + reads + parse are blocking; run them off the loop.
        session = await asyncio.to_thread(self._collect_sync)
        last_ts = 0.0
        if session:
            last_ts = session.pop("_last_ts", 0.0)
            result.update(session)
        state, disp = self._task_state(running, last_ts)
        result["task_state"] = state
        result["task_state_display"] = disp
        return result

    def _collect_sync(self) -> dict[str, Any]:
        out: dict[str, Any] = {}
        found = self._find_active_session()
        if found:
            path, mtime = found
            age_min = (time.time() - mtime) / 60.0
            s = self._parse_incremental(path, age_min)
            if s:
                out.update(s)
        # REAL account usage from the vibe-island / Claude Code Hub cache — the
        # exact 5h & 7d window utilization the CLI statusline shows. Far more
        # accurate than summing local tokens (which is a different metering basis).
        rl = self._read_rate_limits()
        out["usage_5h_pct"] = rl["five_hour_pct"]
        out["usage_7d_pct"] = rl["seven_day_pct"]
        out["usage_5h_display"] = f"{rl['five_hour_pct']}%"
        out["usage_7d_display"] = f"{rl['seven_day_pct']}%"
        out["rl_5h_reset"] = rl["five_hour_reset"]
        out["rl_7d_reset"] = rl["seven_day_reset"]
        # Outer ring = real 7-day window utilization.
        out["usage_pct"] = rl["seven_day_pct"]
        # Plan tier is kept in the payload for background use (limits, logic) but
        # NOT shown on the device — model line stays just "Opus 4.8".
        out["plan"] = self._plan
        return out

    def _read_rate_limits(self) -> dict:
        """Real 5h/7d window utilization from the vibe-island cache (the source
        the Claude Code statusline uses). Zeros if the cache isn't present."""
        for name in ("rl.json", "usage-persist-anthropic.json"):
            try:
                d = json.loads((self._home / ".vibe-island" / "cache" / name)
                               .read_text(encoding="utf-8"))
            except (OSError, ValueError):
                continue
            fh = d.get("five_hour") or {}
            sd = d.get("seven_day") or {}
            return {
                "five_hour_pct": int(fh.get("used_percentage") or 0),
                "seven_day_pct": int(sd.get("used_percentage") or 0),
                "five_hour_reset": int(fh.get("resets_at") or 0),
                "seven_day_reset": int(sd.get("resets_at") or 0),
            }
        return {"five_hour_pct": 0, "seven_day_pct": 0,
                "five_hour_reset": 0, "seven_day_reset": 0}

    def _task_state(self, running: bool, last_ts: float) -> tuple[str, str]:
        if not running:
            return "inactive", "Inactive"
        if last_ts <= 0:
            return "idle", "Idle"
        age = time.time() - last_ts
        if age < 12:
            return "working", "Working"
        if age < 1800:           # responded within 30 min → waiting on you
            return "waiting", "Waiting"
        return "idle", "Idle"

    def _find_active_session(self) -> tuple[Path, float] | None:
        now = time.time()
        if self._active_path and (now - self._last_scan) < self._scan_interval:
            try:
                m = self._active_path.stat().st_mtime
                if (now - m) / 60.0 <= 60:
                    return self._active_path, m
            except OSError:
                pass

        projects_dir = self._claude_dir / "projects"
        if not projects_dir.exists():
            return None

        latest_file: Path | None = None
        latest_mtime = 0.0
        try:
            for project_dir in projects_dir.iterdir():
                if not project_dir.is_dir():
                    continue
                for f in project_dir.glob("*.jsonl"):
                    try:
                        m = f.stat().st_mtime
                        if m > latest_mtime:
                            latest_mtime = m
                            latest_file = f
                    except OSError:
                        continue
        except (PermissionError, OSError):
            return None

        self._last_scan = now
        if not latest_file or (now - latest_mtime) / 60.0 > 60:
            return None
        self._active_path = latest_file
        return latest_file, latest_mtime

    def _parse_incremental(self, path: Path, age_min: float) -> dict[str, Any] | None:
        key = str(path)
        try:
            size = path.stat().st_size
        except OSError:
            return None

        if key != self._acc_path or size < self._acc["offset"]:
            self._acc_path = key
            self._acc = self._fresh_acc()
        acc = self._acc

        if size > acc["offset"]:
            try:
                with path.open("rb") as fp:
                    fp.seek(acc["offset"])
                    chunk = fp.read(size - acc["offset"])
            except OSError:
                return None
            nl = chunk.rfind(b"\n")
            if nl != -1:
                acc["offset"] += nl + 1
                for raw in chunk[:nl].split(b"\n"):
                    self._ingest_line(raw, acc)

        total_tokens = acc["input"] + acc["output"] + acc["cache_create"]
        cost = self._estimate_cost(acc["model"], acc["input"], acc["output"],
                                   acc["cache_read"], acc["cache_create"])
        usage_pct = min(int(acc["cache_read"] / 200_000 * 100), 100)

        now = time.time()
        self._cost_history.append((now, cost))
        self._cost_history = [(t, c) for t, c in self._cost_history if now - t < 300]
        cost_per_min = 0.0
        if len(self._cost_history) >= 2:
            t0, c0 = self._cost_history[0]
            dt_min = (now - t0) / 60.0
            if dt_min > 0.1:
                cost_per_min = max(0.0, (cost - c0) / dt_min)

        last_task = acc["topic"] or acc["last_task"]
        return {
            "model": _short_model_name(acc["model"]),
            "current_task": last_task,
            "session_count": 1,
            "turn_count": acc["turns"],
            "uptime_min": int(age_min) if age_min < 60 else 0,
            "tokens_used": total_tokens,
            "tokens_display": _format_number(total_tokens),
            "input_tokens": acc["input"],
            "output_tokens": acc["output"],
            "cache_read_tokens": acc["cache_read"],
            "cache_create_tokens": acc["cache_create"],
            "cost_usd": cost,
            "cost_display": f"${cost:.2f}",
            "cost_per_min": cost_per_min,
            "cost_rate_display": f"${cost_per_min:.2f}/m" if cost_per_min > 0 else "",
            "usage_pct": usage_pct,
            "_last_ts": acc["last_ts"],
        }

    def _ingest_line(self, raw: bytes, acc: dict) -> None:
        raw = raw.strip()
        if not raw or not raw.startswith(b"{"):
            return
        try:
            entry = json.loads(raw)
        except (json.JSONDecodeError, ValueError):
            return
        ts = entry.get("timestamp")
        if ts:
            t = _iso_to_epoch(ts)
            if t and t > acc["last_ts"]:
                acc["last_ts"] = t
        msg = entry.get("message") or {}
        if not isinstance(msg, dict):
            return
        usage = msg.get("usage")
        if isinstance(usage, dict):
            acc["input"] += int(usage.get("input_tokens") or 0)
            acc["output"] += int(usage.get("output_tokens") or 0)
            acc["cache_read"] += int(usage.get("cache_read_input_tokens") or 0)
            acc["cache_create"] += int(usage.get("cache_creation_input_tokens") or 0)
        model = msg.get("model")
        if model and model != "<synthetic>":
            acc["model"] = model
        if entry.get("type") == "user":
            acc["turns"] += 1
            content = msg.get("content")
            extracted = ""
            if isinstance(content, str):
                extracted = content
            elif isinstance(content, list) and content:
                first = content[0]
                if isinstance(first, dict) and first.get("type") == "text":
                    extracted = first.get("text", "")
            if extracted and not extracted.startswith("<") and len(extracted) > 3:
                if not acc["topic"]:
                    acc["topic"] = extracted.split("\n")[0][:60]
                acc["last_task"] = extracted.split("\n")[0][:60]

    def _compute_windows(self) -> dict:
        """Account-wide token/cost over rolling 5h and 7d windows.

        Scans the tail of every session file modified within 7 days and buckets
        assistant-message usage by entry timestamp. Approximate (tail-capped) but
        cheap enough for a status display; refreshed every _WINDOWS_TTL seconds.
        """
        now = time.time()
        c7 = now - 7 * 86400
        c5 = now - 5 * 3600
        t5 = t7 = 0
        cost5 = cost7 = 0.0

        projects = self._claude_dir / "projects"
        if not projects.exists():
            return self._zero_windows()
        try:
            project_dirs = list(projects.iterdir())
        except OSError:
            return self._zero_windows()

        for proj in project_dirs:
            if not proj.is_dir():
                continue
            try:
                files = list(proj.glob("*.jsonl"))
            except OSError:
                continue
            for f in files:
                try:
                    st = f.stat()
                except OSError:
                    continue
                if st.st_mtime < c7:
                    continue
                try:
                    with f.open("rb") as fp:
                        if st.st_size > self._WINDOW_TAIL:
                            fp.seek(st.st_size - self._WINDOW_TAIL)
                            fp.readline()  # discard the partial first line
                        data = fp.read()
                except OSError:
                    continue
                for raw in data.split(b"\n"):
                    raw = raw.strip()
                    if not raw.startswith(b"{"):
                        continue
                    try:
                        e = json.loads(raw)
                    except (json.JSONDecodeError, ValueError):
                        continue
                    ts = e.get("timestamp")
                    if not ts:
                        continue
                    t = _iso_to_epoch(ts)
                    if t is None or t < c7:
                        continue
                    msg = e.get("message") or {}
                    if not isinstance(msg, dict):
                        continue
                    u = msg.get("usage")
                    if not isinstance(u, dict):
                        continue
                    inp = int(u.get("input_tokens") or 0)
                    out = int(u.get("output_tokens") or 0)
                    cr = int(u.get("cache_read_input_tokens") or 0)
                    cc = int(u.get("cache_creation_input_tokens") or 0)
                    cost = self._estimate_cost(msg.get("model") or "", inp, out, cr, cc)
                    tok = inp + out + cc
                    t7 += tok
                    cost7 += cost
                    if t >= c5:
                        t5 += tok
                        cost5 += cost

        return {
            "usage_5h_tokens": t5, "usage_5h_cost": cost5,
            "usage_5h_display": f"${cost5:.2f}",
            "usage_7d_tokens": t7, "usage_7d_cost": cost7,
            "usage_7d_display": f"${cost7:.2f}",
        }

    @staticmethod
    def _estimate_cost(model: str, in_tok: int, out_tok: int,
                       cache_read: int, cache_create: int) -> float:
        """Rough cost estimate based on Anthropic pricing (USD per million tokens)."""
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
    """claude-opus-4-8 → 'Opus 4.8', claude-sonnet-4-6 → 'Sonnet 4.6'."""
    if not model or model == "<synthetic>":
        return ""
    m = model.lower()
    family = ""
    if "opus" in m:
        family = "Opus"
    elif "sonnet" in m:
        family = "Sonnet"
    elif "haiku" in m:
        family = "Haiku"
    if not family:
        return model[:14]
    # Pull the major[.minor] version: claude-opus-4-8 → 4.8, claude-opus-4 → 4
    import re
    mt = re.search(r"-(\d+)(?:-(\d+))?", m)
    if mt:
        major = mt.group(1)
        minor = mt.group(2)
        return f"{family} {major}.{minor}" if minor else f"{family} {major}"
    return family


def _format_number(n: int | float) -> str:
    if n >= 1_000_000:
        return f"{n / 1_000_000:.1f}M"
    if n >= 1_000:
        return f"{n / 1_000:.1f}K"
    return f"{int(n)}"
