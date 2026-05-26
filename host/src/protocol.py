"""Vibe Pi communication protocol v1 — message construction and parsing."""

import time
from enum import StrEnum
from typing import Any

PROTOCOL_VERSION = 1


class MsgType(StrEnum):
    HELLO = "hello"
    REGISTER = "register"
    REGISTERED = "registered"
    STATUS = "status"
    PING = "ping"
    PONG = "pong"
    SETTINGS_UPDATE = "settings_update"
    SETTINGS_ACK = "settings_ack"
    OTA_AVAILABLE = "ota_available"
    OTA_ACCEPT = "ota_accept"
    ERROR = "error"


def make_msg(msg_type: MsgType, payload: dict[str, Any] | None = None) -> dict:
    return {
        "v": PROTOCOL_VERSION,
        "type": msg_type.value,
        "ts": int(time.time()),
        "payload": payload or {},
    }


def make_hello(host_version: str, hostname: str, collectors: list[str]) -> dict:
    return make_msg(MsgType.HELLO, {
        "host_version": host_version,
        "protocol_version": PROTOCOL_VERSION,
        "hostname": hostname,
        "collectors": collectors,
    })


def make_registered(device_id: str, config: dict) -> dict:
    return make_msg(MsgType.REGISTERED, {
        "device_id": device_id,
        "config": config,
    })


def make_status(tools: dict[str, Any], system: dict[str, Any], active_tool: str) -> dict:
    return make_msg(MsgType.STATUS, {
        "active_tool": active_tool,
        "tools": tools,
        "system": system,
    })


def make_pong() -> dict:
    return make_msg(MsgType.PONG)


def make_ota_available(version: str, size_bytes: int, changelog: str, url: str) -> dict:
    return make_msg(MsgType.OTA_AVAILABLE, {
        "version": version,
        "size_bytes": size_bytes,
        "changelog": changelog,
        "url": url,
    })


def make_error(code: str, message: str) -> dict:
    return make_msg(MsgType.ERROR, {"code": code, "message": message})


def parse_msg(data: dict) -> tuple[MsgType | None, dict]:
    try:
        msg_type = MsgType(data.get("type", ""))
        payload = data.get("payload", {})
        return msg_type, payload
    except ValueError:
        return None, {}
