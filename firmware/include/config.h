#pragma once

// ── Firmware version ──
#define FW_VERSION "0.1.0"
#define DEVICE_HARDWARE "waveshare-esp32s3-amoled-175"

// ── Display: 1.75" Round AMOLED 466x466 ──
#define SCREEN_WIDTH 466
#define SCREEN_HEIGHT 466
#define SCREEN_RADIUS 233

// CO5300 AMOLED (QSPI)
#define AMOLED_QSPI_CS   12
#define AMOLED_QSPI_SCK  47
#define AMOLED_QSPI_D0   18
#define AMOLED_QSPI_D1   7
#define AMOLED_QSPI_D2   48
#define AMOLED_QSPI_D3   5
#define AMOLED_RST        17
#define AMOLED_TE         9

// CST9217 Touch (I2C)
#define TOUCH_SDA  39
#define TOUCH_SCL  40
#define TOUCH_INT  13
#define TOUCH_RST  14

// ── Peripherals ──
#define BUTTON_PIN        0

// ── Network defaults ──
#define AP_SSID_PREFIX    "VibePi-"
#define WS_DEFAULT_PORT   8765
#define MDNS_SERVICE      "_vibepi"
#define MDNS_PROTO        "_tcp"

// ── Timing ──
#define WIFI_CONNECT_TIMEOUT_MS   15000
#define WS_RECONNECT_BASE_MS      1000
#define WS_RECONNECT_MAX_MS       30000
#define HEARTBEAT_INTERVAL_MS     10000
#define HEARTBEAT_TIMEOUT_MS      15000
#define STATUS_STALE_TIMEOUT_MS   15000
#define DISPLAY_SLEEP_TIMEOUT_MS  60000

// ── NVS keys ──
#define NVS_NAMESPACE     "vibepi"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"
#define NVS_KEY_WS_HOST   "ws_host"
#define NVS_KEY_WS_PORT   "ws_port"
#define NVS_KEY_BRIGHT    "brightness"
#define NVS_KEY_SETUP_DONE "setup_done"
