# Vibe Pi

ESP32-S3 圆形 AMOLED 硬件状态监视器，实时显示 Claude Code、Codex、Gemini CLI 等 AI 编程工具的运行状态。

## 功能

- **实时监控**: Claude Code / Codex / Gemini CLI 运行状态、token 用量、费用
- **系统资源**: CPU、内存、网络流量实时监控
- **自动发现**: mDNS 零配置，设备自动发现主机
- **WiFi 配网**: 首次使用通过手机配网，无需修改代码
- **触摸交互**: 左右滑动切换页面，支持屏幕休眠/唤醒
- **Web 面板**: 浏览器端实时预览状态
- **可扩展**: 插件化采集器架构，轻松添加新工具支持

## 硬件

- **开发板**: Waveshare ESP32-S3 1.75" AMOLED 开发板
- **芯片**: ESP32-S3R8 N16R8 (双核 LX7 @ 240MHz, 8MB PSRAM, 16MB Flash)
- **屏幕**: 1.75" 圆形 AMOLED, 466x466, CO5300 驱动 (QSPI)
- **触控**: CST9217 电容触摸 (I2C)
- **连接**: Wi-Fi 2.4GHz + Bluetooth 5 (LE)
- **其他**: QMI8658 六轴 IMU, PCF85063 RTC, 双麦克风, 可编程按键

## 架构

```
┌────────────────────┐                    ┌────────────────────┐
│   Host Agent       │    WebSocket       │   ESP32-S3         │
│   (Python)         │ ────────────────>  │   AMOLED Display   │
│                    │    Wi-Fi           │                    │
│ ┌────────────────┐ │    mDNS discovery  │ ┌────────────────┐ │
│ │ Collectors     │ │                    │ │ LVGL UI        │ │
│ │ · Claude Code  │ │                    │ │ · Overview     │ │
│ │ · Codex        │ │                    │ │ · Tool Detail  │ │
│ │ · Gemini CLI   │ │                    │ │ · System       │ │
│ │ · System       │ │                    │ │ (swipe nav)    │ │
│ └────────────────┘ │                    │ └────────────────┘ │
│ ┌────────────────┐ │                    │ ┌────────────────┐ │
│ │ WebSocket Srv  │ │                    │ │ WiFi Provision │ │
│ │ mDNS Advertise │ │                    │ │ mDNS Discovery │ │
│ │ Web Dashboard  │ │                    │ │ NVS Settings   │ │
│ └────────────────┘ │                    │ │ Watchdog       │ │
└────────────────────┘                    │ └────────────────┘ │
                                          └────────────────────┘
```

## 快速开始

详见 [Quick Start Guide](docs/QUICK_START.md)

### 1. 烧录固件

```bash
pip install platformio
cd firmware && pio run -t upload
```

### 2. 安装主机 Agent

```bash
cd host && pip install -e '.[web]'
```

### 3. 启动

```bash
vibe-pi-host
```

### 4. 设备配网

首次启动设备会创建 WiFi 热点 `VibePi-XXXXXX`，手机连接后自动弹出配网页面。

## 显示页面

| 页面 | 内容 |
|------|------|
| **Overview** | 活跃工具名称、模型、token 消耗、费用、用量弧形进度 |
| **Detail** | 当前任务、会话数、运行时长、用量百分比 |
| **System** | CPU/内存仪表盘、网络上下行速度 |

## 交互

- **左右滑动**: 切换页面
- **短按按钮**: 切换页面 / 唤醒屏幕
- **长按按钮 (5s)**: 恢复出厂设置

## 通信协议

Host ↔ Device 使用 WebSocket JSON 协议，详见 [protocol.md](protocol.md)。

支持：版本握手、设备注册、心跳检测、指数退避重连、OTA 固件更新。

## 配置

```bash
vibe-pi-host --init-config  # 创建默认配置文件
```

详见 [Configuration Guide](docs/CONFIGURATION.md)

## 项目结构

```
vibe_pi/
├── firmware/                # ESP32-S3 PlatformIO 项目
│   ├── platformio.ini
│   ├── include/config.h     # 引脚定义、时序常量
│   └── src/
│       ├── main.cpp         # 状态机主循环
│       ├── display/         # CO5300 AMOLED + LVGL 初始化
│       ├── network/
│       │   ├── wifi_provision  # 配网 (AP + captive portal)
│       │   ├── mdns_discovery  # mDNS 自动发现
│       │   └── ws_client       # WebSocket 协议客户端
│       └── ui/
│           ├── theme          # 设计系统 (颜色、字体、间距)
│           └── ui_manager     # 多页面 UI (tileview + swipe)
├── host/                    # Python 主机 Agent
│   ├── pyproject.toml
│   └── src/
│       ├── main.py          # CLI 入口、事件循环
│       ├── config.py        # TOML 配置管理
│       ├── protocol.py      # 消息构建与解析
│       ├── collectors/      # 状态采集器
│       │   ├── claude_code  # Claude Code 会话/用量
│       │   ├── codex        # OpenAI Codex
│       │   ├── gemini_cli   # Google Gemini CLI
│       │   └── system       # CPU/内存/网络
│       └── server/
│           ├── ws_server     # WebSocket 服务 + 设备管理
│           ├── mdns          # mDNS 服务广播
│           └── web_dashboard # 浏览器状态面板
├── protocol.md              # 通信协议规范
├── docs/                    # 用户文档
│   ├── QUICK_START.md
│   ├── CONFIGURATION.md
│   └── TROUBLESHOOTING.md
└── README.md
```

## License

MIT
