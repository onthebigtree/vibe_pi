#include "ws_client.h"
#include "config.h"
#include "../ui/ui_manager.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

static WebSocketsClient ws;
static bool connected = false;

static void on_ws_event(WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            Serial.println("[WS] Connected to host agent");
            connected = true;
            ui_show_dashboard();
            break;

        case WStype_DISCONNECTED:
            Serial.println("[WS] Disconnected");
            connected = false;
            ui_show_connecting();
            break;

        case WStype_TEXT: {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err) {
                Serial.printf("[WS] JSON parse error: %s\n", err.c_str());
                break;
            }
            ui_update_data(doc);
            break;
        }

        default:
            break;
    }
}

void ws_client_init() {
    ws.begin(WS_HOST, WS_PORT, WS_PATH);
    ws.onEvent(on_ws_event);
    ws.setReconnectInterval(RECONNECT_INTERVAL_MS);
    Serial.printf("[WS] Connecting to %s:%d\n", WS_HOST, WS_PORT);
}

void ws_client_loop() {
    ws.loop();
}

bool ws_client_is_connected() {
    return connected;
}

void ws_client_reconnect() {
    ws.disconnect();
    ws.begin(WS_HOST, WS_PORT, WS_PATH);
    ws.onEvent(on_ws_event);
}
