#include "ws_client.h"
#include "config.h"
#include "../ui/ui_manager.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static WebSocketsClient ws;
static bool connected = false;
static bool registered = false;
static unsigned long lastStatusTime = 0;
static unsigned long lastPingSent = 0;
static unsigned long connectAttemptTime = 0;
static uint32_t reconnectDelay = WS_RECONNECT_BASE_MS;

static void handle_hello(JsonObject &payload) {
    Serial.printf("[WS] Host: v%s, protocol=%d\n",
                  payload["host_version"].as<const char*>(),
                  payload["protocol_version"].as<int>());
    ws_client_send_register();
}

static void handle_registered(JsonObject &payload) {
    registered = true;
    Serial.printf("[WS] Registered as: %s\n", payload["device_id"].as<const char*>());

    JsonObject config = payload["config"];
    if (!config.isNull()) {
        if (config["brightness"].is<int>()) {
            display_set_brightness(config["brightness"].as<uint8_t>());
        }
    }

    ui_show_dashboard();
}

static void handle_status(JsonObject &payload) {
    lastStatusTime = millis();
    ui_update_status(payload);
}

static void on_ws_event(WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            Serial.println("[WS] Connected");
            connected = true;
            registered = false;
            reconnectDelay = WS_RECONNECT_BASE_MS;
            break;

        case WStype_DISCONNECTED:
            Serial.println("[WS] Disconnected");
            connected = false;
            registered = false;
            ui_show_reconnecting();
            break;

        case WStype_TEXT: {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err) {
                Serial.printf("[WS] JSON error: %s\n", err.c_str());
                break;
            }

            const char *msgType = doc["type"];
            if (!msgType) break;

            JsonObject pl = doc["payload"];

            if (strcmp(msgType, "hello") == 0)           handle_hello(pl);
            else if (strcmp(msgType, "registered") == 0) handle_registered(pl);
            else if (strcmp(msgType, "status") == 0)     handle_status(pl);
            else if (strcmp(msgType, "pong") == 0)       { /* heartbeat ok */ }
            else Serial.printf("[WS] Unknown type: %s\n", msgType);
            break;
        }

        default:
            break;
    }
}

void ws_client_init(const char *host, uint16_t port) {
    ws.begin(host, port, "/");
    ws.onEvent(on_ws_event);
    ws.setReconnectInterval(0); // we handle reconnection ourselves
    connectAttemptTime = millis();
    Serial.printf("[WS] Connecting to %s:%d\n", host, port);
}

void ws_client_loop() {
    ws.loop();

    if (connected && registered) {
        unsigned long now = millis();
        if (now - lastPingSent > HEARTBEAT_INTERVAL_MS) {
            ws_client_send_ping();
            lastPingSent = now;
        }
    }
}

bool ws_client_is_connected() {
    return connected && registered;
}

void ws_client_reconnect() {
    ws.disconnect();
    delay(reconnectDelay);
    reconnectDelay = min(reconnectDelay * 2, (uint32_t)WS_RECONNECT_MAX_MS);
    connectAttemptTime = millis();
    // ws.begin will be called again from main loop with discovered host
}

void ws_client_send_register() {
    JsonDocument doc;
    doc["v"] = 1;
    doc["type"] = "register";
    doc["ts"] = (unsigned long)(millis() / 1000);

    JsonObject payload = doc["payload"].to<JsonObject>();
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    char deviceId[20];
    snprintf(deviceId, sizeof(deviceId), "vibepi-%02x%02x%02x", mac[3], mac[4], mac[5]);

    payload["device_id"] = deviceId;
    payload["firmware_version"] = FW_VERSION;
    payload["hardware"] = DEVICE_HARDWARE;
    payload["display"] = "466x466";
    payload["mac"] = macStr;

    String json;
    serializeJson(doc, json);
    ws.sendTXT(json);
    Serial.printf("[WS] Sent register: %s\n", deviceId);
}

void ws_client_send_ping() {
    ws.sendTXT("{\"v\":1,\"type\":\"ping\",\"ts\":0,\"payload\":{}}");
}

void ws_client_send_settings(JsonDocument &settings) {
    JsonDocument doc;
    doc["v"] = 1;
    doc["type"] = "settings_update";
    doc["ts"] = (unsigned long)(millis() / 1000);
    doc["payload"] = settings;

    String json;
    serializeJson(doc, json);
    ws.sendTXT(json);
}

unsigned long ws_client_last_status_time() {
    return lastStatusTime;
}
