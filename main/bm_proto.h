// main/bm_proto.h - Badge Mic wire framing (pure C, no ESP-IDF dependency).
// Adapted from the MIT-licensed Pocket Intercom oc_proto by Shinku-Chen.
// Contract: docs/development/engineering/badge-mic-protocol.md
//
// Frame: [0xA5][0x5A][TYPE][FLAGS][LEN:2 BE][payload]
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BM_PROTO_VERSION 1u

#define BM_MAGIC0 0xA5u
#define BM_MAGIC1 0x5Au
#define BM_HEADER_SIZE 6u

typedef enum {
    BM_FRAME_CONTROL   = 0x03,  // Mac -> device, JSON
    BM_FRAME_EVENT     = 0x04,  // device -> Mac, JSON
    BM_FRAME_AUDIO_OPUS = 0x05, // device -> Mac, [SEQ] + Opus packet
    BM_FRAME_PUT_DATA  = 0x07,  // Mac -> device, [OFFSET:4 BE] + bytes
} bm_frame_type_t;

#define BM_JSON_PAYLOAD_MAX 512u
#define BM_OPUS_PACKET_MAX 512u
#define BM_AUDIO_PAYLOAD_MAX (1u + BM_OPUS_PACKET_MAX)
#define BM_PUT_DATA_MAX 224u
#define BM_PUT_PAYLOAD_MAX (4u + BM_PUT_DATA_MAX)
#define BM_FRAME_MAX (BM_HEADER_SIZE + BM_AUDIO_PAYLOAD_MAX)

#define BM_TYPE_BIT(t) (1u << (t))
// What the device accepts from the Mac.
#define BM_RX_ACCEPT_MASK (BM_TYPE_BIT(BM_FRAME_CONTROL) | BM_TYPE_BIT(BM_FRAME_PUT_DATA))

bool bm_type_known(uint8_t type);
size_t bm_payload_limit(uint8_t type);  // 0 for unknown types

// Writes one frame (flags 0). Returns the frame size, or 0 on bad arguments.
size_t bm_encode(uint8_t *out, size_t out_cap, uint8_t type, const uint8_t *payload, size_t len);

typedef void (*bm_frame_cb_t)(uint8_t type, const uint8_t *payload, size_t len, void *ctx);

typedef struct {
    uint8_t buf[BM_FRAME_MAX];
    size_t len;
    size_t need;
    size_t frame_len;
    bool in_frame;
    uint32_t accept_mask;  // frame types delivered; other types are treated as misalignment
    bm_frame_cb_t cb;
    void *ctx;
    uint32_t stat_delivered;
    uint32_t stat_half_dropped;
} bm_reassembler_t;

void bm_reassembler_init(bm_reassembler_t *rx, uint32_t accept_mask, bm_frame_cb_t cb, void *ctx);
void bm_reassembler_reset(bm_reassembler_t *rx);
// Bytes may be split anywhere. A new valid CONTROL header found inside a half
// CONTROL frame discards the half frame. Binary PUT_DATA payloads are never
// scanned for headers (they may legally contain A5 5A).
void bm_reassembler_push(bm_reassembler_t *rx, const uint8_t *bytes, size_t n);
