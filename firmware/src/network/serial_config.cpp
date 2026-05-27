#include "serial_config.h"
#include <ArduinoJson.h>
#include <WiFi.h>

static char _ssid[64] = "";
static char _pass[64] = "";
static bool _has_wifi = false;
static String _serial_buf;

void serial_config_init() {
    _serial_buf.reserve(256);
}

void serial_config_loop() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (_serial_buf.length() > 0) {
                // Try to parse JSON
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, _serial_buf);
                if (!err) {
                    const char *cmd = doc["cmd"];
                    if (cmd && strcmp(cmd, "wifi") == 0) {
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
                    } else if (cmd && strcmp(cmd, "ping") == 0) {
                        Serial.println("{\"ok\":true,\"device\":\"vibepi\",\"version\":\"0.2.0\"}");
                    } else if (cmd && strcmp(cmd, "scan") == 0) {
                        Serial.println("{\"ok\":true,\"scanning\":true}");
                        int n = WiFi.scanNetworks();
                        Serial.print("{\"ok\":true,\"networks\":[");
                        for (int i = 0; i < n; i++) {
                            if (i > 0) Serial.print(",");
                            Serial.printf("{\"ssid\":\"%s\",\"rssi\":%d}", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
                        }
                        Serial.println("]}");
                        WiFi.scanDelete();
                    }
                }
                _serial_buf = "";
            }
        } else {
            _serial_buf += c;
        }
    }
}

bool serial_config_has_wifi()       { return _has_wifi; }
const char *serial_config_get_ssid() { return _ssid; }
const char *serial_config_get_pass() { return _pass; }
void serial_config_clear()          { _has_wifi = false; _ssid[0] = '\0'; _pass[0] = '\0'; }
