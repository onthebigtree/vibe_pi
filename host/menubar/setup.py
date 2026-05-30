"""py2app build script for the Vibe Pi menu bar app.

    cd host/menubar
    pip install py2app rumps
    python setup.py py2app          # -> dist/Vibe Pi.app

For a quick local dev run you don't need this — just:
    python vibe_pi_menubar.py
"""

from setuptools import setup

APP = ["vibe_pi_menubar.py"]
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
    "packages": ["rumps"],
}

setup(
    app=APP,
    name="Vibe Pi",
    options={"py2app": OPTIONS},
    setup_requires=["py2app"],
)
