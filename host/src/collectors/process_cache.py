"""Shared running-process snapshot.

Each collector used to spawn its own ``pgrep`` on every poll (5 collectors ×
every 5s). This caches a single ``psutil`` process scan — run off the event
loop via a thread — and lets every collector query it:
  - ``any_running(*patterns)``: substring match against the full command line
    (mirrors ``pgrep -f``).
  - ``count_basename(name)``: count processes whose argv0 basename == name
    (e.g. how many ``claude`` CLI sessions are running).
"""

import asyncio
import os
import time

import psutil

# Each entry: (argv0_basename, full_cmdline)
_procs: list[tuple[str, str]] = []
_ts: float = 0.0
_TTL = 3.0
_lock = asyncio.Lock()


def _scan() -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    for p in psutil.process_iter(["name", "cmdline"]):
        try:
            cl = p.info.get("cmdline")
            if cl:
                full = " ".join(cl)
                base = os.path.basename(cl[0]) if cl[0] else (p.info.get("name") or "")
            else:
                full = p.info.get("name") or ""
                base = full
            out.append((base, full))
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue
    return out


async def _ensure_fresh() -> None:
    global _procs, _ts
    if time.monotonic() - _ts >= _TTL or not _procs:
        async with _lock:
            if time.monotonic() - _ts >= _TTL or not _procs:
                _procs = await asyncio.to_thread(_scan)
                _ts = time.monotonic()


async def any_running(*patterns: str) -> bool:
    """True if any process command line contains any of ``patterns`` (pgrep -f)."""
    await _ensure_fresh()
    return any(any(pat in full for pat in patterns) for _, full in _procs)


async def count_basename(name: str) -> int:
    """Count processes whose executable name (argv0 basename) == ``name``."""
    await _ensure_fresh()
    return sum(1 for base, _ in _procs if base == name)
