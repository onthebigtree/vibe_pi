#include "serial_config.h"
#include "../system/settings_manager.h"
#include "../system/i18n.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>

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
    else if (strcmp(cmd, "wifi") == 0) {
        const char *ssid = doc["ssid"];
        const char *pass = doc["pass"] | "";
        if (ssid && strlen(ssid) > 0) {
            strlcpy(_ssid, ssid, sizeof(_ssid));
            strlcpy(_pass, pass, sizeof(_pass));
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
