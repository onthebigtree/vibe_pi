#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
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
#include "ui/notification.h"
#include "network/serial_config.h"

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

// ═══════════════════════════════════════════════════════════════
void setup() {
    Serial.setRxBufferSize(2048);  // default 256B too small for status msgs
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
    serial_config_init();

    display_init();
    ui_init();

    build_device_id();
    Serial.printf("[VibePi] Device ID: %s\n", deviceId);

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    set_state(AppState::SELF_TEST);
    Serial.println("[App] Setup complete, entering loop");
}

static uint32_t loopCount = 0;

void loop() {
    loopCount++;
    watchdog_feed();
    handle_button();
    serial_config_loop();
    display_update_touch();
    notif_loop();

    // State machine runs BEFORE rendering to avoid blocking on UI transitions
    // lv_timer_handler() runs AFTER state machine so it renders the final state

    switch (appState) {

    // ── SELF TEST ──
    case AppState::SELF_TEST: {
        SelfTestResult result = health_run_self_test();

        // Safe mode is auto-cleared in health_init()

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

    // ── OOBE (web-based setup via USB serial) ──
    case AppState::OOBE: {
        oobe_loop();

        if (oobe_is_finished()) {
            if (WiFi.status() == WL_CONNECTED) {
                mdns_discovery_init();
                set_state(AppState::DISCOVERING);
                ui_show_discovering();
            } else {
                set_state(AppState::CONNECTING_WIFI);
                ui_show_connecting_wifi();
            }
        }
        break;
    }

    // ── CONNECTING WIFI ──
    case AppState::CONNECTING_WIFI: {
        // Serial transport can work without WiFi
        if (serial_transport_is_active()) {
            Serial.println("[App] Serial active — running without WiFi");
            set_state(AppState::RUNNING);
            ui_show_dashboard();
            break;
        }

        Serial.println("[App] CONNECTING_WIFI...");
        provision_init();
        if (provision_is_configured()) {
            if (provision_connect_saved()) {
                Serial.printf("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
                mdns_discovery_init();
                set_state(AppState::DISCOVERING);
                ui_show_discovering();
            } else {
                // WiFi failed but serial might be available
                if (serial_transport_is_active()) {
                    Serial.println("[App] WiFi failed, using serial transport");
                    set_state(AppState::RUNNING);
                    ui_show_dashboard();
                } else {
                    set_state(AppState::WIFI_FAILED);
                    ui_show_wifi_failed();
                }
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
        // Skip WS discovery if serial transport is active
        if (serial_transport_is_active()) {
            Serial.println("[App] Serial transport active — skipping WS discovery");
            set_state(AppState::RUNNING);
            ui_show_dashboard();
            break;
        }

        DeviceSettings &s = settings_get();

        static int discoverAttempt = 0;

        // 1. Saved host address (always try first)
        if (strlen(s.host_addr) > 0) {
            hostInfo.host = String(s.host_addr);
            hostInfo.port = s.host_port;
            hostInfo.found = true;
            Serial.printf("[App] Using saved host: %s:%d\n", s.host_addr, s.host_port);
        }

        // 2. mDNS disabled — unreliable on many networks (returns 0.0.0.0)
        // if (!hostInfo.found && discoverAttempt < 2) {
        //     hostInfo = mdns_discover_host();
        // }

        // 3. Subnet scan — cycle through common host IPs
        if (!hostInfo.found) {
            IPAddress local = WiFi.localIP();
            if (local != IPAddress(0,0,0,0)) {
                const int candidates[] = {250, 1, 100, 200, 50};
                int idx = discoverAttempt % 5;
                char buf[20];
                snprintf(buf, sizeof(buf), "%d.%d.%d.%d", local[0], local[1], local[2], candidates[idx]);
                hostInfo.host = String(buf);
                hostInfo.port = WS_DEFAULT_PORT;
                hostInfo.found = true;
                Serial.printf("[App] Trying subnet: %s:%d\n", buf, WS_DEFAULT_PORT);
            }
        }

        discoverAttempt++;

        if (hostInfo.found) {
            set_state(AppState::CONNECTING_WS);
            ws_client_init(hostInfo.host.c_str(), hostInfo.port);
        }
        break;
    }

    // ── CONNECTING WS ──
    case AppState::CONNECTING_WS:
        ws_client_loop();
        if (serial_transport_is_active()) {
            Serial.println("[App] Serial transport active — RUNNING");
            set_state(AppState::RUNNING);
            ui_show_dashboard();
        } else if (ws_client_is_connected()) {
            set_state(AppState::RUNNING);
            ui_show_dashboard();
            Serial.println("[App] WS connected — RUNNING");
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
        if (serial_transport_is_active()) {
            // Serial transport mode — no WS needed
            power_loop();
            if (millis() - serial_transport_last_status() > STATUS_STALE_TIMEOUT_MS * 2
                && serial_transport_last_status() > 0) {
                // Serial link stale
            }
        } else {
            ws_client_loop();
            power_loop();
            if (!ws_client_is_connected()) {
                set_state(AppState::RECONNECTING);
                ui_show_reconnecting();
                break;
            }
        }

        // Periodic health report
        if (millis() - lastHealthReport > HEALTH_REPORT_INTERVAL_MS) {
            lastHealthReport = millis();
            JsonDocument doc;
            health_build_report(doc, deviceId);
            if (serial_transport_is_active()) {
                String json;
                serializeJson(doc, json);
                serial_transport_send(json.c_str());
            } else {
                ws_client_send_json(doc);
            }
        }
        break;

    // ── RECONNECTING ──
    case AppState::RECONNECTING:
        ws_client_loop();
        power_loop();

        // Serial transport can activate at any time
        if (serial_transport_is_active()) {
            Serial.println("[App] Serial transport active — switching to RUNNING");
            set_state(AppState::RUNNING);
            ui_show_dashboard();
        } else if (ws_client_is_connected()) {
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
        break;
    }

    lv_timer_handler();

    // Heartbeat: prove main loop is alive, report state + free heap + touch cb count
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 3000) {
        lastHeartbeat = millis();
        Serial.printf("[HB] state=%d loop=%lu heap=%lu serial=%d touch_cb=%lu touch_press=%lu\n",
                     (int)appState, loopCount,
                     ESP.getFreeHeap(),
                     serial_transport_is_active() ? 1 : 0,
                     display_get_touch_cb_count(),
                     display_get_touch_press_count());
    }

    delay(5);
}
