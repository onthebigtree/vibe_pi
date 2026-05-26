# Vibe Pi

ESP32-S3 圆形 AMOLED 状态监视器，显示 Claude Code / Codex / Gemini CLI 等 AI 工具运行状态。

## 硬件

- Waveshare ESP32-S3 1.75" AMOLED (N16R8)
- 466x466 圆形 AMOLED, CO5300 驱动 (QSPI), CST9217 触摸 (I2C)
- Wi-Fi + BLE 5

## 项目结构

- `firmware/` — ESP32 PlatformIO (Arduino + LVGL 9)
- `host/` — Python host agent (websockets + psutil + zeroconf)

## 开发命令

### Firmware

```bash
cd firmware && pio run                # 编译
cd firmware && pio run -t upload      # 烧录
cd firmware && pio device monitor     # 串口
```

### Host

```bash
cd host
pip install -e '.[web,dev]'
vibe-pi-host                          # 启动
vibe-pi-host --debug                  # 调试模式
vibe-pi-host --init-config            # 生成配置文件
```

## 通信协议

见 `protocol.md`。Host→Device WebSocket JSON:

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
        "tokens_display": "125.0K",
        "cost_display": "$0.42",
        "usage_pct": 35,
        "current_task": "Refactoring auth",
        "session_count": 3,
        "uptime_min": 45
      }
    },
    "system": { "cpu_pct": 45, "mem_pct": 62, "net_up_kbps": 120, "net_down_kbps": 450 }
  }
}
```

## 关键设计

- **Firmware state machine**: BOOT → PROVISION → CONNECTING_WIFI → DISCOVERING → CONNECTING_WS → RUNNING
- **Auto-discovery**: mDNS `_vibepi._tcp` — host advertises, device discovers
- **Reconnection**: exponential backoff 1s → 30s, falls back to mDNS re-discovery after 30s
- **Config**: NVS on device, TOML on host (`~/.config/vibe-pi/config.toml`)
- **UI**: LVGL tileview with 3 pages (overview / detail / system), swipe + button navigation
- **Display driver**: CO5300 QSPI — TODO stubs, reference Waveshare example code
- **Touch driver**: CST9217 I2C — TODO stubs
