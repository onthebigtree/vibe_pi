"""py2app build script for the Vibe Pi menu bar app.

    cd host/menubar
    pip install py2app rumps pywebview
    python setup.py py2app          # -> dist/Vibe Pi.app

For a quick local dev run you don't need this — just:
    python vibe_pi_menubar.py

The bundle ships TWO entry points: the menu bar app (vibe_pi_menubar.py) is the
main executable, and the native dashboard window (window.py) rides along as a
data file. The menu bar launches window.py as a separate process at runtime
(rumps and pywebview can't share one main run loop — see window.py).
"""

from setuptools import setup

APP = ["vibe_pi_menubar.py"]
# window.py is launched as its own process, so it must be present inside the
# bundle's Resources alongside the menu bar app.
DATA_FILES = ["window.py"]
OPTIONS = {
    "argv_emulation": False,
    "plist": {
        "CFBundleName": "Vibe Pi",
        "CFBundleDisplayName": "Vibe Pi",
        "CFBundleIdentifier": "com.vibepi.menubar",
        "CFBundleVersion": "0.2.3",
        "CFBundleShortVersionString": "0.2.3",
        # Menu-bar-only app: no Dock icon, no main window.
        "LSUIElement": True,
        "NSHumanReadableCopyright": "Vibe Pi",
    },
    # "webview" is the import package for pywebview (the native window backend).
    "packages": ["rumps", "webview"],
}

setup(
    app=APP,
    name="Vibe Pi",
    data_files=DATA_FILES,
    options={"py2app": OPTIONS},
    setup_requires=["py2app"],
)
