#include "pairing_manager.h"
#include "config.h"
#include "settings_manager.h"
#include <esp_random.h>

static PairState state = PairState::NOT_PAIRED;
static char pairCode[7] = "";
static unsigned long codeGeneratedAt = 0;

void pairing_init() {
    DeviceSettings &s = settings_get();
    if (s.paired && strlen(s.pair_token) > 0) {
        state = PairState::PAIRED;
        Serial.printf("[Pairing] Already paired to: %s\n", s.host_name);
    } else {
        state = PairState::NOT_PAIRED;
    }
}

String pairing_generate_code() {
    uint32_t code = esp_random() % 1000000;
    snprintf(pairCode, sizeof(pairCode), "%06lu", (unsigned long)code);
    state = PairState::AWAITING_CONFIRM;
    codeGeneratedAt = millis();
    Serial.printf("[Pairing] Code generated: %s\n", pairCode);
    return String(pairCode);
}

PairState pairing_get_state() { return state; }

const char *pairing_get_code() { return pairCode; }

bool pairing_is_paired() { return state == PairState::PAIRED; }

void pairing_on_confirm(const char *token, const char *hostName,
                        const char *hostAddr, uint16_t hostPort) {
    state = PairState::PAIRED;
    settings_save_pairing(true, token, hostName, hostAddr, hostPort);
    Serial.printf("[Pairing] Paired to %s (%s:%d)\n", hostName, hostAddr, hostPort);
}

void pairing_on_reject() {
    state = PairState::REJECTED;
    pairCode[0] = '\0';
    Serial.println("[Pairing] Rejected by host");
}

void pairing_unpair() {
    state = PairState::NOT_PAIRED;
    pairCode[0] = '\0';
    settings_save_pairing(false, "", "", "", WS_DEFAULT_PORT);
    Serial.println("[Pairing] Unpaired");
}

void pairing_loop() {
    if (state == PairState::AWAITING_CONFIRM) {
        if (millis() - codeGeneratedAt > PAIR_CODE_TIMEOUT_MS) {
            state = PairState::TIMEOUT;
            pairCode[0] = '\0';
            Serial.println("[Pairing] Code expired");
        }
    }
}
