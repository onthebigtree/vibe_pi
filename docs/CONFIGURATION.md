# Configuration Guide

## Host Agent Configuration

Create a config file:

```bash
vibe-pi-host --init-config
```

This creates `~/.config/vibe-pi/config.toml`. You can also place a `vibe-pi.toml` in your working directory, or set `VIBEPI_CONFIG=/path/to/config.toml`.

### Config File Reference

```toml
# Server settings
[server]
host = "0.0.0.0"      # Bind address
port = 8765            # WebSocket port
mdns = true            # Enable mDNS auto-discovery

# Data collection
[polling]
interval = 2.0         # Seconds between status updates

# Toggle individual collectors
[collectors]
claude_code = true
codex = true
gemini_cli = true
system = true

# Claude Code specific settings
[collectors.claude_code]
daily_budget = 5.0     # USD — used to calculate usage percentage

# Default display settings pushed to device
[display]
brightness = 80        # 0-100
theme = "minimal"      # Theme name
pages = ["overview", "claude_code", "codex", "system"]

# Logging
[logging]
level = "INFO"         # DEBUG, INFO, WARNING, ERROR
file = ""              # Log file path (empty = stdout only)
```

### CLI Options

```
vibe-pi-host [options]

Options:
  -v, --version       Show version
  -c, --config PATH   Config file path
  -p, --port PORT     Override WebSocket port
  --init-config       Create default config and exit
  --debug             Enable debug logging
```

### Environment Variables

| Variable | Description |
|----------|-------------|
| `VIBEPI_CONFIG` | Path to config file |

## Device Configuration

Device settings are stored in NVS (non-volatile storage) and can be changed via:

1. **Captive portal** (first-run setup): WiFi credentials
2. **Touch UI** (coming soon): Brightness, theme
3. **Host push**: The host agent sends default display config on device registration

### Factory Reset

Hold the programmable button for 5+ seconds to clear all device settings.

## Web Dashboard

When the host agent is running, a web dashboard is available at `http://<host-ip>:8766`.

Install the web dependency:

```bash
pip install 'vibe-pi-host[web]'
```

The dashboard shows:
- Real-time status of all tracked AI tools
- Connected device list
- System resource metrics
- No login required (local network only)
