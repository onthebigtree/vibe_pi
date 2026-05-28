"""Extended CLI with device management subcommands."""

import argparse
import asyncio
import sys
from pathlib import Path

from .config import AppConfig, init_config, load_config
from .device_registry import DeviceRegistry
from .ota_server import OTAManager

__version__ = "0.2.0"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="vibe-pi-host",
        description="Vibe Pi Host Agent — AI coding tool status monitor",
    )
    parser.add_argument("-v", "--version", action="version", version=f"%(prog)s {__version__}")
    parser.add_argument("-c", "--config", type=Path, help="Config file path (TOML)")
    parser.add_argument("--debug", action="store_true", help="Enable debug logging")

    sub = parser.add_subparsers(dest="command")

    # Default: run the agent
    sub.add_parser("run", help="Start the host agent (default)")

    # Init config
    sub.add_parser("init", help="Create default config file")

    # Device management
    dev = sub.add_parser("devices", help="Manage paired devices")
    dev_sub = dev.add_subparsers(dest="device_action")

    dev_sub.add_parser("list", help="List all known devices")

    rename = dev_sub.add_parser("rename", help="Rename a device")
    rename.add_argument("device_id", help="Device ID")
    rename.add_argument("name", help="New name")

    unpair = dev_sub.add_parser("unpair", help="Unpair a device")
    unpair.add_argument("device_id", help="Device ID")

    remove = dev_sub.add_parser("remove", help="Remove a device from registry")
    remove.add_argument("device_id", help="Device ID")

    # Pairing
    pair = sub.add_parser("pair", help="Confirm a pending pairing")
    pair.add_argument("device_id", help="Device ID")
    pair.add_argument("code", help="6-digit pairing code")

    # OTA
    ota = sub.add_parser("ota", help="Manage firmware updates")
    ota_sub = ota.add_subparsers(dest="ota_action")

    publish = ota_sub.add_parser("publish", help="Publish a firmware binary")
    publish.add_argument("firmware_path", type=Path, help="Path to .bin file")
    publish.add_argument("version", help="Version string (e.g. 1.2.0)")
    publish.add_argument("--changelog", default="", help="English changelog")
    publish.add_argument("--changelog-zh", default="", help="Chinese changelog")
    publish.add_argument("--channel", default="stable", help="Release channel")
    publish.add_argument("--force", action="store_true", help="Force update")

    ota_sub.add_parser("list", help="List published firmware versions")
    ota_sub.add_parser("keygen", help="Show OTA signing key (creates one if missing)")

    cert = sub.add_parser("cert", help="Manage WSS TLS certificates")
    cert_sub = cert.add_subparsers(dest="cert_action")
    cert_sub.add_parser("generate", help="Generate self-signed cert + key for WSS")
    cert_sub.add_parser("show", help="Show certificate fingerprint")

    return parser


def handle_devices(args):
    registry = DeviceRegistry()

    if args.device_action == "list" or not args.device_action:
        devices = registry.get_all()
        if not devices:
            print("No devices registered.")
            return
        for d in devices:
            status = "paired" if d.paired else "unpaired"
            print(f"  {d.device_id}  {d.name:<20s}  {status:<10s}  fw={d.firmware_version}  mac={d.mac}")

    elif args.device_action == "rename":
        if registry.rename_device(args.device_id, args.name):
            print(f"Renamed {args.device_id} → {args.name}")
        else:
            print(f"Device not found: {args.device_id}", file=sys.stderr)

    elif args.device_action == "unpair":
        if registry.unpair_device(args.device_id):
            print(f"Unpaired {args.device_id}")
        else:
            print(f"Device not found: {args.device_id}", file=sys.stderr)

    elif args.device_action == "remove":
        if registry.remove_device(args.device_id):
            print(f"Removed {args.device_id}")
        else:
            print(f"Device not found: {args.device_id}", file=sys.stderr)


def handle_ota(args):
    ota = OTAManager()

    if args.ota_action == "publish":
        release = ota.publish(
            args.firmware_path, args.version,
            changelog=args.changelog,
            changelog_zh=args.changelog_zh,
            channel=args.channel,
            force=args.force,
        )
        print(f"Published: v{release.version} ({release.size_bytes} bytes, sha256={release.sha256[:16]}...)")

    elif args.ota_action == "list" or not args.ota_action:
        for v, r in sorted(ota._releases.items()):
            sig = "signed" if r.signature else "unsigned"
            print(f"  v{r.version}  {r.size_bytes:>10} bytes  {r.channel}  {sig}  {'FORCE' if r.force else ''}")

    elif args.ota_action == "keygen":
        from .ota_server import get_or_create_signing_key, OTA_KEY_PATH
        key = get_or_create_signing_key()
        print(f"Signing key: {OTA_KEY_PATH}")
        print(f"Public key (paste into firmware/src/system/ota_pubkey.h as OTA_PUBKEY):")
        print(f'  #define OTA_PUBKEY "{key.hex()}"')


def handle_cert(args):
    from pathlib import Path
    import socket
    import subprocess
    cert_dir = Path.home() / ".config" / "vibe-pi" / "tls"
    cert_path = cert_dir / "server.crt"
    key_path = cert_dir / "server.key"

    if args.cert_action == "generate":
        cert_dir.mkdir(parents=True, exist_ok=True)
        hostname = socket.gethostname()
        # Generate self-signed cert with the hostname + localhost + 127.0.0.1 as SAN
        san = f"DNS:{hostname},DNS:localhost,IP:127.0.0.1"
        try:
            subprocess.run([
                "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                "-keyout", str(key_path), "-out", str(cert_path),
                "-days", "3650", "-subj", f"/CN=Vibe Pi Host ({hostname})",
                "-addext", f"subjectAltName={san}",
            ], check=True)
            key_path.chmod(0o600)
            print(f"Generated TLS cert: {cert_path}")
            print(f"Generated TLS key:  {key_path}")
            print(f"Update config.toml [server]: ssl_cert = \"{cert_path}\"")
            print(f"                              ssl_key = \"{key_path}\"")
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            print(f"openssl failed: {e}", file=sys.stderr)
            print("Install openssl: brew install openssl", file=sys.stderr)

    elif args.cert_action == "show":
        if not cert_path.exists():
            print(f"No cert at {cert_path}. Run: vibe-pi-host cert generate", file=sys.stderr)
            return
        result = subprocess.run(
            ["openssl", "x509", "-in", str(cert_path), "-fingerprint", "-sha256", "-noout"],
            capture_output=True, text=True)
        print(result.stdout)
