#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <Update.h>

#include "config.h"
#include "display/display.h"
#include "network/wifi_provision.h"
#include "network/mdns_discovery.h"
#include "network/ws_client.h"
#include "ui/ui_manager.h"

// ── Application states ──
enum class AppState {
    BOOT,
    PROVISION,
    CONNECTING_WIFI,
    WIFI_FAILED,
    DISCOVERING,
    CONNECTING_WS,
    RUNNING,
    RECONNECTING,
};

static AppState appState = AppState::BOOT;
static HostInfo hostInfo = {"", 0, false};
static unsigned long stateEnteredAt = 0;
static unsigned long lastActivityTime = 0;
static unsigned long buttonPressStart = 0;
static bool buttonHeld = false;

static void set_state(AppState newState) {
    appState = newState;
    stateEnteredAt = millis();
    lastActivityTime = millis();
}

static unsigned long state_elapsed() {
    return millis() - stateEnteredAt;
}

// ── Button handling ──
static void handle_button() {
    bool pressed = (digitalRead(BUTTON_PIN) == LOW);

    if (pressed && !buttonHeld) {
        buttonPressStart = millis();
        buttonHeld = true;
    }

    if (!pressed && buttonHeld) {
        unsigned long duration = millis() - buttonPressStart;
        buttonHeld = false;

        if (duration > 5000) {
            // Long hold: factory reset
            Serial.println("[Button] Factory reset triggered");
            provision_reset();
            ESP.restart();
        } else if (duration > 100) {
            // Short press: wake display or cycle page
            if (ui_is_sleeping()) {
                ui_wake();
            } else if (appState == AppState::RUNNING) {
                int next = (ui_get_current_page() + 1) % ui_get_page_count();
                ui_set_page(next);
            }
        }
        lastActivityTime = millis();
    }
}

// ── Display sleep management ──
static void check_sleep() {
    if (appState != AppState::RUNNING) return;
    if (ui_is_sleeping()) return;

    if (millis() - lastActivityTime > DISPLAY_SLEEP_TIMEOUT_MS) {
        ui_sleep();
    }
}

// ═══════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\n[VibePi] Booting v%s\n", FW_VERSION);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Watchdog: 30s timeout
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = 30000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdt_cfg);
    esp_task_wdt_add(nullptr);

    display_init();
    ui_init();
    ui_show_boot();
    lv_timer_handler();

    provision_init();

    delay(1500); // show boot splash

    if (provision_is_configured()) {
        set_state(AppState::CONNECTING_WIFI);
        ui_show_connecting_wifi();
    } else {
        set_state(AppState::PROVISION);
        provision_start_ap();
        ui_show_provision(provision_get_ap_ssid().c_str(),
                          WiFi.softAPIP().toString().c_str());
    }
}

void loop() {
    esp_task_wdt_reset();
    lv_timer_handler();
    handle_button();
    check_sleep();

    switch (appState) {

        case AppState::PROVISION:
            provision_loop();
            break;

        case AppState::CONNECTING_WIFI:
            if (provision_connect_saved()) {
                Serial.printf("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
                mdns_discovery_init();
                set_state(AppState::DISCOVERING);
                ui_show_discovering();
            } else {
                Serial.println("[WiFi] Connection failed");
                set_state(AppState::WIFI_FAILED);
                ui_show_wifi_failed();
            }
            break;

        case AppState::WIFI_FAILED:
            // After 10s, retry. Long button press resets.
            if (state_elapsed() > 10000) {
                set_state(AppState::CONNECTING_WIFI);
                ui_show_connecting_wifi();
            }
            break;

        case AppState::DISCOVERING:
            hostInfo = mdns_discover_host();
            if (hostInfo.found) {
                set_state(AppState::CONNECTING_WS);
                ws_client_init(hostInfo.host.c_str(), hostInfo.port);
            } else {
                // Retry discovery every 3s
                if (state_elapsed() > 3000) {
                    stateEnteredAt = millis();
                }
            }
            break;

        case AppState::CONNECTING_WS:
            ws_client_loop();
            if (ws_client_is_connected()) {
                Serial.println("[App] Fully connected — entering run mode");
                set_state(AppState::RUNNING);
            } else if (state_elapsed() > 15000) {
                Serial.println("[App] WS connect timeout, re-discovering");
                set_state(AppState::DISCOVERING);
                ui_show_discovering();
            }
            break;

        case AppState::RUNNING:
            ws_client_loop();

            if (!ws_client_is_connected()) {
                set_state(AppState::RECONNECTING);
                ui_show_reconnecting();
                break;
            }

            // Check for stale data
            if (ws_client_last_status_time() > 0 &&
                millis() - ws_client_last_status_time() > STATUS_STALE_TIMEOUT_MS) {
                // Could show "stale" indicator on UI
            }
            break;

        case AppState::RECONNECTING:
            ws_client_loop();
            if (ws_client_is_connected()) {
                set_state(AppState::RUNNING);
                ui_show_dashboard();
            } else if (state_elapsed() > 30000) {
                // After 30s reconnect failure, re-discover
                set_state(AppState::DISCOVERING);
                ui_show_discovering();
            }
            break;

        default:
            break;
    }

    delay(5);
}
