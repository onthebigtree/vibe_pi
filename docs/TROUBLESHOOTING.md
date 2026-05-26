# Troubleshooting

## Device won't connect to WiFi

**Symptoms**: Screen shows "WiFi Failed" or stays on "Connecting to WiFi..."

1. Ensure the WiFi is 2.4GHz (ESP32 doesn't support 5GHz)
2. Check that the password is correct
3. Hold the button for 5 seconds to factory reset, then reconfigure
4. Make sure the router isn't blocking new devices (MAC filtering)

## Device can't find host agent

**Symptoms**: Screen shows "Finding host..." indefinitely

1. Make sure `vibe-pi-host` is running on your computer
2. Both devices must be on the **same WiFi network**
3. Check if mDNS works: `dns-sd -B _vibepi._tcp` (macOS) or `avahi-browse -r _vibepi._tcp` (Linux)
4. If mDNS doesn't work on your network, you can set the host IP manually on the device (coming in a future firmware update)

## Host agent can't detect Claude Code

**Symptoms**: Dashboard shows "Idle" even when Claude Code is running

1. The collector checks for running processes with `pgrep -fl claude`
2. Verify: run `pgrep -fl claude` manually — it should return results
3. Usage data is read from `~/.claude/` — check that this directory exists
4. The collector also reads session files from `~/.claude/projects/`

## Display is black / not showing UI

**Symptoms**: ESP32 boots (serial output visible) but screen stays black

1. The CO5300 display driver may need pin configuration specific to your board revision
2. Check `firmware/include/config.h` for correct QSPI pin assignments
3. Verify with Waveshare's official example code first
4. Check serial monitor for any initialization errors

## WebSocket keeps disconnecting

**Symptoms**: Dashboard shows "Reconnecting..." frequently

1. Check WiFi signal strength at the device location
2. The ESP32 antenna is small — keep it within reasonable range of the router
3. Check for WiFi interference (microwave, other 2.4GHz devices)
4. The device auto-reconnects with exponential backoff (1s → 2s → 4s → ... → 30s)

## High CPU usage on host

**Symptoms**: Host agent using too much CPU

1. Increase polling interval in config: `polling.interval = 5.0`
2. Disable collectors you don't need in the config file
3. Run `vibe-pi-host --init-config` to create a config file, then edit it

## Factory Reset

Hold the programmable button for **5+ seconds**. The device will:
1. Clear all saved WiFi credentials
2. Clear host connection settings
3. Restart in setup mode (AP mode)
