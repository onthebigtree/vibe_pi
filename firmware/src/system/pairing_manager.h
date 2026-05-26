#pragma once

#include <Arduino.h>

enum class PairState : uint8_t {
    NOT_PAIRED,
    AWAITING_CONFIRM,
    PAIRED,
    REJECTED,
    TIMEOUT,
};

void        pairing_init();
String      pairing_generate_code();
PairState   pairing_get_state();
const char *pairing_get_code();
bool        pairing_is_paired();
void        pairing_on_confirm(const char *token, const char *hostName,
                               const char *hostAddr, uint16_t hostPort);
void        pairing_on_reject();
void        pairing_unpair();
void        pairing_loop();  // check timeout
