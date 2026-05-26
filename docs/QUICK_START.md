# Quick Start Guide

Get your Vibe Pi running in 5 minutes.

## What You Need

- Waveshare ESP32-S3 1.75" AMOLED dev board
- USB-C cable
- Computer running Claude Code, Codex, or other AI coding tools
- Same WiFi network for both devices

## Step 1: Flash the Firmware

Install [PlatformIO](https://platformio.org/install/cli):

```bash
pip install platformio
```

Connect the ESP32 via USB-C and flash:

```bash
cd firmware
pio run -t upload
```

## Step 2: Install the Host Agent

```bash
cd host
pip install -e .
```

Or with the optional web dashboard:

```bash
pip install -e '.[web]'
```

## Step 3: Start the Host Agent

```bash
vibe-pi-host
```

You should see:

```
12:00:00 [vibe-pi] INFO: Vibe Pi Host Agent v0.1.0
12:00:00 [vibe-pi] INFO: Active collectors: Claude Code, Codex, Gemini CLI, System
12:00:00 [vibe-pi] INFO: mDNS: advertising _vibepi._tcp on 192.168.1.100:8765
12:00:00 [vibe-pi] INFO: Web dashboard: http://192.168.1.100:8766
12:00:00 [vibe-pi] INFO: Waiting for ESP32 device to connect...
```

## Step 4: Configure the Device

When the ESP32 boots for the first time:

1. It creates a WiFi access point named **VibePi-XXXXXX**
2. Connect to this WiFi from your phone or laptop
3. A setup page opens automatically (or go to http://192.168.4.1)
4. Enter your WiFi network name and password
5. The device connects and auto-discovers the host agent via mDNS

That's it! The display should show the status dashboard within seconds.

## Using the Device

- **Swipe left/right**: Navigate between pages (Overview → Details → System)
- **Press button**: Cycle pages / wake from sleep
- **Hold button 5s**: Factory reset (clear WiFi settings)

## Pages

| Page | Content |
|------|---------|
| Overview | Active tool, model, token count, cost, usage arc |
| Details | Current task, session count, uptime, usage percentage |
| System | CPU/memory gauges, network throughput |

## Troubleshooting

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for common issues.
