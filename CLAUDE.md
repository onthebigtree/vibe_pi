# Vibe Pi

ESP32-S3 圆形 AMOLED 状态监视器，显示 Claude Code / Codex / Gemini CLI 等 AI 工具运行状态。商业产品级架构。

## 硬件

- Waveshare ESP32-S3 1.75" AMOLED (N16R8)
- 466x466 圆形 AMOLED, CO5300 (QSPI), CST9217 触摸 (I2C), QMI8658 IMU
- Wi-Fi + BLE 5

## 项目结构

- `firmware/` — ESP32 PlatformIO (Arduino + LVGL 9)
- `host/` — Python host agent (websockets + psutil + zeroconf + aiohttp)
- `protocol.md` — 通信协议 v2 规范

## 开发命令

### Firmware
```bash
cd firmware && pio run                # 编译
cd firmware && pio run -t upload      # 烧录
cd firmware && pio device monitor     # 串口
```

### Host
```bash
cd host && pip install -e '.[web,dev]'
vibe-pi-host                          # 启动
vibe-pi-host --debug                  # 调试模式
vibe-pi-host init                     # 生成配置文件
vibe-pi-host devices list             # 列出设备
vibe-pi-host devices rename ID NAME   # 重命名设备
vibe-pi-host devices unpair ID        # 取消配对
vibe-pi-host ota publish FILE VER     # 发布固件
vibe-pi-host ota list                 # 查看已发布版本
```

## 系统架构

### 固件状态机
```
SELF_TEST → OOBE(首次) / CONNECTING_WIFI(已配置)
    OOBE: 语言→WiFi扫描→密码→配对→同步→完成
    CONNECTING_WIFI → DISCOVERING → CONNECTING_WS → PAIRING → RUNNING
    RUNNING ↔ RECONNECTING
    任何状态可进入 SAFE_MODE (连续崩溃3次)
```

### 系统模块 (firmware/src/system/)
- `i18n` — 中英双语字符串表
- `settings_manager` — NVS 设置读写、分类、校验、导出导入、双向同步
- `health_manager` — POST 自检、错误日志环形缓冲、崩溃计数、安全模式
- `reset_manager` — L0~L3 四级重置
- `power_manager` — ACTIVE→DIM→SCREEN_OFF→DEEP_SLEEP 渐进休眠
- `pairing_manager` — 6位配对码、token 存储、超时管理
- `ota_manager` — HTTP 下载、SHA256 校验、A/B 分区切换、回滚

### UI 页面 (firmware/src/ui/)
- `oobe_flow` — OOBE 引导 (语言/WiFi/配对/同步)
- `ui_manager` — 主界面 tileview (总览/详情/系统) + 配对/安全模式屏
- `settings_page` — 触摸设置页 (显示/系统/关于/重置)
- `ota_page` — OTA 升级页 (版本/进度/状态)
- `diagnostic_page` — 诊断页 (内存/WiFi/温度/崩溃)

### Host 模块
- `protocol.py` — v2 协议消息构建/解析
- `config.py` — TOML 配置管理
- `device_registry.py` — 设备注册表 JSON 持久化
- `pairing.py` — 配对流程 + HMAC token
- `ota_server.py` — 固件文件 HTTP 服务 + 版本清单
- `cli.py` — 子命令: run/init/devices/ota/pair
- `server/ws_server.py` — 协议 v2 WebSocket 服务
- `server/mdns.py` — mDNS 广播
- `server/web_dashboard.py` — Web 管理面板

## 通信协议 v2

见 `protocol.md`。消息类型: hello, register, registered, pair_request, pair_confirm, pair_reject, unpair, status, ping, pong, settings_sync, settings_update, settings_ack, ota_available, ota_accept, ota_start, ota_progress, ota_done, ota_failed, health_report, reset_command, device_rename, error

## CO5300 / CST9217 驱动

显示驱动和触摸驱动留有 TODO stub，需参考 Waveshare 官方示例代码填充。
