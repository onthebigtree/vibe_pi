"""Usage history — a SQLite time-series store for AI-tool usage.

This is the data foundation the roadmap calls for: trends, budgets and
burn-rate prediction all read from here. The poll loop hands each collected
sample to `record()`; writes are buffered and flushed off the event loop so
they never block collection.

Design notes:
- One sqlite file at ~/.config/vibe-pi/history.db, WAL mode so the CLI can read
  while the agent writes.
- `cost_usd` is the collector's cumulative (all-time) cost, so a spend delta is
  just `last - first` over a window — that's what `burn_rate()` uses.
- All timestamps are unix seconds. Methods accept an explicit `now`/`ts` so the
  behaviour is deterministic under test (no hidden wall-clock reads).
"""

from __future__ import annotations

import asyncio
import logging
import sqlite3
import time
from datetime import datetime, timezone
from pathlib import Path
from threading import Lock

logger = logging.getLogger("vibe-pi.history")

HISTORY_PATH = Path.home() / ".config" / "vibe-pi" / "history.db"

_SCHEMA = """
CREATE TABLE IF NOT EXISTS usage_samples (
    id          INTEGER PRIMARY KEY,
    ts          INTEGER NOT NULL,
    tool        TEXT    NOT NULL,
    status      TEXT,
    tokens_used INTEGER DEFAULT 0,
    cost_usd    REAL    DEFAULT 0,
    cost_5h     REAL    DEFAULT 0,
    cost_7d     REAL    DEFAULT 0,
    pct_5h      REAL    DEFAULT 0,
    pct_7d      REAL    DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_usage_tool_ts ON usage_samples(tool, ts);
"""

_COLUMNS = ("ts", "tool", "status", "tokens_used", "cost_usd",
            "cost_5h", "cost_7d", "pct_5h", "pct_7d")
_NUMERIC_METRICS = ("tokens_used", "cost_usd", "cost_5h", "cost_7d", "pct_5h", "pct_7d")


def _day(ts: float) -> str:
    return datetime.fromtimestamp(ts, tz=timezone.utc).strftime("%Y-%m-%d")


class UsageHistory:
    def __init__(self, path: Path | str | None = None,
                 flush_interval: float = 60.0, retention_days: int = 90):
        self.path = Path(path) if path else HISTORY_PATH
        self.flush_interval = flush_interval
        self.retention_days = retention_days

        self.path.parent.mkdir(parents=True, exist_ok=True)
        # check_same_thread=False: the async poll loop stages rows in the loop
        # thread, while flush()/queries may run in an executor thread. All access
        # is serialized by self._lock, so the single connection is safe to share.
        self._conn = sqlite3.connect(str(self.path), check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        self._conn.execute("PRAGMA journal_mode=WAL")
        self._conn.execute("PRAGMA synchronous=NORMAL")
        self._conn.execute("PRAGMA busy_timeout=3000")
        self._conn.executescript(_SCHEMA)
        self._conn.commit()

        self._lock = Lock()
        self._buffer: list[tuple] = []
        self._last_flush = 0.0

    # ── Ingest ──────────────────────────────────────────────────────
    @staticmethod
    def _first(d: dict, *keys, cast=float, default=0.0):
        """Return the first present key, cast — collectors disagree on names
        (claude_code uses usage_5h_pct; others may use pct_5h). Be tolerant."""
        for k in keys:
            if k in d and d[k] is not None:
                try:
                    return cast(d[k])
                except (TypeError, ValueError):
                    return default
        return default

    def add(self, tools_data: dict, ts: float) -> int:
        """Stage one poll's samples. Returns how many rows were staged.

        Tools whose value isn't a dict are skipped. The 'system' collector is
        skipped — it carries host metrics, not tool usage.
        """
        staged = 0
        with self._lock:
            for tool, d in (tools_data or {}).items():
                if not isinstance(d, dict) or tool == "system":
                    continue
                self._buffer.append((
                    int(ts), str(tool), d.get("status"),
                    self._first(d, "tokens_used", cast=int, default=0),
                    self._first(d, "cost_usd"),
                    self._first(d, "cost_5h", "usage_5h_cost"),
                    self._first(d, "cost_7d", "usage_7d_cost"),
                    self._first(d, "pct_5h", "usage_5h_pct"),
                    self._first(d, "pct_7d", "usage_7d_pct"),
                ))
                staged += 1
        return staged

    def flush(self) -> int:
        """Write staged rows in one transaction. Returns rows written."""
        with self._lock:
            if not self._buffer:
                return 0
            rows, self._buffer = self._buffer, []
            placeholders = ",".join("?" * len(_COLUMNS))
            self._conn.executemany(
                f"INSERT INTO usage_samples ({','.join(_COLUMNS)}) VALUES ({placeholders})",
                rows,
            )
            self._conn.commit()
            return len(rows)

    def maybe_flush(self, now: float | None = None) -> int:
        """Flush only if `flush_interval` has elapsed since the last flush."""
        now = time.time() if now is None else now
        if now - self._last_flush >= self.flush_interval:
            self._last_flush = now
            return self.flush()
        return 0

    async def record(self, tools_data: dict, ts: float,
                     loop: asyncio.AbstractEventLoop | None = None) -> int:
        """Poll-loop entry point: stage now, flush off-thread on interval."""
        self.add(tools_data, ts)
        loop = loop or asyncio.get_event_loop()
        return await loop.run_in_executor(None, self.maybe_flush, ts)

    # ── Query ───────────────────────────────────────────────────────
    def tools(self) -> list[str]:
        with self._lock:
            cur = self._conn.execute(
                "SELECT DISTINCT tool FROM usage_samples ORDER BY tool")
            return [r["tool"] for r in cur.fetchall()]

    def count(self, tool: str | None = None) -> int:
        with self._lock:
            if tool:
                cur = self._conn.execute(
                    "SELECT COUNT(*) c FROM usage_samples WHERE tool=?", (tool,))
            else:
                cur = self._conn.execute("SELECT COUNT(*) c FROM usage_samples")
            return cur.fetchone()["c"]

    def latest(self, tool: str) -> dict | None:
        with self._lock:
            cur = self._conn.execute(
                "SELECT * FROM usage_samples WHERE tool=? ORDER BY ts DESC, id DESC LIMIT 1",
                (tool,))
            row = cur.fetchone()
            return dict(row) if row else None

    def series(self, tool: str, metric: str, since_ts: float,
               until_ts: float | None = None) -> list[tuple[int, float]]:
        if metric not in _NUMERIC_METRICS:
            raise ValueError(f"unknown metric {metric!r}")
        with self._lock:
            if until_ts is None:
                cur = self._conn.execute(
                    f"SELECT ts, {metric} v FROM usage_samples "
                    f"WHERE tool=? AND ts>=? ORDER BY ts", (tool, int(since_ts)))
            else:
                cur = self._conn.execute(
                    f"SELECT ts, {metric} v FROM usage_samples "
                    f"WHERE tool=? AND ts>=? AND ts<=? ORDER BY ts",
                    (tool, int(since_ts), int(until_ts)))
            return [(r["ts"], r["v"]) for r in cur.fetchall()]

    def daily(self, tool: str, metric: str, days: int,
              now: float | None = None) -> dict[str, float]:
        """Per-UTC-day peak of `metric` over the last `days`."""
        if metric not in _NUMERIC_METRICS:
            raise ValueError(f"unknown metric {metric!r}")
        now = time.time() if now is None else now
        since = int(now - days * 86400)
        out: dict[str, float] = {}
        with self._lock:
            cur = self._conn.execute(
                f"SELECT ts, {metric} v FROM usage_samples WHERE tool=? AND ts>=?",
                (tool, since))
            for r in cur.fetchall():
                d = _day(r["ts"])
                v = r["v"] or 0.0
                if d not in out or v > out[d]:
                    out[d] = v
        return out

    def burn_rate(self, tool: str, window_s: float = 1800.0,
                  now: float | None = None) -> float:
        """USD/minute over the trailing window, from cumulative cost deltas."""
        now = time.time() if now is None else now
        since = int(now - window_s)
        with self._lock:
            cur = self._conn.execute(
                "SELECT ts, cost_usd FROM usage_samples WHERE tool=? AND ts>=? ORDER BY ts",
                (tool, since))
            rows = cur.fetchall()
        if len(rows) < 2:
            return 0.0
        dt = rows[-1]["ts"] - rows[0]["ts"]
        dc = rows[-1]["cost_usd"] - rows[0]["cost_usd"]
        if dt <= 0:
            return 0.0
        return max(0.0, dc / dt * 60.0)

    def prune(self, now: float | None = None) -> int:
        """Delete samples older than `retention_days`. Returns rows deleted."""
        now = time.time() if now is None else now
        cutoff = int(now - self.retention_days * 86400)
        with self._lock:
            cur = self._conn.execute(
                "DELETE FROM usage_samples WHERE ts < ?", (cutoff,))
            self._conn.commit()
            return cur.rowcount

    def close(self):
        try:
            self.flush()
        finally:
            with self._lock:
                self._conn.close()
