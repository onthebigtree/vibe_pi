#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "display/display.h"
#include "network/ws_client.h"
#include "ui/ui_manager.h"

static unsigned long lastReconnectAttempt = 0;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("[VibePi] Booting...");

    display_init();
    ui_show_splash();

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("[VibePi] Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[VibePi] WiFi connected: %s\n", WiFi.localIP().toString().c_str());
        ui_show_connecting();
        ws_client_init();
    } else {
        Serial.println("\n[VibePi] WiFi failed - entering offline mode");
        ui_show_wifi_error();
    }
}

void loop() {
    lv_timer_handler();

    if (WiFi.status() == WL_CONNECTED) {
        ws_client_loop();

        if (!ws_client_is_connected()) {
            unsigned long now = millis();
            if (now - lastReconnectAttempt > RECONNECT_INTERVAL_MS) {
                lastReconnectAttempt = now;
                ws_client_reconnect();
            }
        }
    }

    delay(5);
}
