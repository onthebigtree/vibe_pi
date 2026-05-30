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
| **Open Dashboard** | Opens `http://127.0.0.1:8765` in your browser (localhost, no token needed) |
| **Open Dashboard Window** | Opens the dashboard in a **native window** (no browser chrome) — see below |
| **Pair New Device…** | Opens the dashboard so you can confirm a device's 6-digit code |
| **Copy Remote Access Token** | Copies `~/.config/vibe-pi/dashboard_token` for LAN access |
| **Open Config Folder** | Reveals `~/.config/vibe-pi` in Finder |
| **Launch at Login** | Installs/removes a LaunchAgent so the agent auto-starts at login |

A login-launched agent is treated as external: the app adopts it for display and
will **not** kill it on Quit. It only tears down agents it started itself.

## Native main-window dashboard

**Open Dashboard Window** opens a native window titled **Vibe Pi** (~1040×740,
with a minimum size) that loads the local dashboard at `http://127.0.0.1:8765/`.
If the agent isn't reachable, the window shows a small inline fallback page
(calm palette: `#0B0D10` background / `#E8EAED` text) explaining that the agent
isn't running and how to start it (`vibe-pi-host run`).

### Why it's a separate process (the dual-run-loop trap)

`rumps` owns the macOS `NSApplication` run loop for the menu bar app, and
`pywebview`'s `webview.start()` **also** takes over the main thread to drive its
own run loop. Two run loops cannot coexist in one process — whichever starts
second never gets the main thread, so the app hangs or crashes.

To avoid this, the native window lives in its own module, `window.py`, and the
menu bar app launches it as a **separate child process** using the same
interpreter (`sys.executable`). The window then owns its run loop independently.

Process lifecycle:

- Clicking **Open Dashboard Window** spawns `window.py` once. A second click is
  a no-op while the window is still open — it only relaunches once that process
  has exited.
- Stopping the agent does **not** close the window (the window is independent of
  the agent; it just shows the fallback page if the agent is down).
- Quitting the menu bar app closes the window (it's ours) and tears down only an
  agent the app started itself.

Both `vibe_pi_menubar.py` and `window.py` are side-effect free at import time —
nothing starts a run loop or spawns a process until they're run as `__main__` —
so they stay smoke-test friendly.

## Files

| File | Role |
|---|---|
| `vibe_pi_menubar.py` | rumps menu bar app (main entry point) |
| `window.py` | Standalone pywebview native-window app (separate process) |
| `setup.py` | py2app build script (bundles **both** entry points) |

## Run (development)

```bash
# from the repo
cd host
pip install -e '.[macapp]'   # installs the host agent (vibe-pi-host) + rumps + pywebview

cd menubar
python vibe_pi_menubar.py
```

Run the native window on its own (handy for iterating on the window/fallback):

```bash
python menubar/window.py
```

Start the agent separately if you want the window to load the real dashboard:

```bash
vibe-pi-host run
```

## Build a standalone `.app`

```bash
cd host/menubar
pip install py2app rumps pywebview
python setup.py py2app
open "dist/Vibe Pi.app"
```

`setup.py` bundles both entry points: `vibe_pi_menubar.py` (the main script) and
`window.py` (shipped via `data_files` so it can be spawned as a child process
with `sys.executable`), and force-collects the `webview` and `rumps` packages.
The bundle is menu-bar-only (`LSUIElement` — no Dock icon). It expects the
`vibe-pi-host` console script to be available on `PATH`; if you ship a fully
self-contained bundle, vendor the host package into the app's `packages`.

## Notes

- The app talks to the agent over `http://127.0.0.1:8765` only; that endpoint is
  trusted for localhost without a token (same model as the web dashboard).
- Remote (LAN) dashboard access requires the token from **Copy Remote Access
  Token** — pass it as `?token=…` or the `X-Vibe-Token` header.
