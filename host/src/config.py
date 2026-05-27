"""Configuration management with TOML file support."""

import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

try:
    import tomllib
except ImportError:
    import tomli as tomllib  # type: ignore[no-redef]


DEFAULT_CONFIG_PATH = Path.home() / ".config" / "vibe-pi" / "config.toml"

DEFAULT_CONFIG_TOML = """\
# Vibe Pi Host Agent Configuration

[server]
host = "0.0.0.0"
port = 8765
# Enable mDNS advertisement for auto-discovery
mdns = true

[polling]
# Status collection interval in seconds
interval = 2.0

[collectors]
# Enable/disable individual collectors
claude_code = true
codex = true
gemini_cli = true
system = true

[collectors.claude_code]
# Daily budget for usage percentage calculation (USD)
daily_budget = 5.0

[display]
# Default brightness (0-100)
brightness = 80
# Default theme
theme = "minimal"
# Active pages shown on device
pages = ["overview", "claude_code", "codex", "system"]

[logging]
level = "INFO"
# Log file path (empty = stdout only)
file = ""
"""


@dataclass
class ServerConfig:
    host: str = "0.0.0.0"
    port: int = 8765
    mdns: bool = True
    ssl_cert: str = ""
    ssl_key: str = ""


@dataclass
class PollingConfig:
    interval: float = 2.0


@dataclass
class CollectorFlags:
    claude_code: bool = True
    codex: bool = True
    gemini_cli: bool = True
    cursor: bool = True
    windsurf: bool = True
    system: bool = True


@dataclass
class ClaudeCodeConfig:
    daily_budget: float = 5.0


@dataclass
class DisplayConfig:
    brightness: int = 80
    theme: str = "minimal"
    pages: list[str] = field(default_factory=lambda: ["overview", "claude_code", "codex", "system"])


@dataclass
class LoggingConfig:
    level: str = "INFO"
    file: str = ""


@dataclass
class AppConfig:
    server: ServerConfig = field(default_factory=ServerConfig)
    polling: PollingConfig = field(default_factory=PollingConfig)
    collectors: CollectorFlags = field(default_factory=CollectorFlags)
    claude_code: ClaudeCodeConfig = field(default_factory=ClaudeCodeConfig)
    display: DisplayConfig = field(default_factory=DisplayConfig)
    logging: LoggingConfig = field(default_factory=LoggingConfig)


def load_config(path: Path | None = None) -> AppConfig:
    path = path or _find_config_path()
    if path and path.exists():
        return _parse_config(path)
    return AppConfig()


def init_config(path: Path | None = None) -> Path:
    path = path or DEFAULT_CONFIG_PATH
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        path.write_text(DEFAULT_CONFIG_TOML)
    return path


def _find_config_path() -> Path | None:
    candidates = [
        Path(os.environ.get("VIBEPI_CONFIG", "")),
        Path.cwd() / "vibe-pi.toml",
        DEFAULT_CONFIG_PATH,
    ]
    for p in candidates:
        if p and p.exists():
            return p
    return None


def _parse_config(path: Path) -> AppConfig:
    data = tomllib.loads(path.read_text())

    cfg = AppConfig()

    if s := data.get("server"):
        cfg.server = ServerConfig(
            host=s.get("host", cfg.server.host),
            port=s.get("port", cfg.server.port),
            mdns=s.get("mdns", cfg.server.mdns),
        )

    if p := data.get("polling"):
        cfg.polling = PollingConfig(interval=p.get("interval", cfg.polling.interval))

    if c := data.get("collectors"):
        cfg.collectors = CollectorFlags(
            claude_code=c.get("claude_code", True),
            codex=c.get("codex", True),
            gemini_cli=c.get("gemini_cli", True),
            system=c.get("system", True),
        )
        if cc := c.get("claude_code_config") or c.get("claude_code"):
            if isinstance(cc, dict):
                cfg.claude_code = ClaudeCodeConfig(daily_budget=cc.get("daily_budget", 5.0))

    if d := data.get("display"):
        cfg.display = DisplayConfig(
            brightness=d.get("brightness", cfg.display.brightness),
            theme=d.get("theme", cfg.display.theme),
            pages=d.get("pages", cfg.display.pages),
        )

    if lg := data.get("logging"):
        cfg.logging = LoggingConfig(
            level=lg.get("level", cfg.logging.level),
            file=lg.get("file", cfg.logging.file),
        )

    return cfg
