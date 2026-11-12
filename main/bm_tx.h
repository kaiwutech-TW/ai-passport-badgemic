// main/bm_tx.h - uplink audio packing rules (pure C).
//
// The Mac releases the dictation hotkey when nothing arrives for 2 s, so during a
// turn EVERY 60 ms frame is sent (DTX / silence packets included), SEQ stays
// contiguous, and if the encoder stalls a tiny DTX packet is synthesized at least
// every BM_TX_KEEPALIVE_MS.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bm_proto.h"

#define BM_TX_KEEPALIVE_MS 500u
#define BM_TX_DEFAULT_TOC 0x58u   // SILK-only, wideband, 60 ms, one frame

typedef struct {
    uint8_t seq;
    uint8_t toc;           // TOC of the last real packet (mode/bandwidth/frame size)
    uint32_t last_ms;      // last time a frame was packed
    uint32_t packed;
} bm_tx_t;

void bm_tx_begin(bm_tx_t *t, uint32_t now_ms);
// Builds an AUDIO_OPUS payload [SEQ][packet] into out (>= 1 + BM_OPUS_PACKET_MAX).
// len == 0 (or too long) yields a 1-byte TOC-only DTX packet. SEQ always advances,
// even if the caller's send later fails, so loss shows up as a gap. Returns the
// payload length (>= 2).
size_t bm_tx_pack(bm_tx_t *t, const uint8_t *opus, size_t len, uint32_t now_ms, uint8_t *out);
bool bm_tx_keepalive_due(const bm_tx_t *t, uint32_t now_ms);
