"""OTA firmware file management and version tracking.

Files are served by the unified aiohttp server at /firmware/ path.
This module handles publishing, manifest, and version queries only.
"""

import hashlib
import json
import logging
from dataclasses import dataclass
from pathlib import Path

logger = logging.getLogger("vibe-pi.ota")

OTA_DIR = Path.home() / ".config" / "vibe-pi" / "firmware"


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
        self._load_releases()

    def _load_releases(self):
        manifest = self.firmware_dir / "releases.json"
        if not manifest.exists():
            return
        try:
            data = json.loads(manifest.read_text())
            for r in data.get("releases", []):
                self._releases[r["version"]] = FirmwareRelease(**r)
        except (json.JSONDecodeError, KeyError) as e:
            logger.warning(f"Failed to load OTA manifest: {e}")

    def get_latest(self, channel: str = "stable") -> FirmwareRelease | None:
        candidates = [r for r in self._releases.values() if r.channel == channel]
        if not candidates:
            return None
        return max(candidates, key=lambda r: r.version)

    def get_all(self) -> list[FirmwareRelease]:
        return list(self._releases.values())

    def publish(self, firmware_path: Path, version: str, changelog: str = "",
                changelog_zh: str = "", channel: str = "stable", force: bool = False) -> FirmwareRelease:
        self.firmware_dir.mkdir(parents=True, exist_ok=True)

        dest = self.firmware_dir / f"vibepi-{version}.bin"
        if firmware_path != dest:
            import shutil
            shutil.copy2(firmware_path, dest)

        sha256 = hashlib.sha256(dest.read_bytes()).hexdigest()

        release = FirmwareRelease(
            version=version,
            filename=dest.name,
            size_bytes=dest.stat().st_size,
            sha256=sha256,
            changelog=changelog,
            changelog_zh=changelog_zh,
            channel=channel,
            force=force,
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
                }
                for r in self._releases.values()
            ]
        }
        manifest.write_text(json.dumps(data, indent=2))

    def get_download_url(self, release: FirmwareRelease, host_ip: str, port: int = 8765) -> str:
        return f"http://{host_ip}:{port}/firmware/{release.filename}"
