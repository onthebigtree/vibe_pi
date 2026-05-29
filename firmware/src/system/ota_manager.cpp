#include "ota_manager.h"
#include "config.h"
#include "watchdog.h"
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include "ota_pubkey.h"

// Decode hex string to bytes; returns true on success
static bool hex_decode(const char *hex, uint8_t *out, size_t out_len) {
    if (!hex || strlen(hex) != out_len * 2) return false;
    for (size_t i = 0; i < out_len; i++) {
        char c1 = hex[i*2], c2 = hex[i*2 + 1];
        auto val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int v1 = val(c1), v2 = val(c2);
        if (v1 < 0 || v2 < 0) return false;
        out[i] = (uint8_t)((v1 << 4) | v2);
    }
    return true;
}

// Verify Ed25519 signature of sha256(firmware) using embedded public key
// Returns true if signature matches OR if signing is disabled (empty sig + no key)
static bool verify_signature(const uint8_t *sha256_hash, const char *sig_hex) {
    // No signature provided AND no embedded key → signing disabled, allow update
    if (strlen(sig_hex) == 0 && OTA_PUBKEY[0] == 0) {
        Serial.println("[OTA] Signing disabled (no key, no signature)");
        return true;
    }
    // Embedded key present but no signature → reject
    if (strlen(sig_hex) == 0) {
        Serial.println("[OTA] ERROR: signing key present but signature missing");
        return false;
    }
    uint8_t sig[64], pubkey[32];
    if (!hex_decode(sig_hex, sig, 64)) {
        Serial.println("[OTA] ERROR: invalid signature hex");
        return false;
    }
    if (!hex_decode(OTA_PUBKEY, pubkey, 32)) {
        Serial.println("[OTA] ERROR: invalid embedded pubkey hex");
        return false;
    }
    // Ed25519 verify is not in mbedtls by default — use libsodium-style check
    // For now: HMAC-SHA256 fallback (treat OTA_PUBKEY as HMAC key, sig = HMAC(sha256))
    // TODO: integrate libsodium or micro-ed25519 for true Ed25519 when needed
    #include <mbedtls/md.h>
    uint8_t computed[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1) != 0) {
        mbedtls_md_free(&ctx);
        return false;
    }
    mbedtls_md_hmac_starts(&ctx, pubkey, 32);
    mbedtls_md_hmac_update(&ctx, sha256_hash, 32);
    mbedtls_md_hmac_finish(&ctx, computed);
    mbedtls_md_free(&ctx);
    // Compare first 32 bytes of sig (we only need 32 for HMAC-SHA256)
    bool match = (memcmp(computed, sig, 32) == 0);
    Serial.printf("[OTA] HMAC-SHA256 signature: %s\n", match ? "VALID" : "INVALID");
    return match;
}

static OtaState state = OtaState::IDLE;
static OtaInfo info;
static uint8_t progressPct = 0;
static char errorMsg[64] = "";

void ota_init() {
    strlcpy(info.current_version, FW_VERSION, sizeof(info.current_version));

    // Check if previous OTA boot was successful
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t otaState;
    if (esp_ota_get_state_partition(running, &otaState) == ESP_OK) {
        if (otaState == ESP_OTA_IMG_PENDING_VERIFY) {
            Serial.println("[OTA] Confirming new firmware boot...");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
}

void ota_on_available(const JsonObject &p) {
    strlcpy(info.version, p["version"] | "", sizeof(info.version));
    info.size_bytes = p["size_bytes"] | 0;
    strlcpy(info.sha256, p["sha256"] | "", sizeof(info.sha256));
    strlcpy(info.signature, p["signature"] | "", sizeof(info.signature));
    strlcpy(info.changelog, p["changelog"] | "", sizeof(info.changelog));
    strlcpy(info.changelog_zh, p["changelog_zh"] | "", sizeof(info.changelog_zh));
    strlcpy(info.url, p["url"] | "", sizeof(info.url));
    info.force = p["force"] | false;

    state = OtaState::AVAILABLE;
    progressPct = 0;
    errorMsg[0] = '\0';
    Serial.printf("[OTA] Update available: %s → %s (%u bytes)\n",
                  FW_VERSION, info.version, info.size_bytes);
}

void ota_on_start(const JsonObject &p) {
    strlcpy(info.url, p["url"] | info.url, sizeof(info.url));
    strlcpy(info.sha256, p["sha256"] | info.sha256, sizeof(info.sha256));
    strlcpy(info.signature, p["signature"] | info.signature, sizeof(info.signature));
    ota_start_download();
}

bool ota_start_download() {
    if (strlen(info.url) == 0) {
        strlcpy(errorMsg, "no_url", sizeof(errorMsg));
        state = OtaState::FAILED;
        return false;
    }

    state = OtaState::DOWNLOADING;
    progressPct = 0;

    HTTPClient http;
    http.setTimeout(OTA_HTTP_TIMEOUT_MS);
    http.begin(info.url);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        snprintf(errorMsg, sizeof(errorMsg), "http_%d", httpCode);
        state = OtaState::FAILED;
        http.end();
        return false;
    }

    int contentLen = http.getSize();
    if (contentLen <= 0) {
        strlcpy(errorMsg, "empty_response", sizeof(errorMsg));
        state = OtaState::FAILED;
        http.end();
        return false;
    }

    if (!Update.begin(contentLen, U_FLASH)) {
        strlcpy(errorMsg, "update_begin_fail", sizeof(errorMsg));
        state = OtaState::FAILED;
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);

    uint8_t buf[OTA_CHUNK_SIZE];
    int totalRead = 0;
    unsigned long downloadStart = millis();
    unsigned long lastProgressMs = downloadStart;

    while (http.connected() && totalRead < contentLen) {
        watchdog_feed();  // long download must not trip the task watchdog

        // Abort if the whole transfer takes too long, or stalls with no new
        // bytes — http.setTimeout() only covers the initial request, not the
        // body, so a server that sends headers then hangs would loop forever.
        unsigned long now = millis();
        if (now - downloadStart > OTA_DOWNLOAD_TIMEOUT_MS ||
            now - lastProgressMs > OTA_STALL_TIMEOUT_MS) {
            Update.abort();
            strlcpy(errorMsg, "download_timeout", sizeof(errorMsg));
            state = OtaState::FAILED;
            http.end();
            return false;
        }

        int available = stream->available();
        if (available <= 0) { delay(1); continue; }

        int readLen = stream->readBytes(buf, min(available, (int)sizeof(buf)));
        if (readLen <= 0) break;

        Update.write(buf, readLen);
        mbedtls_sha256_update(&sha_ctx, buf, readLen);
        totalRead += readLen;
        lastProgressMs = now;
        progressPct = (uint8_t)((totalRead * 100L) / contentLen);
    }

    http.end();

    // The loop can exit cleanly (http.connected() false) with an incomplete
    // body. Don't hash/install a truncated image — fail explicitly.
    if (totalRead < contentLen) {
        Update.abort();
        snprintf(errorMsg, sizeof(errorMsg), "incomplete_%d/%d", totalRead, contentLen);
        state = OtaState::FAILED;
        return false;
    }

    // Verify SHA256
    state = OtaState::VERIFYING;
    unsigned char hash[32];
    mbedtls_sha256_finish(&sha_ctx, hash);
    mbedtls_sha256_free(&sha_ctx);

    char hashHex[65];
    for (int i = 0; i < 32; i++) {
        sprintf(hashHex + i * 2, "%02x", hash[i]);
    }
    hashHex[64] = '\0';

    if (strlen(info.sha256) > 0 && strcmp(hashHex, info.sha256) != 0) {
        Update.abort();
        strlcpy(errorMsg, "sha256_mismatch", sizeof(errorMsg));
        state = OtaState::FAILED;
        Serial.printf("[OTA] SHA256 mismatch: expected=%s got=%s\n", info.sha256, hashHex);
        return false;
    }

    // Verify signature (HMAC-SHA256 placeholder for full Ed25519)
    if (!verify_signature(hash, info.signature)) {
        Update.abort();
        strlcpy(errorMsg, "signature_invalid", sizeof(errorMsg));
        state = OtaState::FAILED;
        return false;
    }

    state = OtaState::INSTALLING;
    if (!Update.end(true)) {
        strlcpy(errorMsg, "update_end_fail", sizeof(errorMsg));
        state = OtaState::FAILED;
        return false;
    }

    state = OtaState::SUCCESS;
    Serial.printf("[OTA] Update to %s successful, rebooting...\n", info.version);
    return true;
}

void ota_loop() {
    // OTA download runs synchronously in ota_start_download()
    // This is reserved for future async implementation
}

OtaState    ota_get_state()    { return state; }
OtaInfo    &ota_get_info()     { return info; }
uint8_t     ota_get_progress() { return progressPct; }
const char *ota_get_error()    { return errorMsg; }

void ota_reset() {
    state = OtaState::IDLE;
    progressPct = 0;
    errorMsg[0] = '\0';
}
