// main/bm_ring.h - fixed-capacity frame ring used as the PTT pre-roll buffer
// (pure C). Pushing into a full ring drops the oldest frame. Not thread safe:
// the device wrapper serializes access.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BM_RING_SLOTS 8u        // 8 x 60 ms = 480 ms >= 300 ms threshold + latency
#define BM_RING_FRAME_MAX 512u

typedef struct {
    uint16_t len[BM_RING_SLOTS];
    uint8_t data[BM_RING_SLOTS][BM_RING_FRAME_MAX];
    uint8_t head;   // next pop
    uint8_t count;
    uint32_t overwritten;  // frames dropped because the ring was full
} bm_ring_t;

void bm_ring_clear(bm_ring_t *r);
// Returns false (frame ignored) when len is 0 or too large.
bool bm_ring_push(bm_ring_t *r, const uint8_t *frame, size_t len);
// Copies the oldest frame; returns its length or 0 when empty.
size_t bm_ring_pop(bm_ring_t *r, uint8_t *out, size_t cap);
size_t bm_ring_count(const bm_ring_t *r);
