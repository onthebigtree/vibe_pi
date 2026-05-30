"""Vibe Pi — macOS menu bar app.

A thin controller around the existing `vibe-pi-host` agent. It does NOT
reimplement collection/pairing/OTA — it starts the agent as a child process and
reflects its state by polling the agent's own localhost API (no token needed
from localhost, same trust model as the dashboard).

Run in dev:    python vibe_pi_menubar.py
Bundle (.app): python setup.py py2app   (see README.md)

Requires:      pip install rumps   (and the host installed: pip install -e ..)
"""

from __future__ import annotations

import json
import os
import plistlib
import shutil
import subprocess
import urllib.request
import webbrowser
from pathlib import Path

import rumps

# ── Where the agent keeps its state (mirrors host/src/config.py) ──
CONFIG_DIR = Path.home() / ".config" / "vibe-pi"
TOKEN_PATH = CONFIG_DIR / "dashboard_token"
API_PORT = 8765
API_BASE = f"http://127.0.0.1:{API_PORT}"

LAUNCH_AGENT_LABEL = "com.vibepi.host"
LAUNCH_AGENT_PATH = Path.home() / "Library" / "LaunchAgents" / f"{LAUNCH_AGENT_LABEL}.plist"

POLL_SECONDS = 3.0


def _api(path: str, timeout: float = 1.5):
    """GET a localhost API endpoint, returning parsed JSON or None."""
    try:
        with urllib.request.urlopen(f"{API_BASE}{path}", timeout=timeout) as r:
            return json.loads(r.read().decode())
    except Exception:
        return None


def _agent_command() -> list[str]:
    """Prefer the installed console script; fall back to `python -m src.cli`."""
    exe = shutil.which("vibe-pi-host")
    if exe:
        return [exe, "run"]
    # Fall back to running the package from the host/ directory (dev checkout).
    import sys
    host_dir = Path(__file__).resolve().parent.parent
    return [sys.executable, "-m", "src.cli", "run"]


class VibePiMenuBar(rumps.App):
    def __init__(self):
        super().__init__("Vibe Pi", title="○", quit_button=None)

        self.proc: subprocess.Popen | None = None
        self.online = False
        self.device_count = 0

        # Menu — most titles are updated live by _refresh().
        self.item_status = rumps.MenuItem("Stopped")
        self.item_devices = rumps.MenuItem("Devices: —")
        self.item_toggle = rumps.MenuItem("Start Agent", callback=self.toggle_agent)
        self.item_dashboard = rumps.MenuItem("Open Dashboard", callback=self.open_dashboard)
        self.item_pair = rumps.MenuItem("Pair New Device…", callback=self.pair_device)
        self.item_token = rumps.MenuItem("Copy Remote Access Token", callback=self.copy_token)
        self.item_folder = rumps.MenuItem("Open Config Folder", callback=self.open_folder)
        self.item_login = rumps.MenuItem("Launch at Login", callback=self.toggle_login)
        self.item_about = rumps.MenuItem("About Vibe Pi", callback=self.about)

        self.menu = [
            self.item_status,
            self.item_devices,
            None,
            self.item_toggle,
            self.item_dashboard,
            self.item_pair,
            None,
            self.item_token,
            self.item_folder,
            self.item_login,
            None,
            self.item_about,
            rumps.MenuItem("Quit Vibe Pi", callback=self.quit_app),
        ]

        self.item_login.state = LAUNCH_AGENT_PATH.exists()

        # If an agent is already running (e.g. started at login), adopt it.
        self.online = _api("/api/health") is not None
        self.timer = rumps.Timer(self.tick, POLL_SECONDS)
        self.timer.start()

    # ── State polling ──────────────────────────────────────────────
    def tick(self, _):
        self._refresh()

    def _agent_alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def _refresh(self):
        # /api/status → {"connected_devices": N, "registered_devices": M}
        # /api/devices → {"connected": [...], "registered": [{...,"paired":bool}]}
        status = _api("/api/status")
        self.online = status is not None

        if self.online:
            connected = status.get("connected_devices", 0)
            devices = _api("/api/devices") or {}
            registered = devices.get("registered", []) if isinstance(devices, dict) else []
            paired = sum(1 for d in registered if d.get("paired"))
            self.device_count = paired

            self.title = f"● {paired}" if paired else "●"
            self.item_status.title = "Running"
            self.item_devices.title = (
                f"Devices: {paired} paired" + (f" · {connected} live" if connected else "")
            )
            self.item_toggle.title = "Stop Agent"
        else:
            self.title = "○"
            self.item_status.title = "Stopped"
            self.item_devices.title = "Devices: —"
            self.item_toggle.title = "Start Agent"

    # ── Actions ────────────────────────────────────────────────────
    def toggle_agent(self, _):
        if self.online or self._agent_alive():
            self.stop_agent()
        else:
            self.start_agent()
        self._refresh()

    def start_agent(self):
        if self._agent_alive() or _api("/api/health") is not None:
            return  # already running (ours or a login-started one)
        try:
            host_dir = Path(__file__).resolve().parent.parent
            self.proc = subprocess.Popen(
                _agent_command(),
                cwd=str(host_dir),
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except Exception as e:
            rumps.alert("Vibe Pi", f"Failed to start agent:\n{e}")

    def stop_agent(self):
        # Stop only the process we started; a login-launched agent is left alone.
        if self._agent_alive():
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.proc = None

    def open_dashboard(self, _):
        if not self.online:
            rumps.alert("Vibe Pi", "Agent isn't running. Start it first.")
            return
        webbrowser.open(API_BASE + "/")

    def pair_device(self, _):
        if not self.online:
            rumps.alert("Vibe Pi", "Start the agent before pairing.")
            return
        # Pairing is confirmed from the dashboard's Devices section.
        webbrowser.open(API_BASE + "/")
        rumps.notification("Vibe Pi", "Pairing",
                           "Enter the 6-digit code shown on the device, then confirm in the dashboard.")

    def copy_token(self, _):
        if not TOKEN_PATH.exists():
            rumps.alert("Vibe Pi",
                        "No remote token yet. Start the agent once to generate it.")
            return
        token = TOKEN_PATH.read_text().strip()
        self._set_clipboard(token)
        rumps.notification("Vibe Pi", "Remote access token copied",
                           "Use it to reach the dashboard from another machine on your network.")

    def open_folder(self, _):
        CONFIG_DIR.mkdir(parents=True, exist_ok=True)
        subprocess.run(["open", str(CONFIG_DIR)])

    def toggle_login(self, _):
        if LAUNCH_AGENT_PATH.exists():
            self._uninstall_login_item()
            self.item_login.state = False
            rumps.notification("Vibe Pi", "Launch at Login disabled", "")
        else:
            self._install_login_item()
            self.item_login.state = True
            rumps.notification("Vibe Pi", "Launch at Login enabled",
                               "The agent will start automatically when you log in.")

    def about(self, _):
        rumps.alert(
            "Vibe Pi",
            "Vibe Pi desktop companion\n\n"
            "Runs the local host agent that collects your AI coding-tool usage "
            "and streams it to your Vibe Pi display.\n\n"
            "The menu bar shows agent state and paired device count; the dashboard "
            "(localhost) has full device, settings and OTA controls.",
        )

    def quit_app(self, _):
        # Leave a login-launched agent running; only tear down what we started.
        self.stop_agent()
        rumps.quit_application()

    # ── Helpers ────────────────────────────────────────────────────
    @staticmethod
    def _set_clipboard(text: str):
        p = subprocess.Popen(["pbcopy"], stdin=subprocess.PIPE)
        p.communicate(text.encode())

    def _install_login_item(self):
        LAUNCH_AGENT_PATH.parent.mkdir(parents=True, exist_ok=True)
        plist = {
            "Label": LAUNCH_AGENT_LABEL,
            "ProgramArguments": _agent_command(),
            "RunAtLoad": True,
            "KeepAlive": True,
            "WorkingDirectory": str(Path(__file__).resolve().parent.parent),
            "StandardOutPath": str(CONFIG_DIR / "agent.log"),
            "StandardErrorPath": str(CONFIG_DIR / "agent.err.log"),
        }
        with open(LAUNCH_AGENT_PATH, "wb") as f:
            plistlib.dump(plist, f)
        subprocess.run(["launchctl", "load", str(LAUNCH_AGENT_PATH)],
                       stderr=subprocess.DEVNULL)

    def _uninstall_login_item(self):
        subprocess.run(["launchctl", "unload", str(LAUNCH_AGENT_PATH)],
                       stderr=subprocess.DEVNULL)
        LAUNCH_AGENT_PATH.unlink(missing_ok=True)


if __name__ == "__main__":
    VibePiMenuBar().run()
