#pragma once

// OTA signing public key (32 bytes hex = 64 chars).
// Empty string disables signature verification (development mode).
// To enable: generate keypair on host with `vibe-pi-host ota keygen`,
// then paste the public key hex here and rebuild.
//
// Note: current impl uses HMAC-SHA256 (symmetric key, simpler). For true
// asymmetric signing, integrate libsodium or micro-ed25519.
#define OTA_PUBKEY ""
