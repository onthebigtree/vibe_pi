#pragma once

// Wi-Fi
#define WIFI_SSID "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"

// WebSocket server (host agent)
#define WS_HOST "192.168.1.100"
#define WS_PORT 8765
#define WS_PATH "/"

// Display
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

// Misc
#define BUTTON_PIN        0
#define STATUS_UPDATE_INTERVAL_MS 2000
#define RECONNECT_INTERVAL_MS     5000
