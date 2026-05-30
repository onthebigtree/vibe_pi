"""Persistent device registry — tracks paired devices, names, settings."""

import hmac
import json
import logging
import time
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any

logger = logging.getLogger("vibe-pi.registry")

REGISTRY_PATH = Path.home() / ".config" / "vibe-pi" / "devices.json"


@dataclass
class DeviceRecord:
    device_id: str
    firmware_version: str = ""
    hardware: str = ""
    mac: str = ""
    name: str = ""
    paired: bool = False
    pair_token: str = ""
    pair_token_expires: float = 0.0
    paired_at: float = 0.0
    last_seen: float = 0.0
    settings: dict = field(default_factory=dict)


class DeviceRegistry:
    def __init__(self, path: Path | None = None):
        self._path = path or REGISTRY_PATH
        self._devices: dict[str, DeviceRecord] = {}
        self._load()

    def _load(self):
        if not self._path.exists():
            return
        try:
            data = json.loads(self._path.read_text())
            for did, d in data.items():
                self._devices[did] = DeviceRecord(**d)
        except (json.JSONDecodeError, TypeError) as e:
            logger.warning(f"Failed to load registry: {e}")

    def _save(self):
        self._path.parent.mkdir(parents=True, exist_ok=True)
        data = {did: asdict(d) for did, d in self._devices.items()}
        self._path.write_text(json.dumps(data, indent=2, ensure_ascii=False))

    def get(self, device_id: str) -> DeviceRecord | None:
        return self._devices.get(device_id)

    def get_all(self) -> list[DeviceRecord]:
        return list(self._devices.values())

    def register_device(self, device_id: str, firmware_version: str = "",
                        hardware: str = "", mac: str = "") -> DeviceRecord:
        if device_id in self._devices:
            d = self._devices[device_id]
            d.firmware_version = firmware_version or d.firmware_version
            d.hardware = hardware or d.hardware
            d.mac = mac or d.mac
            d.last_seen = time.time()
        else:
            d = DeviceRecord(
                device_id=device_id,
                firmware_version=firmware_version,
                hardware=hardware,
                mac=mac,
                name=device_id,
                last_seen=time.time(),
            )
            self._devices[device_id] = d
        self._save()
        return d

    def pair_device(self, device_id: str, token: str, ttl_days: int = 30) -> bool:
        d = self._devices.get(device_id)
        if not d:
            return False
        d.paired = True
        d.pair_token = token
        d.pair_token_expires = time.time() + (ttl_days * 86400)
        d.paired_at = time.time()
        self._save()
        logger.info(f"Device paired: {device_id}")
        return True

    def unpair_device(self, device_id: str) -> bool:
        d = self._devices.get(device_id)
        if not d:
            return False
        d.paired = False
        d.pair_token = ""
        self._save()
        logger.info(f"Device unpaired: {device_id}")
        return True

    def rename_device(self, device_id: str, name: str) -> bool:
        d = self._devices.get(device_id)
        if not d:
            return False
        d.name = name
        self._save()
        return True

    def update_settings(self, device_id: str, settings: dict) -> bool:
        d = self._devices.get(device_id)
        if not d:
            return False
        d.settings.update(settings)
        self._save()
        return True

    def is_paired(self, device_id: str) -> bool:
        d = self._devices.get(device_id)
        return d.paired if d else False

    def verify_token(self, device_id: str, token: str) -> bool:
        d = self._devices.get(device_id)
        if not d or not d.paired:
            return False
        if d.pair_token_expires > 0 and time.time() > d.pair_token_expires:
            logger.info(f"Token expired for {device_id}")
            d.paired = False
            self._save()
            return False
        # Constant-time compare to avoid leaking the token via timing.
        return bool(d.pair_token) and hmac.compare_digest(d.pair_token, token)

    def remove_device(self, device_id: str) -> bool:
        if device_id in self._devices:
            del self._devices[device_id]
            self._save()
            return True
        return False
