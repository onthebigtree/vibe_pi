#include "ota_manager.h"
#include "config.h"
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

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

    while (http.connected() && totalRead < contentLen) {
        int available = stream->available();
        if (available <= 0) { delay(1); continue; }

        int readLen = stream->readBytes(buf, min(available, (int)sizeof(buf)));
        if (readLen <= 0) break;

        Update.write(buf, readLen);
        mbedtls_sha256_update(&sha_ctx, buf, readLen);
        totalRead += readLen;
        progressPct = (uint8_t)((totalRead * 100L) / contentLen);
    }

    http.end();

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
