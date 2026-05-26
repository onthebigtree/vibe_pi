#pragma once

#include <Arduino.h>

struct HostInfo {
    String host;
    uint16_t port;
    bool found;
};

void mdns_discovery_init();
HostInfo mdns_discover_host();
