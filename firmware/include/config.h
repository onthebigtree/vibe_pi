#pragma once

// ── Firmware ──
#define FW_VERSION        "0.2.0"
#define PROTOCOL_VERSION  2
#define DEVICE_HARDWARE   "waveshare-esp32s3-amoled-175"

// ── Display: 1.75" Round AMOLED 466x466 ──
#define SCREEN_WIDTH   466
#define SCREEN_HEIGHT  466
#define SCREEN_RADIUS  233

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

// QMI8658 IMU (I2C, same bus as touch)
#define IMU_ADDR   0x6B

// Peripherals
#define BUTTON_PIN 0

// ── Network ──
#define AP_SSID_PREFIX        "VibePi-"
#define WS_DEFAULT_PORT       8765
#define MDNS_SERVICE          "_vibepi"
#define MDNS_PROTO            "_tcp"

// ── Timing ──
#define WIFI_CONNECT_TIMEOUT_MS   15000
#define WIFI_SCAN_TIMEOUT_MS      10000
#define WS_RECONNECT_BASE_MS       1000
#define WS_RECONNECT_MAX_MS       30000
#define WS_HELLO_TIMEOUT_MS        5000
#define HEARTBEAT_INTERVAL_MS     10000
#define HEARTBEAT_TIMEOUT_MS      15000
#define STATUS_STALE_TIMEOUT_MS   15000
#define HEALTH_REPORT_INTERVAL_MS 30000
#define PAIR_CODE_TIMEOUT_MS     300000  // 5 min

// ── Power Management ──
#define DIM_TIMEOUT_MS         30000   // dim after 30s inactivity
#define SLEEP_TIMEOUT_MS       60000   // screen off after 60s
#define DEEP_SLEEP_TIMEOUT_MS 300000   // deep sleep after 5 min
#define DIM_BRIGHTNESS         20
#define DEFAULT_BRIGHTNESS     80

// ── Health / Diagnostics ──
#define MAX_CRASH_COUNT_SAFE_MODE  3
#define ERROR_LOG_MAX_ENTRIES     32
#define SELF_TEST_TIMEOUT_MS    5000
#define WATCHDOG_TIMEOUT_MS   120000

// ── OTA ──
#define OTA_HTTP_TIMEOUT_MS    30000
#define OTA_CHUNK_SIZE          4096
#define OTA_MAX_RETRIES            3

// ── NVS Namespace & Keys ──
#define NVS_NAMESPACE      "vibepi"

// WiFi
#define NVS_WIFI_SSID      "wifi_ssid"
#define NVS_WIFI_PASS      "wifi_pass"

// Pairing
#define NVS_PAIRED         "paired"
#define NVS_PAIR_TOKEN     "pair_token"
#define NVS_HOST_ADDR      "host_addr"
#define NVS_HOST_PORT      "host_port"
#define NVS_HOST_NAME      "host_name"

// Settings
#define NVS_LANGUAGE       "language"
#define NVS_BRIGHTNESS     "brightness"
#define NVS_SLEEP_TIMEOUT  "sleep_to"
#define NVS_THEME          "theme"
#define NVS_DEVICE_NAME    "dev_name"
#define NVS_TIMEZONE       "timezone"
#define NVS_ALERT_USAGE    "alert_usage"
#define NVS_ALERT_DISCONN  "alert_disc"
#define NVS_OTA_CHANNEL    "ota_chan"
#define NVS_PAGE_ORDER     "page_order"
#define NVS_OOBE_DONE      "oobe_done"

// Health
#define NVS_CRASH_COUNT    "crash_cnt"
#define NVS_LAST_CRASH     "last_crash"
#define NVS_BOOT_COUNT     "boot_cnt"
