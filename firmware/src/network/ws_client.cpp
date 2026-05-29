#include "ws_client.h"
#include "config.h"
#include "../ui/ui_manager.h"
#include "../system/settings_manager.h"
#include "../system/pairing_manager.h"
#include "../system/ota_manager.h"
#include "../system/reset_manager.h"
#include "../system/power_manager.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static WebSocketsClient ws;
static bool connected = false;
static bool registered = false;
static unsigned long lastStatusTime = 0;
static unsigned long lastPingSent = 0;
static uint32_t reconnectDelay = WS_RECONNECT_BASE_MS;

// ── Message handlers ──

static void handle_hello(JsonObject &p) {
    Serial.printf("[WS] Host: v%s, protocol=%d\n",
                  p["host_version"].as<const char*>(),
                  p["protocol_version"].as<int>());
    ws_client_send_register();
}

static void handle_registered(JsonObject &p) {
    registered = true;
    bool paired = p["paired"] | false;
    Serial.printf("[WS] Registered: %s (paired=%d)\n",
                  p["device_id"].as<const char*>(), paired);

    JsonObject config = p["config"];
    if (!config.isNull()) {
        settings_apply_sync(config);
        display_set_brightness(settings_get().brightness);
    }
}

static void handle_pair_confirm(JsonObject &p) {
    const char *token = p["token"] | "";
    const char *hostName = p["host_name"] | "";
    // Get host IP from current WS connection
    DeviceSettings &s = settings_get();
    pairing_on_confirm(token, hostName, s.host_addr, s.host_port);
}

static void handle_pair_reject(JsonObject &p) {
    pairing_on_reject();
}

static void handle_unpair(JsonObject &p) {
    pairing_unpair();
}

static void handle_status(JsonObject &p) {
    lastStatusTime = millis();
    power_register_activity();
    ui_update_status(p);
}

static void handle_settings_sync(JsonObject &p) {
    bool changed = settings_apply_sync(p);
    if (changed) {
        display_set_brightness(settings_get().brightness);
        Serial.println("[WS] Settings synced from host");
    }
    // Send ack
    ws.sendTXT("{\"v\":2,\"type\":\"settings_ack\",\"ts\":0,\"payload\":{\"ok\":true}}");
}

static void handle_ota_available(JsonObject &p) {
    ota_on_available(p);
}

static void handle_ota_start(JsonObject &p) {
    ota_on_start(p);
}

static void handle_reset_command(JsonObject &p) {
    int level = p["level"] | 0;
    const char *reason = p["reason"] | "";
    Serial.printf("[WS] Remote reset L%d: %s\n", level, reason);
    reset_execute((ResetLevel)level, true);
}

static void handle_device_rename(JsonObject &p) {
    const char *name = p["name"] | "";
    if (strlen(name) > 0) {
        strlcpy(settings_get().device_name, name, sizeof(settings_get().device_name));
        settings_save();
        Serial.printf("[WS] Renamed to: %s\n", name);
    }
}

// ── WebSocket event handler ──

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
            if (deserializeJson(doc, payload, length)) break;

            const char *msgType = doc["type"];
            if (!msgType) break;
            JsonObject p = doc["payload"];

            if      (strcmp(msgType, "hello") == 0)          handle_hello(p);
            else if (strcmp(msgType, "registered") == 0)     handle_registered(p);
            else if (strcmp(msgType, "pair_confirm") == 0)   handle_pair_confirm(p);
            else if (strcmp(msgType, "pair_reject") == 0)    handle_pair_reject(p);
            else if (strcmp(msgType, "unpair") == 0)         handle_unpair(p);
            else if (strcmp(msgType, "status") == 0)         handle_status(p);
            else if (strcmp(msgType, "settings_sync") == 0)  handle_settings_sync(p);
            else if (strcmp(msgType, "ota_available") == 0)  handle_ota_available(p);
            else if (strcmp(msgType, "ota_start") == 0)      handle_ota_start(p);
            else if (strcmp(msgType, "reset_command") == 0)  handle_reset_command(p);
            else if (strcmp(msgType, "device_rename") == 0)  handle_device_rename(p);
            else if (strcmp(msgType, "pong") == 0)           { /* ok */ }
            break;
        }

        default: break;
    }
}

// ── Public API ──

void ws_client_init(const char *host, uint16_t port) {
    // Save host info for reconnection
    strlcpy(settings_get().host_addr, host, sizeof(settings_get().host_addr));
    settings_get().host_port = port;

    // WSS on port 8766 (8765 + 1) by convention; insecure ws on 8765
    if (port == 8766 || port == 443) {
        ws.beginSSL(host, port, "/ws");
        Serial.printf("[WSS] Connecting to wss://%s:%d (TLS, no cert verify)\n", host, port);
    } else {
        ws.begin(host, port, "/ws");
        Serial.printf("[WS] Connecting to %s:%d\n", host, port);
    }
    ws.onEvent(on_ws_event);
    // Backoff between reconnect attempts. 0 would let ws.loop() hammer TCP
    // connect ~30x/sec when the host is absent, burning CPU and flooding the
    // host's TCP backlog with resets. 1s is responsive yet polite.
    ws.setReconnectInterval(WS_RECONNECT_BASE_MS);
    // Respond to WebSocket-protocol PING control frames every 15s; reconnect after 2 missed pongs
    ws.enableHeartbeat(15000, 3000, 2);
}

void ws_client_loop() {
    ws.loop();

    if (connected && registered) {
        if (millis() - lastPingSent > HEARTBEAT_INTERVAL_MS) {
            ws_client_send_ping();
            lastPingSent = millis();
        }
    }
}

bool ws_client_is_connected() { return connected && registered; }

void ws_client_reconnect() {
    ws.disconnect();
    delay(min(reconnectDelay, (uint32_t)WS_RECONNECT_MAX_MS));
    reconnectDelay *= 2;
}

void ws_client_send_register() {
    JsonDocument doc;
    doc["v"] = PROTOCOL_VERSION;
    doc["type"] = "register";
    doc["ts"] = (unsigned long)(millis() / 1000);

    JsonObject p = doc["payload"].to<JsonObject>();
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    char devId[20];
    snprintf(devId, sizeof(devId), "vibepi-%02x%02x%02x", mac[3], mac[4], mac[5]);

    DeviceSettings &s = settings_get();
    p["device_id"] = devId;
    p["firmware_version"] = FW_VERSION;
    p["hardware"] = DEVICE_HARDWARE;
    p["display"] = "466x466";
    p["mac"] = macStr;
    p["paired_token"] = s.pair_token;
    p["language"] = (s.language == Lang::ZH) ? "zh" : "en";

    JsonArray caps = p["capabilities"].to<JsonArray>();
    caps.add("touch");
    caps.add("imu");
    caps.add("mic");
    caps.add("rtc");

    String json;
    serializeJson(doc, json);
    ws.sendTXT(json);
}

void ws_client_send_ping() {
    ws.sendTXT("{\"v\":2,\"type\":\"ping\",\"ts\":0,\"payload\":{}}");
}

void ws_client_send_pair_request(const char *deviceId, const char *pairCode, const char *deviceName) {
    JsonDocument doc;
    doc["v"] = PROTOCOL_VERSION;
    doc["type"] = "pair_request";
    doc["ts"] = (unsigned long)(millis() / 1000);
    JsonObject p = doc["payload"].to<JsonObject>();
    p["device_id"] = deviceId;
    p["pair_code"] = pairCode;
    p["device_name"] = deviceName;

    String json;
    serializeJson(doc, json);
    ws.sendTXT(json);
}

void ws_client_send_settings(JsonDocument &settings) {
    JsonDocument doc;
    doc["v"] = PROTOCOL_VERSION;
    doc["type"] = "settings_update";
    doc["ts"] = (unsigned long)(millis() / 1000);
    doc["payload"] = settings;

    String json;
    serializeJson(doc, json);
    ws.sendTXT(json);
}

void ws_client_send_json(JsonDocument &doc) {
    String json;
    serializeJson(doc, json);
    ws.sendTXT(json);
}

void ws_client_send_ota_progress(const char *version, uint8_t pct, uint32_t bytesReceived) {
    JsonDocument doc;
    doc["v"] = PROTOCOL_VERSION;
    doc["type"] = "ota_progress";
    doc["ts"] = (unsigned long)(millis() / 1000);
    JsonObject p = doc["payload"].to<JsonObject>();
    p["version"] = version;
    p["progress_pct"] = pct;
    p["bytes_received"] = bytesReceived;
    ws_client_send_json(doc);
}

void ws_client_send_ota_done(const char *version, bool success, bool sha256Ok) {
    JsonDocument doc;
    doc["v"] = PROTOCOL_VERSION;
    doc["type"] = "ota_done";
    doc["ts"] = (unsigned long)(millis() / 1000);
    JsonObject p = doc["payload"].to<JsonObject>();
    p["version"] = version;
    p["success"] = success;
    p["sha256_ok"] = sha256Ok;
    ws_client_send_json(doc);
}

void ws_client_send_ota_failed(const char *version, const char *error) {
    JsonDocument doc;
    doc["v"] = PROTOCOL_VERSION;
    doc["type"] = "ota_failed";
    doc["ts"] = (unsigned long)(millis() / 1000);
    JsonObject p = doc["payload"].to<JsonObject>();
    p["version"] = version;
    p["error"] = error;
    ws_client_send_json(doc);
}

unsigned long ws_client_last_status_time() { return lastStatusTime; }
