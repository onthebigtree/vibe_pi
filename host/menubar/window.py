"""Vibe Pi — native main-window dashboard (standalone pywebview app).

This is a SEPARATE process, not part of the menu bar app. The reason is a hard
main-thread conflict: rumps owns the NSApplication run loop, and pywebview's
`webview.start()` also blocks on the main thread — they cannot coexist in one
process. So the menu bar launches this script as its own subprocess (see
`vibe_pi_menubar.py`).

It simply opens a native window onto the dashboard the host already serves at
http://127.0.0.1:8765/. If the agent isn't reachable, it shows a small inline
fallback page (calm palette) explaining how to start it.

Run in dev:   python window.py

Requires:     pip install pywebview
"""

from __future__ import annotations

import urllib.request

import webview

API_PORT = 8765
API_BASE = f"http://127.0.0.1:{API_PORT}"

WINDOW_TITLE = "Vibe Pi"
WINDOW_WIDTH = 1040
WINDOW_HEIGHT = 740
MIN_WIDTH = 760
MIN_HEIGHT = 560

# Shown only when the agent isn't answering — calm palette, no external assets.
FALLBACK_HTML = """\
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Vibe Pi</title>
<style>
  html, body { height: 100%; margin: 0; }
  body {
    background: #0B0D10;
    color: #E8EAED;
    font: 15px/1.6 -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    display: flex;
    align-items: center;
    justify-content: center;
  }
  .card {
    max-width: 460px;
    padding: 40px;
    text-align: center;
  }
  h1 { font-size: 22px; font-weight: 600; margin: 0 0 12px; }
  p { color: #9AA0A6; margin: 0 0 18px; }
  code {
    display: inline-block;
    background: #16191D;
    color: #E8EAED;
    padding: 6px 12px;
    border-radius: 8px;
    font: 13px/1.4 ui-monospace, SFMono-Regular, Menlo, monospace;
  }
</style>
</head>
<body>
  <div class="card">
    <h1>The Vibe Pi agent isn't running</h1>
    <p>Start it from the menu bar (Start Agent), or from a terminal:</p>
    <p><code>vibe-pi-host run</code></p>
    <p>This window will need to be reopened once the agent is up.</p>
  </div>
</body>
</html>
"""


def _agent_reachable(timeout: float = 1.5) -> bool:
    """True if the localhost dashboard answers — same trust model as the menu bar."""
    try:
        with urllib.request.urlopen(f"{API_BASE}/api/status", timeout=timeout):
            return True
    except Exception:
        return False


def main():
    # Load the live dashboard if the agent is up; otherwise an inline explainer.
    if _agent_reachable():
        window_kwargs = {"url": API_BASE + "/"}
    else:
        window_kwargs = {"html": FALLBACK_HTML}

    webview.create_window(
        WINDOW_TITLE,
        width=WINDOW_WIDTH,
        height=WINDOW_HEIGHT,
        min_size=(MIN_WIDTH, MIN_HEIGHT),
        background_color="#0B0D10",
        **window_kwargs,
    )
    webview.start()


if __name__ == "__main__":
    main()
