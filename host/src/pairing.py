"""Pairing flow management — generates tokens, validates codes."""

import hashlib
import hmac
import logging
import secrets
import time
from dataclasses import dataclass

from .device_registry import DeviceRegistry

logger = logging.getLogger("vibe-pi.pairing")


@dataclass
class PendingPair:
    device_id: str
    pair_code: str
    device_name: str
    timestamp: float


class PairingManager:
    def __init__(self, registry: DeviceRegistry, secret: str | None = None):
        self.registry = registry
        self._secret = secret or secrets.token_hex(32)
        self._pending: dict[str, PendingPair] = {}  # device_id → PendingPair
        self._timeout = 300  # 5 minutes

    def on_pair_request(self, device_id: str, pair_code: str, device_name: str) -> bool:
        self._pending[device_id] = PendingPair(
            device_id=device_id,
            pair_code=pair_code,
            device_name=device_name,
            timestamp=time.time(),
        )
        logger.info(f"Pairing request from {device_id} ({device_name}), code: {pair_code}")
        return True

    def confirm_pair(self, device_id: str, code: str) -> str | None:
        pending = self._pending.get(device_id)
        if not pending:
            logger.warning(f"No pending pair for {device_id}")
            return None

        if time.time() - pending.timestamp > self._timeout:
            del self._pending[device_id]
            logger.warning(f"Pair code expired for {device_id}")
            return None

        if pending.pair_code != code:
            logger.warning(f"Invalid pair code for {device_id}")
            return None

        token = self._generate_token(device_id)
        self.registry.pair_device(device_id, token)
        self.registry.rename_device(device_id, pending.device_name)
        del self._pending[device_id]

        logger.info(f"Pairing confirmed for {device_id}")
        return token

    def get_pending(self) -> list[PendingPair]:
        now = time.time()
        expired = [did for did, p in self._pending.items()
                   if now - p.timestamp > self._timeout]
        for did in expired:
            del self._pending[did]
        return list(self._pending.values())

    def _generate_token(self, device_id: str) -> str:
        msg = f"{device_id}:{time.time()}:{self._secret}"
        return hmac.new(self._secret.encode(), msg.encode(), hashlib.sha256).hexdigest()
