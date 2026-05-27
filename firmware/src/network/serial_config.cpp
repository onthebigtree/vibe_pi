#include "serial_config.h"
#include "../system/settings_manager.h"
#include "../system/i18n.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <lvgl.h>

static char _ssid[64] = "";
static char _pass[64] = "";
static bool _has_wifi = false;
static bool _setup_done = false;
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
        if (ssid && strlen(ssid) > 0) {
            strlcpy(_ssid, ssid, sizeof(_ssid));
            strlcpy(_pass, pass, sizeof(_pass));
            if (strlen(host) > 0) {
                strlcpy(settings_get().host_addr, host, sizeof(settings_get().host_addr));
                settings_get().host_port = doc["port"] | 8765;
            }
            _has_wifi = true;
            Serial.printf("{\"ok\":true,\"ssid\":\"%s\"}\n", _ssid);
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

void serial_config_loop() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (_buf.length() > 0) {
                JsonDocument doc;
                if (!deserializeJson(doc, _buf)) {
                    handle_cmd(doc);
                }
                _buf = "";
            }
        } else {
            _buf += c;
        }
    }
}

bool serial_config_has_wifi()       { return _has_wifi; }
const char *serial_config_get_ssid() { return _ssid; }
const char *serial_config_get_pass() { return _pass; }
bool serial_config_is_setup_done()  { return _setup_done; }
void serial_config_clear()          { _has_wifi = false; _setup_done = false; }
