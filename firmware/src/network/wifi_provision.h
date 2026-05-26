#pragma once

#include <Arduino.h>

enum class ProvisionState {
    IDLE,
    AP_ACTIVE,
    CONNECTING,
    CONNECTED,
    FAILED
};

void provision_init();
void provision_start_ap();
void provision_loop();
bool provision_is_configured();
bool provision_connect_saved();
ProvisionState provision_get_state();
String provision_get_ap_ssid();
String provision_get_ip();
void provision_reset();
