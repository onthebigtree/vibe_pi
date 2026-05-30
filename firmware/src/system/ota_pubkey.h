#pragma once

// OTA signing key (32 bytes hex = 64 chars).
// Empty string disables signature verification (DEVELOPMENT MODE ONLY).
// To enable: generate a key on host with `vibe-pi-host ota keygen`,
// then paste the hex here and rebuild.
//
// PRODUCTION: build with -DOTA_REQUIRE_SIGNED (see platformio.ini). That turns
// an empty key into a COMPILE ERROR and forbids the unsigned-update path at
// runtime — so a shippable image can never accept an unverified firmware.
//
// Note: current impl uses HMAC-SHA256 (symmetric key, simpler). For true
// asymmetric signing, integrate libsodium or micro-ed25519.
#define OTA_PUBKEY ""
