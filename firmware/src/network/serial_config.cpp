#include "serial_config.h"
#include "../system/settings_manager.h"
#include "../system/health_manager.h"
#include "../system/reset_manager.h"
#include "../system/power_manager.h"
#include "../system/ota_manager.h"
#include "../system/i18n.h"
#include "../ui/ui_manager.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <lvgl.h>

static char _ssid[64] = "";
static char _pass[64] = "";
static bool _has_wifi = false;
static bool _setup_done = false;
static bool _authenticated = false; // serial session auth
static String _buf;

static void handle_cmd(JsonDocument &doc) {
    const char *cmd = doc["cmd"];
    if (!cmd) return;

    if (strcmp(cmd, "ping") == 0) {
        DeviceSettings &s = settings_get();
        Serial.printf("{\"ok\":true,\"device\":\"vibepi\",\"version\":\"%s\",\"lang\":\"%s\",\"name\":\"%s\",\"oobe_done\":%s}\n",
                      FW_VERSION, (s.language == Lang::ZH) ? "zh" : "en",
                      s.device_name, s.oobe_done ? "true" : "false");
    }
    else if (strcmp(cmd, "scan") == 0) {
        WiFi.mode(WIFI_STA);
        int n = WiFi.scanNetworks();
        Serial.print("{\"ok\":true,\"networks\":[");
        for (int i = 0; i < n; i++) {
            if (i > 0) Serial.print(",");
            Serial.printf("{\"ssid\":\"%s\",\"rssi\":%d,\"enc\":%d}",
                          WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        }
        Serial.println("]}");
        WiFi.scanDelete();
    }
    else if (strcmp(cmd, "set_host") == 0) {
        const char *host = doc["host"];
        int port = doc["port"] | 8765;
        if (host && strlen(host) > 0) {
            strlcpy(settings_get().host_addr, host, sizeof(settings_get().host_addr));
            settings_get().host_port = port;
            settings_save();
            Serial.printf("{\"ok\":true,\"host\":\"%s\",\"port\":%d}\n", host, port);
        }
    }
    else if (strcmp(cmd, "wifi") == 0) {
        const char *ssid = doc["ssid"];
        const char *pass = doc["pass"] | "";
        const char *host = doc["host"] | "";
        bool persist = doc["persist"] | true;  // save to NVS by default
        if (ssid && strlen(ssid) > 0) {
            strlcpy(_ssid, ssid, sizeof(_ssid));
            strlcpy(_pass, pass, sizeof(_pass));
            if (strlen(host) > 0) {
                strlcpy(settings_get().host_addr, host, sizeof(settings_get().host_addr));
                settings_get().host_port = doc["port"] | 8765;
            }
            _has_wifi = true;
            if (persist) {
                settings_save_wifi(ssid, pass);
                settings_save();
                Serial.printf("{\"ok\":true,\"ssid\":\"%s\",\"saved\":true,\"reboot_required\":true}\n", _ssid);
            } else {
                Serial.printf("{\"ok\":true,\"ssid\":\"%s\",\"saved\":false}\n", _ssid);
            }
        } else {
            Serial.println("{\"ok\":false,\"error\":\"ssid required\"}");
        }
    }
    else if (strcmp(cmd, "set_lang") == 0) {
        const char *lang = doc["lang"];
        if (lang) {
            Lang l = (strcmp(lang, "en") == 0) ? Lang::EN : Lang::ZH;
            settings_get().language = l;
            i18n_set_lang(l);
            settings_save();
            Serial.printf("{\"ok\":true,\"lang\":\"%s\"}\n", lang);
        }
    }
    else if (strcmp(cmd, "set_name") == 0) {
        const char *name = doc["name"];
        if (name && strlen(name) > 0) {
            strlcpy(settings_get().device_name, name, sizeof(settings_get().device_name));
            settings_save();
            Serial.printf("{\"ok\":true,\"name\":\"%s\"}\n", name);
        }
    }
    else if (strcmp(cmd, "set_brightness") == 0) {
        int b = doc["value"] | -1;
        if (b >= 0 && b <= 100) {
            settings_get().brightness = b;
            settings_save();
            Serial.printf("{\"ok\":true,\"brightness\":%d}\n", b);
        }
    }
    else if (strcmp(cmd, "finish_setup") == 0) {
        settings_mark_oobe_done();
        _setup_done = true;
        Serial.println("{\"ok\":true,\"setup\":\"done\"}");
    }
    else if (strcmp(cmd, "get_settings") == 0) {
        DeviceSettings &s = settings_get();
        Serial.printf("{\"ok\":true,\"brightness\":%d,\"lang\":\"%s\",\"name\":\"%s\",\"sleep_ms\":%lu,\"theme\":\"%s\"}\n",
                      s.brightness,
                      (s.language == Lang::ZH) ? "zh" : "en",
                      s.device_name,
                      s.sleep_timeout_ms,
                      s.theme);
    }
    else if (strcmp(cmd, "reset") == 0) {
        int level = doc["level"] | 0;
        if (level >= 0 && level <= 3) {
            Serial.printf("{\"ok\":true,\"reset_level\":%d}\n", level);
            delay(200);
            reset_execute((ResetLevel)level, true);
        }
    }
    else if (strcmp(cmd, "diagnostics") == 0) {
        HealthData hd = health_get_data();
        DeviceSettings &s = settings_get();
        Serial.printf("{\"ok\":true,\"uptime_sec\":%lu,\"free_heap\":%lu,\"wifi_rssi\":%d,"
                      "\"temp_c\":%.1f,\"crash_count\":%lu,\"safe_mode\":%s,"
                      "\"boot_count\":%lu,\"fw\":\"%s\",\"hw\":\"%s\"}\n",
                      hd.uptime_sec, hd.free_heap, hd.wifi_rssi,
                      hd.temperature_c, hd.crash_count, hd.safe_mode ? "true" : "false",
                      s.boot_count, FW_VERSION, DEVICE_HARDWARE);
    }
    else if (strcmp(cmd, "set_sleep") == 0) {
        int ms = doc["ms"] | -1;
        if (ms >= 5000) {
            settings_get().sleep_timeout_ms = ms;
            settings_save();
            Serial.printf("{\"ok\":true,\"sleep_ms\":%d}\n", ms);
        }
    }
    else if (strcmp(cmd, "set_theme") == 0) {
        const char *theme = doc["theme"];
        if (theme) {
            strlcpy(settings_get().theme, theme, sizeof(settings_get().theme));
            settings_save();
            Serial.printf("{\"ok\":true,\"theme\":\"%s\"}\n", theme);
        }
    }
    else if (strcmp(cmd, "ui_dump") == 0) {
        // Dump current LVGL screen widget tree
        lv_obj_t *scr = lv_screen_active();
        if (!scr) { Serial.println("{\"ok\":false,\"error\":\"no screen\"}"); return; }

        Serial.print("{\"ok\":true,\"screen\":{\"w\":");
        Serial.print(lv_obj_get_width(scr));
        Serial.print(",\"h\":");
        Serial.print(lv_obj_get_height(scr));
        Serial.print("},\"widgets\":[");

        // Recursive lambda via function pointer
        struct Dumper {
            static void dump(lv_obj_t *obj, int depth) {
                if (depth > 0) Serial.print(",");

                int32_t x = lv_obj_get_x(obj);
                int32_t y = lv_obj_get_y(obj);
                int32_t w = lv_obj_get_width(obj);
                int32_t h = lv_obj_get_height(obj);

                // Detect type
                const char *type = "obj";
                if (lv_obj_check_type(obj, &lv_label_class)) type = "label";
                else if (lv_obj_check_type(obj, &lv_button_class)) type = "btn";
                else if (lv_obj_check_type(obj, &lv_arc_class)) type = "arc";
                else if (lv_obj_check_type(obj, &lv_spinner_class)) type = "spinner";
                else if (lv_obj_check_type(obj, &lv_bar_class)) type = "bar";

                Serial.printf("{\"t\":\"%s\",\"x\":%ld,\"y\":%ld,\"w\":%ld,\"h\":%ld",
                              type, x, y, w, h);

                // Get text for labels
                if (lv_obj_check_type(obj, &lv_label_class)) {
                    const char *txt = lv_label_get_text(obj);
                    if (txt) {
                        Serial.print(",\"text\":\"");
                        // Escape quotes in text
                        for (const char *p = txt; *p; p++) {
                            if (*p == '"') Serial.print("\\\"");
                            else if (*p == '\n') Serial.print("\\n");
                            else Serial.print(*p);
                        }
                        Serial.print("\"");
                    }
                }

                Serial.print("}");

                // Recurse children
                uint32_t cnt = lv_obj_get_child_count(obj);
                for (uint32_t i = 0; i < cnt; i++) {
                    dump(lv_obj_get_child(obj, i), depth + 1);
                }
            }
        };

        uint32_t cnt = lv_obj_get_child_count(scr);
        for (uint32_t i = 0; i < cnt; i++) {
            if (i > 0) Serial.print(",");
            Dumper::dump(lv_obj_get_child(scr, i), 0);
        }
        Serial.println("]}");
    }
    else {
        Serial.printf("{\"ok\":false,\"error\":\"unknown cmd: %s\"}\n", cmd);
    }
}

void serial_config_init() {
    _buf.reserve(256);
}

static void handle_protocol_msg(JsonDocument &doc);
static bool _serial_transport_active = false;
static unsigned long _last_serial_status = 0;

void serial_config_loop() {
    int read_this_iter = 0;
    while (Serial.available()) {
        char c = Serial.read();
        read_this_iter++;
        if (c == '\n' || c == '\r') {
            if (_buf.length() > 0) {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, _buf);
                if (!err) {
                    if (doc.containsKey("cmd")) {
                        handle_cmd(doc);
                    } else if (doc.containsKey("type")) {
                        handle_protocol_msg(doc);
                    }
                } else {
                    // Search for embedded { } pairs — buf may have multiple concatenated msgs
                    Serial.printf("[JSON ERR] %s len=%u\n", err.c_str(), _buf.length());
                    // Try to recover: find last `}{` boundary and parse last msg
                    int last = _buf.lastIndexOf("}{");
                    if (last > 0) {
                        String last_msg = _buf.substring(last + 1);
                        JsonDocument doc2;
                        if (!deserializeJson(doc2, last_msg)) {
                            Serial.printf("[JSON RECOVERED] parsing last msg, len=%u\n", last_msg.length());
                            if (doc2.containsKey("type")) handle_protocol_msg(doc2);
                        }
                    }
                }
                _buf = "";
            }
        } else {
            _buf += c;
            if (_buf.length() > 2048) {
                Serial.printf("[BUF OVF] %u bytes, last10=%.10s\n",
                              _buf.length(), _buf.c_str() + _buf.length() - 10);
                _buf = "";
            }
        }
    }
}

// ── Serial Transport (protocol v2 over USB) ──

static void handle_protocol_msg(JsonDocument &doc) {
    const char *msgType = doc["type"];
    if (!msgType) return;
    JsonObject p = doc["payload"];

    if (strcmp(msgType, "hello") == 0) {
        _serial_transport_active = true;
        Serial.printf("[Serial] Host connected: v%s\n",
                      p["host_version"].as<const char*>());
        // Auto-register
        serial_transport_send_register();
    }
    else if (strcmp(msgType, "registered") == 0) {
        bool paired = p["paired"] | false;
        Serial.printf("[Serial] Registered (paired=%d)\n", paired);
        JsonObject config = p["config"];
        if (!config.isNull()) {
            settings_apply_sync(config);
        }
    }
    else if (strcmp(msgType, "status") == 0) {
        _last_serial_status = millis();
        power_register_activity();
        static uint32_t status_count = 0;
        status_count++;
        const char *at = p["active_tool"] | "?";
        Serial.printf("[STATUS #%u] active=%s\n", status_count, at);
        ui_update_status(p);
    }
    else if (strcmp(msgType, "pong") == 0) {
        // heartbeat ack
    }
    else if (strcmp(msgType, "settings_sync") == 0) {
        settings_apply_sync(p);
        Serial.println("{\"v\":2,\"type\":\"settings_ack\",\"ts\":0,\"payload\":{\"ok\":true}}");
    }
    else if (strcmp(msgType, "ota_available") == 0) {
        ota_on_available(p);
    }
    else if (strcmp(msgType, "ota_start") == 0) {
        ota_on_start(p);
    }
    else if (strcmp(msgType, "reset_command") == 0) {
        int level = p["level"] | 0;
        reset_execute((ResetLevel)level, true);
    }
    else if (strcmp(msgType, "device_rename") == 0) {
        const char *name = p["name"] | "";
        if (strlen(name) > 0) {
            strlcpy(settings_get().device_name, name, sizeof(settings_get().device_name));
            settings_save();
        }
    }
}

void serial_transport_send_register() {
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
    p["transport"] = "serial";

    String json;
    serializeJson(doc, json);
    Serial.println(json);
}

void serial_transport_send(const char *json) {
    Serial.println(json);
}

bool serial_transport_is_active() { return _serial_transport_active; }
unsigned long serial_transport_last_status() { return _last_serial_status; }

bool serial_config_has_wifi()       { return _has_wifi; }
const char *serial_config_get_ssid() { return _ssid; }
const char *serial_config_get_pass() { return _pass; }
bool serial_config_is_setup_done()  { return _setup_done; }
void serial_config_clear()          { _has_wifi = false; _setup_done = false; }
