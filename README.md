# Vibe Pi

ESP32-S3 圆形 AMOLED 硬件状态监视器，实时显示 Claude Code、Codex 等 AI 编程工具的运行状态。

## 硬件

- **开发板**: Waveshare ESP32-S3 1.75" AMOLED 开发板
- **芯片**: ESP32-S3R8 N16R8 (双核 LX7 @ 240MHz, 8MB PSRAM, 16MB Flash)
- **屏幕**: 1.75" 圆形 AMOLED, 466x466, CO5300 驱动 (QSPI)
- **触控**: CST9217 电容触摸 (I2C)
- **连接**: Wi-Fi 2.4GHz + Bluetooth 5 (LE)
- **其他**: QMI8658 六轴 IMU, PCF85063 RTC, 双麦克风, 可编程按键

## 架构

```
┌─────────────────┐     WebSocket      ┌──────────────────┐
│   Host Agent    │ ──────────────────> │   ESP32-S3       │
│   (Python)      │     Wi-Fi          │   AMOLED Display  │
│                 │                     │                   │
│ - Claude Code   │                     │ - Status UI       │
│ - Codex         │                     │ - Touch Control   │
│ - Gemini CLI    │                     │ - Auto Refresh    │
│ - Token Usage   │                     │                   │
│ - System Stats  │                     │                   │
└─────────────────┘                     └──────────────────┘
```

### Host Agent (`host/`)

运行在开发机上的 Python 服务，负责：
- 采集 Claude Code / Codex / Gemini CLI 等工具的运行状态
- 读取 token 用量、费用、请求历史
- 监控会话状态和活跃度
- 通过 WebSocket 将数据推送到 ESP32 设备

### Firmware (`firmware/`)

ESP32-S3 固件，基于 PlatformIO + Arduino + LVGL：
- 连接 Wi-Fi 并接收 WebSocket 推送
- 在 466x466 圆形 AMOLED 上渲染极简状态仪表盘
- 支持触摸交互切换显示页面
- 低功耗待机模式

## 显示内容

- **总览页**: 当前活跃工具、总 token 用量、费用趋势
- **Claude Code 页**: 会话状态、模型、token 消耗、当前任务
- **Codex 页**: 任务队列、运行进度
- **系统页**: CPU/内存、网络状态、API 延迟

## 快速开始

### Host Agent

```bash
cd host
python -m venv .venv
source .venv/bin/activate
pip install -e .
vibe-pi-host
```

### Firmware

需要 [PlatformIO](https://platformio.org/)：

```bash
cd firmware
pio run -t upload
```

## 开发

```
vibe_pi/
├── firmware/          # ESP32-S3 PlatformIO 项目
│   ├── platformio.ini
│   ├── src/
│   │   ├── main.cpp
│   │   ├── display/   # 屏幕驱动 & LVGL
│   │   ├── network/   # Wi-Fi & WebSocket
│   │   └── ui/        # UI 页面
│   └── lib/
├── host/              # 主机端 Python agent
│   ├── pyproject.toml
│   └── src/
│       ├── collectors/ # 各工具状态采集器
│       ├── server/     # WebSocket 服务
│       └── main.py
├── docs/
└── README.md
```

## License

MIT
