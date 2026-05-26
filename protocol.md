# Vibe Pi Communication Protocol v2

## Envelope

```json
{ "v": 2, "type": "<msg_type>", "ts": 1716700000, "payload": { ... } }
```

## Connection Lifecycle

```
Device                              Host
  |─── ws connect ──────────────────>|
  |<── hello ────────────────────────|
  |─── register ────────────────────>|
  |<── registered ───────────────────|   (if already paired)
  |                                  |
  |  ┌─ PAIRING (first time only) ─┐|
  |  │ device shows 6-digit code    │|
  |─── pair_request ────────────────>|
  |  │ user enters code on host     │|
  |<── pair_confirm / pair_reject ──│|
  |  └─────────────────────────────┘|
  |                                  |
  |<── settings_sync ────────────────|   (full settings push)
  |─── settings_ack ────────────────>|
  |                                  |
  |<── status ───────────────────────|   (periodic)
  |─── ping ────────────────────────>|
  |<── pong ─────────────────────────|
  |─── health_report ───────────────>|   (periodic)
  |                                  |
  |─── settings_update ─────────────>|   (device-side change)
  |<── settings_ack ─────────────────|
  |                                  |
  |<── ota_available ────────────────|
  |─── ota_accept ──────────────────>|
  |<── ota_start ────────────────────|
  |    (device downloads via HTTP)   |
  |─── ota_progress ────────────────>|
  |─── ota_done / ota_failed ───────>|
  |                                  |
  |<── reset_command ────────────────|
  |<── device_rename ────────────────|
  |<── unpair ───────────────────────|
```

---

## Message Types — Host → Device

### `hello`
```json
{
  "host_version": "0.2.0",
  "protocol_version": 2,
  "hostname": "macbook.local",
  "collectors": ["claude_code", "codex", "gemini_cli", "system"],
  "capabilities": ["ota", "pairing", "settings_sync", "health", "reset"]
}
```

### `registered`
```json
{
  "device_id": "vibepi-a1b2c3",
  "paired": true,
  "config": {
    "poll_interval_ms": 2000,
    "brightness": 80,
    "theme": "minimal",
    "active_pages": ["overview", "claude_code", "codex", "system"],
    "language": "zh",
    "sleep_timeout_ms": 60000,
    "ota_channel": "stable"
  }
}
```

### `pair_confirm`
```json
{ "device_id": "vibepi-a1b2c3", "token": "hmac-sha256-token-here", "host_name": "My MacBook" }
```

### `pair_reject`
```json
{ "reason": "invalid_code" }
```

### `status`
```json
{
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
```

### `pong`
```json
{}
```

### `settings_sync`
Full settings push from host to device.
```json
{
  "brightness": 80,
  "sleep_timeout_ms": 60000,
  "theme": "minimal",
  "language": "zh",
  "active_pages": ["overview", "claude_code", "codex", "system"],
  "alert_usage_pct": 80,
  "alert_disconnect": true,
  "ota_channel": "stable",
  "device_name": "My Vibe Pi",
  "timezone": "Asia/Shanghai"
}
```

### `settings_ack`
```json
{ "ok": true }
```

### `ota_available`
```json
{
  "version": "1.2.0",
  "current_version": "1.1.0",
  "size_bytes": 1048576,
  "sha256": "abcdef1234567890...",
  "changelog": "Bug fixes and new features",
  "changelog_zh": "修复问题并新增功能",
  "url": "http://192.168.1.100:8767/firmware/vibepi-1.2.0.bin",
  "force": false,
  "channel": "stable"
}
```

### `ota_start`
```json
{ "version": "1.2.0", "url": "http://...", "sha256": "..." }
```

### `reset_command`
```json
{ "level": 2, "reason": "User requested network reset" }
```
Levels: 0=soft restart, 1=display reset, 2=network reset, 3=factory reset

### `device_rename`
```json
{ "device_id": "vibepi-a1b2c3", "name": "Office Monitor" }
```

### `unpair`
```json
{ "device_id": "vibepi-a1b2c3" }
```

---

## Message Types — Device → Host

### `register`
```json
{
  "device_id": "vibepi-a1b2c3",
  "firmware_version": "1.0.0",
  "hardware": "waveshare-esp32s3-amoled-175",
  "display": "466x466",
  "mac": "AA:BB:CC:DD:EE:FF",
  "paired_token": "hmac-token-or-empty",
  "language": "zh",
  "capabilities": ["touch", "imu", "mic", "rtc"]
}
```

### `pair_request`
```json
{ "device_id": "vibepi-a1b2c3", "pair_code": "482916", "device_name": "Vibe Pi" }
```

### `ping`
```json
{}
```

### `settings_update`
```json
{ "brightness": 60, "current_page": "system" }
```

### `settings_ack`
```json
{ "ok": true }
```

### `health_report`
```json
{
  "device_id": "vibepi-a1b2c3",
  "uptime_sec": 3600,
  "free_heap": 245760,
  "wifi_rssi": -45,
  "fps": 30,
  "temperature_c": 42.5,
  "crash_count": 0,
  "last_crash_reason": "",
  "errors": ["TOUCH_INIT_FAIL"],
  "safe_mode": false
}
```

### `ota_accept`
```json
{ "version": "1.2.0" }
```

### `ota_progress`
```json
{ "version": "1.2.0", "progress_pct": 45, "bytes_received": 471859 }
```

### `ota_done`
```json
{ "version": "1.2.0", "success": true, "sha256_ok": true }
```

### `ota_failed`
```json
{ "version": "1.2.0", "error": "sha256_mismatch" }
```

---

## Error Handling

| Scenario | Behavior |
|----------|----------|
| No `hello` within 5s | Device retries connection |
| No `status` for 15s | Device shows "Waiting for data..." |
| No `pong` within 10s | Device triggers reconnect |
| Reconnect backoff | 1s → 2s → 4s → 8s → 16s → 30s max |
| 3 consecutive boot crashes | Enter safe mode (minimal UI, no OTA) |
| OTA SHA256 mismatch | Abort, send `ota_failed`, keep current firmware |
| OTA boot failure (3x) | Auto-rollback to previous partition |
| Invalid settings value | Silently revert to default, log warning |
| Unpaired device connects | Host sends `registered` with `paired: false`, device enters pairing flow |

## Discovery

- mDNS service: `_vibepi._tcp.local`
- TXT records: `version`, `protocol`, `hostname`, `pairing_enabled`
