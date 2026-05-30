"""OTA firmware file management and version tracking.

Files are served by the unified aiohttp server at /firmware/ path.
This module handles publishing, manifest, and version queries only.
"""

import hashlib
import hmac
import json
import logging
import secrets
from dataclasses import dataclass
from pathlib import Path

logger = logging.getLogger("vibe-pi.ota")

OTA_DIR = Path.home() / ".config" / "vibe-pi" / "firmware"
OTA_KEY_PATH = Path.home() / ".config" / "vibe-pi" / "ota_signing_key.hex"


def get_or_create_signing_key() -> bytes:
    """Returns the 32-byte signing key, creating one on first call."""
    if OTA_KEY_PATH.exists():
        return bytes.fromhex(OTA_KEY_PATH.read_text().strip())
    OTA_KEY_PATH.parent.mkdir(parents=True, exist_ok=True)
    key = secrets.token_bytes(32)
    OTA_KEY_PATH.write_text(key.hex())
    OTA_KEY_PATH.chmod(0o600)
    logger.info(f"Generated OTA signing key at {OTA_KEY_PATH}")
    logger.info(f"Public key (paste into firmware/src/system/ota_pubkey.h):\n  {key.hex()}")
    return key


def sign_firmware(sha256_bytes: bytes) -> str:
    """HMAC-SHA256 signature of the SHA256 hash (matches firmware verify_signature)."""
    key = get_or_create_signing_key()
    return hmac.new(key, sha256_bytes, hashlib.sha256).hexdigest()


@dataclass
class FirmwareRelease:
    version: str
    filename: str
    size_bytes: int
    sha256: str
    changelog: str
    changelog_zh: str
    channel: str = "stable"
    force: bool = False
    signature: str = ""


class OTAManager:
    def __init__(self, firmware_dir: Path | None = None):
        self.firmware_dir = firmware_dir or OTA_DIR
        self._releases: dict[str, FirmwareRelease] = {}
        self._manifest_mtime = 0.0
        self._load_releases()

    def _manifest_path(self) -> Path:
        return self.firmware_dir / "releases.json"

    def _load_releases(self):
        manifest = self._manifest_path()
        if not manifest.exists():
            return
        try:
            self._manifest_mtime = manifest.stat().st_mtime
            data = json.loads(manifest.read_text())
            self._releases = {}   # rebuild so removed versions drop out on reload
            for r in data.get("releases", []):
                self._releases[r["version"]] = FirmwareRelease(**r)
        except (json.JSONDecodeError, KeyError, OSError) as e:
            logger.warning(f"Failed to load OTA manifest: {e}")

    def _reload_if_changed(self):
        # `vibe-pi-host ota publish` runs in a SEPARATE process and rewrites the
        # manifest. Re-read it when the file changes so a long-running host serves
        # fresh metadata (matching SHA) without a restart — otherwise it would
        # push a stale SHA and the device's verification would fail.
        try:
            m = self._manifest_path()
            if m.exists() and m.stat().st_mtime > self._manifest_mtime:
                self._load_releases()
        except OSError:
            pass

    def get_latest(self, channel: str = "stable") -> FirmwareRelease | None:
        self._reload_if_changed()
        candidates = [r for r in self._releases.values() if r.channel == channel]
        if not candidates:
            return None
        return max(candidates, key=lambda r: r.version)

    def get_all(self) -> list[FirmwareRelease]:
        self._reload_if_changed()
        return list(self._releases.values())

    def publish(self, firmware_path: Path, version: str, changelog: str = "",
                changelog_zh: str = "", channel: str = "stable", force: bool = False) -> FirmwareRelease:
        self.firmware_dir.mkdir(parents=True, exist_ok=True)

        dest = self.firmware_dir / f"vibepi-{version}.bin"
        if firmware_path != dest:
            import shutil
            shutil.copy2(firmware_path, dest)

        sha256_bytes = hashlib.sha256(dest.read_bytes()).digest()
        sha256 = sha256_bytes.hex()
        signature = sign_firmware(sha256_bytes)

        release = FirmwareRelease(
            version=version,
            filename=dest.name,
            size_bytes=dest.stat().st_size,
            sha256=sha256,
            changelog=changelog,
            changelog_zh=changelog_zh,
            channel=channel,
            force=force,
            signature=signature,
        )
        self._releases[version] = release
        self._save_manifest()
        logger.info(f"Published firmware {version} ({release.size_bytes} bytes, {channel})")
        return release

    def _save_manifest(self):
        self.firmware_dir.mkdir(parents=True, exist_ok=True)
        manifest = self.firmware_dir / "releases.json"
        data = {
            "releases": [
                {
                    "version": r.version,
                    "filename": r.filename,
                    "size_bytes": r.size_bytes,
                    "sha256": r.sha256,
                    "changelog": r.changelog,
                    "changelog_zh": r.changelog_zh,
                    "channel": r.channel,
                    "force": r.force,
                    "signature": r.signature,
                }
                for r in self._releases.values()
            ]
        }
        manifest.write_text(json.dumps(data, indent=2))
        try:
            self._manifest_mtime = manifest.stat().st_mtime
        except OSError:
            pass

    def get_download_url(self, release: FirmwareRelease, host_ip: str, port: int = 8765) -> str:
        return f"http://{host_ip}:{port}/firmware/{release.filename}"
