# Vibe Pi

ESP32-S3 圆形 AMOLED 状态监视器，显示 Claude Code / Codex 等 AI 工具运行状态。

## 硬件

- Waveshare ESP32-S3 1.75" AMOLED 开发板 (N16R8)
- 466x466 圆形 AMOLED, CO5300 驱动 (QSPI), CST9217 触摸 (I2C)
- Wi-Fi + BLE 5

## 项目结构

- `firmware/` - ESP32 PlatformIO 项目 (Arduino + LVGL 9)
- `host/` - Python 主机端 agent (websockets + psutil)

## 开发命令

### Firmware
```bash
cd firmware && pio run          # 编译
cd firmware && pio run -t upload # 烧录
cd firmware && pio device monitor # 串口监视
```

### Host
```bash
cd host
pip install -e .
vibe-pi-host                    # 启动 WebSocket 服务
```

## 通信协议

Host -> ESP32 via WebSocket (JSON):
```json
{
  "tool": "Claude Code",
  "status": "active",
  "model": "opus-4",
  "tokens": "12.5K tokens",
  "cost": "$0.42",
  "usage_pct": 35,
  "system": { "cpu_pct": 45, "mem_pct": 62 }
}
```
