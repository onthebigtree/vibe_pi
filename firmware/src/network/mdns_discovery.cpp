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

    for (int i = 0; i < n; i++) {
        IPAddress ip = MDNS.IP(i);
        uint16_t port = MDNS.port(i);
        String hostname = MDNS.hostname(i);

        Serial.printf("[mDNS] Service %d: %s (%s:%d)\n", i, hostname.c_str(), ip.toString().c_str(), port);

        // Skip invalid addresses
        if (ip == IPAddress(0, 0, 0, 0) || ip == IPAddress(127, 0, 0, 1)) {
            continue;
        }

        info.host = ip.toString();
        info.port = port;
        info.found = true;
        Serial.printf("[mDNS] Using host: %s:%d\n", info.host.c_str(), info.port);
        break;
    }

    if (!info.found) {
        Serial.println("[mDNS] No valid host IP found");
    }

    return info;
}
