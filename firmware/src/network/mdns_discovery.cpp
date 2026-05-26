#include "mdns_discovery.h"
#include "config.h"
#include <ESPmDNS.h>

void mdns_discovery_init() {
    if (!MDNS.begin("vibepi")) {
        Serial.println("[mDNS] Failed to start responder");
    } else {
        Serial.println("[mDNS] Responder started");
    }
}

HostInfo mdns_discover_host() {
    HostInfo info = {"", 0, false};

    Serial.println("[mDNS] Searching for Vibe Pi host agent...");

    int n = MDNS.queryService(MDNS_SERVICE, MDNS_PROTO);
    if (n <= 0) {
        Serial.println("[mDNS] No host found");
        return info;
    }

    info.host = MDNS.IP(0).toString();
    info.port = MDNS.port(0);
    info.found = true;

    Serial.printf("[mDNS] Found host: %s:%d\n", info.host.c_str(), info.port);
    return info;
}
