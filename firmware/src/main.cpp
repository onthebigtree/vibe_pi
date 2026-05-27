#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "display/display.h"
#include "network/wifi_provision.h"
#include "network/mdns_discovery.h"
#include "network/ws_client.h"
#include "system/i18n.h"
#include "system/settings_manager.h"
#include "system/health_manager.h"
#include "system/reset_manager.h"
#include "system/power_manager.h"
#include "system/pairing_manager.h"
#include "system/ota_manager.h"
#include "system/watchdog.h"
#include "ui/ui_manager.h"
#include "ui/oobe_flow.h"

// ── Application State Machine ──
enum class AppState {
    SELF_TEST,
    OOBE,
    CONNECTING_WIFI,
    WIFI_FAILED,
    DISCOVERING,
    CONNECTING_WS,
    PAIRING,
    RUNNING,
    RECONNECTING,
    SAFE_MODE,
};

static AppState appState = AppState::SELF_TEST;
static HostInfo hostInfo = {"", 0, false};
static unsigned long stateEnteredAt = 0;
static unsigned long lastHealthReport = 0;
static unsigned long buttonPressStart = 0;
static bool buttonHeld = false;
static char deviceId[20] = "";

static void set_state(AppState s) {
    appState = s;
    stateEnteredAt = millis();
    power_register_activity();
}

static unsigned long state_elapsed() {
    return millis() - stateEnteredAt;
}

static void build_device_id() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(deviceId, sizeof(deviceId), "vibepi-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

// ── Button ──
static void handle_button() {
    bool pressed = (digitalRead(BUTTON_PIN) == LOW);

    if (pressed && !buttonHeld) {
        buttonPressStart = millis();
        buttonHeld = true;
    }

    if (!pressed && buttonHeld) {
        unsigned long duration = millis() - buttonPressStart;
        buttonHeld = false;
        power_register_activity();

        if (duration > 10000) {
            // 10s hold → factory reset
            reset_execute(ResetLevel::FACTORY_RESET, true);
        } else if (duration > 3000) {
            // 3s hold → network reset
            reset_execute(ResetLevel::NETWORK_RESET, true);
        } else if (duration > 100) {
            // Short press → wake / cycle page
            if (power_get_state() != PowerState::ACTIVE) {
                power_force_state(PowerState::ACTIVE);
            } else if (appState == AppState::RUNNING) {
                int next = (ui_get_current_page() + 1) % ui_get_page_count();
                ui_set_page(next);
            }
        }
    }
}

// ── WiFi scan callback for OOBE ──
static void check_wifi_scan_result() {
    int16_t result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) return;
    if (result >= 0) {
        oobe_on_wifi_scan_done(result);
    }
}

static void check_wifi_connect_result() {
    if (WiFi.status() == WL_CONNECTED) {
        oobe_on_wifi_connected(WiFi.localIP().toString().c_str());
    }
}

// ═══════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\n[VibePi] Boot v%s (protocol v%d)\n", FW_VERSION, PROTOCOL_VERSION);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Watchdog
    watchdog_init(WATCHDOG_TIMEOUT_MS);

    // Init systems (order matters)
    settings_init();
    health_init();
    ota_init();

    display_init();
    ui_init();

    build_device_id();
    Serial.printf("[VibePi] Device ID: %s\n", deviceId);

    // Self-test
    set_state(AppState::SELF_TEST);
    ui_show_boot();
    lv_timer_handler();
}

void loop() {
    watchdog_feed();
    lv_timer_handler();
    handle_button();

    switch (appState) {

    // ── SELF TEST ──
    case AppState::SELF_TEST: {
        SelfTestResult result = health_run_self_test();

        if (health_is_safe_mode()) {
            Serial.println("[App] Entering SAFE MODE");
            set_state(AppState::SAFE_MODE);
            ui_show_safe_mode();
            break;
        }

        if (result != SelfTestResult::PASS) {
            Serial.printf("[App] Self-test failed: %d\n", (int)result);
            // Continue anyway for non-critical failures
        }

        pairing_init();
        power_init();

        if (!settings_get().oobe_done) {
            set_state(AppState::OOBE);
            oobe_init();
            oobe_show();
        } else {
            set_state(AppState::CONNECTING_WIFI);
            ui_show_connecting_wifi();
        }
        break;
    }

    // ── OOBE (first-run wizard) ──
    case AppState::OOBE: {
        oobe_loop();
        check_wifi_scan_result();

        if (oobe_get_step() == OobeStep::WIFI_CONNECTING) {
            if (WiFi.status() == WL_CONNECTED) {
                oobe_on_wifi_connected(WiFi.localIP().toString().c_str());
                mdns_discovery_init();

                // Auto-discover and connect for pairing
                hostInfo = mdns_discover_host();
                if (hostInfo.found) {
                    ws_client_init(hostInfo.host.c_str(), hostInfo.port);
                }
            } else if (state_elapsed() > WIFI_CONNECT_TIMEOUT_MS) {
                oobe_on_wifi_failed();
            }
        }

        if (oobe_is_finished()) {
            if (pairing_is_paired()) {
                set_state(AppState::RUNNING);
                ui_show_dashboard();
            } else {
                set_state(AppState::CONNECTING_WIFI);
                ui_show_connecting_wifi();
            }
        }
        break;
    }

    // ── CONNECTING WIFI ──
    case AppState::CONNECTING_WIFI: {
        provision_init();
        if (provision_is_configured()) {
            if (provision_connect_saved()) {
                Serial.printf("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
                mdns_discovery_init();
                set_state(AppState::DISCOVERING);
                ui_show_discovering();
            } else {
                set_state(AppState::WIFI_FAILED);
                ui_show_wifi_failed();
            }
        } else {
            // No WiFi config — go to OOBE
            set_state(AppState::OOBE);
            oobe_init();
            oobe_show();
        }
        break;
    }

    // ── WIFI FAILED ──
    case AppState::WIFI_FAILED:
        if (state_elapsed() > 10000) {
            set_state(AppState::CONNECTING_WIFI);
            ui_show_connecting_wifi();
        }
        break;

    // ── DISCOVERING HOST ──
    case AppState::DISCOVERING: {
        DeviceSettings &s = settings_get();

        // If we have a saved host address, try it directly
        if (strlen(s.host_addr) > 0 && !hostInfo.found) {
            hostInfo.host = String(s.host_addr);
            hostInfo.port = s.host_port;
            hostInfo.found = true;
            Serial.printf("[App] Using saved host: %s:%d\n", s.host_addr, s.host_port);
        }

        if (!hostInfo.found) {
            hostInfo = mdns_discover_host();
        }

        if (hostInfo.found) {
            set_state(AppState::CONNECTING_WS);
            ws_client_init(hostInfo.host.c_str(), hostInfo.port);
        } else if (state_elapsed() > 5000) {
            stateEnteredAt = millis(); // retry
        }
        break;
    }

    // ── CONNECTING WS ──
    case AppState::CONNECTING_WS:
        ws_client_loop();
        if (ws_client_is_connected()) {
            if (!pairing_is_paired()) {
                set_state(AppState::PAIRING);
                // Pairing code already generated during OOBE or generate now
                if (strlen(pairing_get_code()) == 0) {
                    pairing_generate_code();
                }
                ui_show_pairing(pairing_get_code());
            } else {
                set_state(AppState::RUNNING);
                ui_show_dashboard();
                Serial.println("[App] Connected and paired — RUNNING");
            }
        } else if (state_elapsed() > 15000) {
            set_state(AppState::DISCOVERING);
            hostInfo.found = false;
            ui_show_discovering();
        }
        break;

    // ── PAIRING ──
    case AppState::PAIRING:
        ws_client_loop();
        pairing_loop();

        if (pairing_is_paired()) {
            set_state(AppState::RUNNING);
            ui_show_dashboard();
        } else if (pairing_get_state() == PairState::TIMEOUT) {
            pairing_generate_code();
            ui_show_pairing(pairing_get_code());
        }
        break;

    // ── RUNNING ──
    case AppState::RUNNING:
        ws_client_loop();
        power_loop();

        if (!ws_client_is_connected()) {
            set_state(AppState::RECONNECTING);
            ui_show_reconnecting();
            break;
        }

        // Periodic health report
        if (millis() - lastHealthReport > HEALTH_REPORT_INTERVAL_MS) {
            lastHealthReport = millis();
            JsonDocument doc;
            health_build_report(doc, deviceId);
            ws_client_send_json(doc);
        }
        break;

    // ── RECONNECTING ──
    case AppState::RECONNECTING:
        ws_client_loop();
        power_loop();

        if (ws_client_is_connected()) {
            set_state(AppState::RUNNING);
            ui_show_dashboard();
        } else if (state_elapsed() > 30000) {
            set_state(AppState::DISCOVERING);
            hostInfo.found = false;
            ui_show_discovering();
        }
        break;

    // ── SAFE MODE ──
    case AppState::SAFE_MODE:
        // Minimal operation — only handle button for factory reset
        break;
    }

    delay(5);
}
