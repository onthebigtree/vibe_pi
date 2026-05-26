# Vibe Pi Communication Protocol v1

## Overview

Host Agent ↔ ESP32 Device communication over WebSocket (JSON).

All messages follow a common envelope:

```json
{
  "v": 1,
  "type": "<message_type>",
  "ts": 1716700000,
  "payload": { ... }
}
```

## Connection Lifecycle

```
Device                          Host
  |                               |
  |--- ws connect --------------->|
  |                               |
  |<-- hello ---------------------|  (server capabilities)
  |--- register ----------------->|  (device info)
  |<-- registered ----------------|  (config + ack)
  |                               |
  |<-- status --------------------|  (periodic, every 2s)
  |<-- status --------------------|
  |--- ping --------------------->|  (heartbeat, every 10s)
  |<-- pong ----------------------|
  |                               |
  |--- settings_update ---------->|  (device config change)
  |<-- settings_ack -------------|
  |                               |
  |<-- ota_available -------------|  (firmware update)
  |--- ota_accept --------------->|
  |<-- ota_chunk / ota_done ------|
  |                               |
```

## Message Types

### Host → Device

#### `hello`
Sent immediately after WebSocket connection.
```json
{
  "v": 1,
  "type": "hello",
  "ts": 1716700000,
  "payload": {
    "host_version": "0.1.0",
    "protocol_version": 1,
    "hostname": "macbook-pro.local",
    "collectors": ["claude_code", "codex", "gemini_cli", "system"]
  }
}
```

#### `registered`
Acknowledgement after device registration.
```json
{
  "v": 1,
  "type": "registered",
  "ts": 1716700000,
  "payload": {
    "device_id": "vibepi-a1b2c3",
    "config": {
      "poll_interval_ms": 2000,
      "brightness": 80,
      "theme": "minimal",
      "active_pages": ["overview", "claude_code", "codex", "system"]
    }
  }
}
```

#### `status`
Periodic status update (primary data message).
```json
{
  "v": 1,
  "type": "status",
  "ts": 1716700000,
  "payload": {
    "active_tool": "claude_code",
    "tools": {
      "claude_code": {
        "status": "active",
        "model": "opus-4",
        "tokens_used": 125000,
        "tokens_display": "125.0K",
        "cost_usd": 0.42,
        "cost_display": "$0.42",
        "usage_pct": 35,
        "session_count": 3,
        "current_task": "Refactoring auth module",
        "uptime_min": 45
      },
      "codex": {
        "status": "inactive",
        "model": "",
        "tokens_used": 0,
        "tokens_display": "0",
        "cost_usd": 0.0,
        "cost_display": "$0.00",
        "usage_pct": 0
      }
    },
    "system": {
      "cpu_pct": 45,
      "mem_pct": 62,
      "mem_used_gb": 12.8,
      "mem_total_gb": 32.0,
      "net_up_kbps": 120,
      "net_down_kbps": 450
    }
  }
}
```

#### `pong`
Heartbeat response.
```json
{ "v": 1, "type": "pong", "ts": 1716700000, "payload": {} }
```

#### `ota_available`
Firmware update notification.
```json
{
  "v": 1,
  "type": "ota_available",
  "ts": 1716700000,
  "payload": {
    "version": "1.2.0",
    "size_bytes": 1048576,
    "changelog": "Bug fixes and UI improvements",
    "url": "http://192.168.1.100:8766/firmware.bin"
  }
}
```

### Device → Host

#### `register`
Device identification after receiving hello.
```json
{
  "v": 1,
  "type": "register",
  "ts": 1716700000,
  "payload": {
    "device_id": "vibepi-a1b2c3",
    "firmware_version": "1.0.0",
    "hardware": "waveshare-esp32s3-amoled-175",
    "display": "466x466",
    "mac": "AA:BB:CC:DD:EE:FF"
  }
}
```

#### `ping`
Heartbeat (device sends every 10s).
```json
{ "v": 1, "type": "ping", "ts": 1716700000, "payload": {} }
```

#### `settings_update`
Device-initiated settings change (e.g. brightness from touch UI).
```json
{
  "v": 1,
  "type": "settings_update",
  "ts": 1716700000,
  "payload": {
    "brightness": 60,
    "current_page": "claude_code"
  }
}
```

## Error Handling

- If device doesn't receive `hello` within 5s of connecting → retry
- If no `status` received for 15s → show "Waiting for data..." on device
- If no `pong` received within 10s of `ping` → trigger reconnect
- Device reconnects with exponential backoff: 1s, 2s, 4s, 8s, max 30s

## Discovery

Host agent advertises via mDNS:
- Service: `_vibepi._tcp.local`
- Port: 8765
- TXT records: `version=0.1.0`, `protocol=1`

Device scans for `_vibepi._tcp.local` on boot to auto-discover host.
