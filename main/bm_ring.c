#include "bm_ring.h"

#include <string.h>

void bm_ring_clear(bm_ring_t *r)
{
    r->head = 0;
    r->count = 0;
}

bool bm_ring_push(bm_ring_t *r, const uint8_t *frame, size_t len)
{
    if (!frame || len == 0 || len > BM_RING_FRAME_MAX) return false;
    if (r->count == BM_RING_SLOTS) {
        r->head = (uint8_t)((r->head + 1u) % BM_RING_SLOTS);
        r->count--;
        r->overwritten++;
    }
    uint8_t tail = (uint8_t)((r->head + r->count) % BM_RING_SLOTS);
    memcpy(r->data[tail], frame, len);
    r->len[tail] = (uint16_t)len;
    r->count++;
    return true;
}

size_t bm_ring_pop(bm_ring_t *r, uint8_t *out, size_t cap)
{
    if (r->count == 0) return 0;
    size_t len = r->len[r->head];
    if (len > cap) len = cap;
    memcpy(out, r->data[r->head], len);
    r->head = (uint8_t)((r->head + 1u) % BM_RING_SLOTS);
    r->count--;
    return len;
}

size_t bm_ring_count(const bm_ring_t *r)
{
    return r->count;
}
