#include "ota_manager.h"
#include "config.h"
#include "watchdog.h"
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_system.h>   // esp_restart()
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

// Verify the firmware signature over sha256(firmware) using the embedded key.
// Returns true if the signature matches. In dev builds (no -DOTA_REQUIRE_SIGNED)
// an empty key + empty signature means "signing disabled" and is allowed.
static bool verify_signature(const uint8_t *sha256_hash, const char *sig_hex) {
#ifdef OTA_REQUIRE_SIGNED
    // Production: refuse to ship without a key, and never accept an unsigned image.
    static_assert(sizeof(OTA_PUBKEY) == 65,
        "OTA_REQUIRE_SIGNED is set but OTA_PUBKEY is empty — generate one with "
        "`vibe-pi-host ota keygen` and paste the hex into ota_pubkey.h");
    if (strlen(sig_hex) == 0) {
        Serial.println("[OTA] ERROR: production build requires a signature, none provided");
        return false;
    }
#else
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
#endif
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

// ── Post-OTA rollback probation ──
// A freshly-OTA'd partition boots in ESP_OTA_IMG_PENDING_VERIFY. We do NOT
// confirm it immediately; instead it must survive OTA_PROBATION_MS of uptime.
// State lives in the partition's own ota_state (persisted by the bootloader)
// + esp_reset_reason() — so this needs no NVS of its own and is crash-safe.
static bool          probation = false;
static unsigned long probationStart = 0;

// Genuine firmware faults that should revert an unconfirmed image. Brownout is
// deliberately excluded: it's a power event, not a code regression, and would
// otherwise cause a false rollback on a flaky supply.
static bool is_firmware_fault(esp_reset_reason_t r) {
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT ||
           r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}

void ota_init() {
    strlcpy(info.current_version, FW_VERSION, sizeof(info.current_version));

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t otaState;
    if (esp_ota_get_state_partition(running, &otaState) != ESP_OK) return;

    // Not an unconfirmed OTA image (e.g. a normal USB/esptool flash) → nothing to do.
    if (otaState != ESP_OTA_IMG_PENDING_VERIFY) return;

    esp_reset_reason_t reason = esp_reset_reason();
    if (is_firmware_fault(reason)) {
        // The new image faulted before proving itself → revert to the previous one.
        Serial.printf("[OTA] New image faulted (reason=%d) before confirmation — rolling back\n",
                      (int)reason);
        Serial.flush();
        if (esp_ota_mark_app_invalid_rollback_and_reboot() != ESP_OK) {
            // No valid rollback target — don't boot-loop. Accept the image and let
            // the crash-counter safe mode take over instead.
            Serial.println("[OTA] No rollback target — confirming to avoid a boot loop");
            esp_ota_mark_app_valid_cancel_rollback();
        }
        return;  // reboots into the old partition if the rollback succeeded
    }

    // Clean boot into an unconfirmed image (the planned post-OTA reboot, or a
    // power-cycle mid-probation) → (re)start the probation window.
    probation = true;
    probationStart = millis();
    Serial.printf("[OTA] New firmware on probation — confirms after %lu ms stable uptime\n",
                  (unsigned long)OTA_PROBATION_MS);
}

// Call every loop. Confirms the running image once it has been stable long
// enough, which cancels the pending rollback. Cheap no-op when not on probation.
void ota_confirm_tick() {
    if (!probation) return;
    if (millis() - probationStart < OTA_PROBATION_MS) return;
    probation = false;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        Serial.println("[OTA] New firmware confirmed healthy — rollback canceled");
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
    // Without this reboot the device keeps running the OLD image forever and the
    // freshly-flashed partition is never booted. (Tier 2 will report ota_done to
    // the host here before rebooting.)
    Serial.flush();
    delay(1500);          // let the SUCCESS state render + serial drain
    esp_restart();        // boots into the new partition; ESP_OTA_IMG_PENDING_VERIFY confirmed in ota_init()
    return true;          // not reached
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
