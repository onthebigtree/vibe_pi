"""Vibe Pi communication protocol v2."""

import hashlib
import hmac
import secrets
import time
from enum import StrEnum
from typing import Any

PROTOCOL_VERSION = 2


class MsgType(StrEnum):
    HELLO = "hello"
    REGISTER = "register"
    REGISTERED = "registered"
    STATUS = "status"
    PING = "ping"
    PONG = "pong"
    # Pairing
    PAIR_REQUEST = "pair_request"
    PAIR_CONFIRM = "pair_confirm"
    PAIR_REJECT = "pair_reject"
    UNPAIR = "unpair"
    # Settings
    SETTINGS_UPDATE = "settings_update"
    SETTINGS_SYNC = "settings_sync"
    SETTINGS_ACK = "settings_ack"
    # OTA
    OTA_AVAILABLE = "ota_available"
    OTA_ACCEPT = "ota_accept"
    OTA_START = "ota_start"
    OTA_PROGRESS = "ota_progress"
    OTA_DONE = "ota_done"
    OTA_FAILED = "ota_failed"
    # Health
    HEALTH_REPORT = "health_report"
    # Device management
    RESET_COMMAND = "reset_command"
    DEVICE_RENAME = "device_rename"
    # Error
    ERROR = "error"
    # User interaction → host re-polls collectors immediately and pushes fresh status
    REFRESH_REQUEST = "refresh_request"


def _msg(msg_type: MsgType, payload: dict[str, Any] | None = None) -> dict:
    return {
        "v": PROTOCOL_VERSION,
        "type": msg_type.value,
        "ts": int(time.time()),
        "payload": payload or {},
    }


def parse_msg(data: dict) -> tuple[MsgType | None, dict]:
    try:
        msg_type = MsgType(data.get("type", ""))
        return msg_type, data.get("payload", {})
    except ValueError:
        return None, {}


# ── Host → Device ──

def make_hello(host_version: str, hostname: str, collectors: list[str],
               capabilities: list[str] | None = None) -> dict:
    return _msg(MsgType.HELLO, {
        "host_version": host_version,
        "protocol_version": PROTOCOL_VERSION,
        "hostname": hostname,
        "collectors": collectors,
        "capabilities": capabilities or ["ota", "pairing", "settings_sync", "health", "reset"],
    })


def make_registered(device_id: str, paired: bool, config: dict) -> dict:
    return _msg(MsgType.REGISTERED, {
        "device_id": device_id,
        "paired": paired,
        "config": config,
    })


def make_pair_confirm(device_id: str, token: str, host_name: str) -> dict:
    return _msg(MsgType.PAIR_CONFIRM, {
        "device_id": device_id,
        "token": token,
        "host_name": host_name,
    })


def make_pair_reject(reason: str = "invalid_code") -> dict:
    return _msg(MsgType.PAIR_REJECT, {"reason": reason})


def make_unpair(device_id: str) -> dict:
    return _msg(MsgType.UNPAIR, {"device_id": device_id})


def make_status(tools: dict, system: dict, active_tool: str) -> dict:
    return _msg(MsgType.STATUS, {
        "active_tool": active_tool,
        "tools": tools,
        "system": system,
    })


def make_status_compact(tools: dict, system: dict, active_tool: str) -> dict:
    """Compact status with ALL active tools so the device can cycle through them.

    Each tool gets the minimum fields needed to render its card. Target <500B
    with the 2KB ESP32 USB CDC RX buffer; fragments fine over multiple USB packets."""
    compact_tools = {}
    for name, d in tools.items():
        if d.get("status") != "active":
            continue
        compact_tools[name] = {
            "status": "active",
            "tokens_display": (d.get("tokens_display") or "")[:8],
            "cost_display": (d.get("cost_display") or "")[:8],
            "cost_rate_display": (d.get("cost_rate_display") or "")[:8],
            "model": (d.get("model") or "")[:24],
            "usage_pct": min(int(d.get("usage_pct") or 0), 999),
            # Task state + rolling 5h/7d usage windows (short keys to stay small).
            "task": (d.get("task_state") or "")[:8],
            "u5": (d.get("usage_5h_display") or "")[:8],
            "u7": (d.get("usage_7d_display") or "")[:8],
            "p5": min(int(d.get("usage_5h_pct") or 0), 100),  # numeric 5h % for inner ring
            "tasks": int(d.get("tasks") or 0),
        }
    compact_sys = {
        "cpu_pct": min(int(system.get("cpu_pct") or 0), 100),
        "mem_pct": min(int(system.get("mem_pct") or 0), 100),
    }
    return _msg(MsgType.STATUS, {
        "active_tool": active_tool,
        "tools": compact_tools,
        "system": compact_sys,
    })


def make_pong() -> dict:
    return _msg(MsgType.PONG)


def make_settings_sync(settings: dict) -> dict:
    return _msg(MsgType.SETTINGS_SYNC, settings)


def make_settings_ack(ok: bool = True) -> dict:
    return _msg(MsgType.SETTINGS_ACK, {"ok": ok})


def make_ota_available(version: str, current_version: str, size_bytes: int,
                       sha256: str, changelog: str, changelog_zh: str,
                       url: str, force: bool = False, channel: str = "stable",
                       signature: str = "") -> dict:
    return _msg(MsgType.OTA_AVAILABLE, {
        "version": version,
        "current_version": current_version,
        "size_bytes": size_bytes,
        "sha256": sha256,
        "signature": signature,
        "changelog": changelog,
        "changelog_zh": changelog_zh,
        "url": url,
        "force": force,
        "channel": channel,
    })


def make_ota_start(version: str, url: str, sha256: str, signature: str = "") -> dict:
    return _msg(MsgType.OTA_START, {
        "version": version, "url": url, "sha256": sha256, "signature": signature
    })


def make_reset_command(level: int, reason: str = "") -> dict:
    return _msg(MsgType.RESET_COMMAND, {"level": level, "reason": reason})


def make_device_rename(device_id: str, name: str) -> dict:
    return _msg(MsgType.DEVICE_RENAME, {"device_id": device_id, "name": name})


def make_error(code: str, message: str) -> dict:
    return _msg(MsgType.ERROR, {"code": code, "message": message})


# ── Security helpers ──

def generate_pair_token(device_id: str, pair_code: str, secret: str | None = None) -> str:
    secret = secret or secrets.token_hex(32)
    msg = f"{device_id}:{pair_code}:{secret}"
    return hmac.new(secret.encode(), msg.encode(), hashlib.sha256).hexdigest()


def generate_pair_code() -> str:
    return f"{secrets.randbelow(1000000):06d}"
