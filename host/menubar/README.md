# Vibe Pi — macOS Menu Bar App

A lightweight menu bar controller for the Vibe Pi host agent. It does **not**
duplicate any logic — it starts/stops the existing `vibe-pi-host` agent and
reflects its state by polling the agent's own localhost API.

```
○            agent stopped
● 2          agent running, 2 AI tools currently active
```

## What the menu does

| Item | Action |
|---|---|
| **Start / Stop Agent** | Launches `vibe-pi-host run` as a child process (or stops it) |
| **Open Dashboard** | Opens `http://127.0.0.1:8765` (localhost, no token needed) |
| **Pair New Device…** | Opens the dashboard so you can confirm a device's 6-digit code |
| **Copy Remote Access Token** | Copies `~/.config/vibe-pi/dashboard_token` for LAN access |
| **Open Config Folder** | Reveals `~/.config/vibe-pi` in Finder |
| **Launch at Login** | Installs/removes a LaunchAgent so the agent auto-starts at login |

A login-launched agent is treated as external: the app adopts it for display and
will **not** kill it on Quit. It only tears down agents it started itself.

## Run (development)

```bash
# from the repo
cd host
pip install -e '.[macapp]'   # installs the host agent (provides `vibe-pi-host`) + rumps

cd menubar
python vibe_pi_menubar.py
```

## Build a standalone `.app`

```bash
cd host/menubar
pip install py2app rumps
python setup.py py2app
open "dist/Vibe Pi.app"
```

The bundle is menu-bar-only (`LSUIElement` — no Dock icon). It expects the
`vibe-pi-host` console script to be available on `PATH`; if you ship a fully
self-contained bundle, vendor the host package into the app's `packages`.

## Notes

- The app talks to the agent over `http://127.0.0.1:8765` only; that endpoint is
  trusted for localhost without a token (same model as the web dashboard).
- Remote (LAN) dashboard access requires the token from **Copy Remote Access
  Token** — pass it as `?token=…` or the `X-Vibe-Token` header.
