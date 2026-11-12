// main/bm_proto.c - see bm_proto.h. Adapted from Shinku-Chen's MIT oc_proto.
#include "bm_proto.h"

#include <string.h>

bool bm_type_known(uint8_t type)
{
    return type == BM_FRAME_CONTROL || type == BM_FRAME_EVENT ||
           type == BM_FRAME_AUDIO_OPUS || type == BM_FRAME_PUT_DATA;
}

size_t bm_payload_limit(uint8_t type)
{
    switch (type) {
    case BM_FRAME_CONTROL:
    case BM_FRAME_EVENT:
        return BM_JSON_PAYLOAD_MAX;
    case BM_FRAME_AUDIO_OPUS:
        return BM_AUDIO_PAYLOAD_MAX;
    case BM_FRAME_PUT_DATA:
        return BM_PUT_PAYLOAD_MAX;
    default:
        return 0;
    }
}

size_t bm_encode(uint8_t *out, size_t out_cap, uint8_t type, const uint8_t *payload, size_t len)
{
    if (out == NULL || (payload == NULL && len != 0)) return 0;
    if (!bm_type_known(type) || len > bm_payload_limit(type)) return 0;
    size_t total = BM_HEADER_SIZE + len;
    if (out_cap < total) return 0;
    out[0] = BM_MAGIC0;
    out[1] = BM_MAGIC1;
    out[2] = type;
    out[3] = 0;
    out[4] = (uint8_t)(len >> 8);
    out[5] = (uint8_t)len;
    if (len) memcpy(out + BM_HEADER_SIZE, payload, len);
    return total;
}

static bool header_valid(const bm_reassembler_t *rx, const uint8_t *h)
{
    if (h[0] != BM_MAGIC0 || h[1] != BM_MAGIC1) return false;
    if (!bm_type_known(h[2]) || h[2] > 31 || !(rx->accept_mask & BM_TYPE_BIT(h[2]))) return false;
    size_t len = ((size_t)h[4] << 8) | h[5];
    return len <= bm_payload_limit(h[2]);
}

// Looks for a new valid CONTROL header inside a half-received CONTROL frame.
static size_t find_inserted_header(const bm_reassembler_t *rx)
{
    if (rx->buf[2] != BM_FRAME_CONTROL) return 0;
    for (size_t i = 1; i + BM_HEADER_SIZE <= rx->len; i++) {
        if (rx->buf[i] == BM_MAGIC0 && rx->buf[i + 1] == BM_MAGIC1 && header_valid(rx, rx->buf + i)) {
            return i;
        }
    }
    return 0;
}

static void drop_one(bm_reassembler_t *rx)
{
    if (rx->len == 0) return;
    memmove(rx->buf, rx->buf + 1, rx->len - 1);
    rx->len--;
}

void bm_reassembler_reset(bm_reassembler_t *rx)
{
    if (!rx) return;
    rx->len = rx->need = rx->frame_len = 0;
    rx->in_frame = false;
}

void bm_reassembler_init(bm_reassembler_t *rx, uint32_t accept_mask, bm_frame_cb_t cb, void *ctx)
{
    if (!rx) return;
    memset(rx, 0, sizeof(*rx));
    rx->accept_mask = accept_mask;
    rx->cb = cb;
    rx->ctx = ctx;
}

void bm_reassembler_push(bm_reassembler_t *rx, const uint8_t *bytes, size_t n)
{
    if (!rx || (!bytes && n)) return;
    size_t idx = 0;
    for (;;) {
        if (!rx->in_frame) {
            if (rx->len < BM_HEADER_SIZE && idx < n) {
                size_t take = BM_HEADER_SIZE - rx->len;
                if (take > n - idx) take = n - idx;
                memcpy(rx->buf + rx->len, bytes + idx, take);
                rx->len += take;
                idx += take;
                continue;
            }
            if (rx->len >= 1 && rx->buf[0] != BM_MAGIC0) { drop_one(rx); continue; }
            if (rx->len >= 2 && rx->buf[1] != BM_MAGIC1) { drop_one(rx); continue; }
            if (rx->len < BM_HEADER_SIZE) break;
            if (!header_valid(rx, rx->buf)) { drop_one(rx); continue; }
            size_t plen = ((size_t)rx->buf[4] << 8) | rx->buf[5];
            rx->frame_len = BM_HEADER_SIZE + plen;
            rx->need = rx->frame_len - rx->len;
            rx->in_frame = true;
            continue;
        }
        if (rx->need > 0 && idx < n) {
            size_t take = rx->need;
            if (take > n - idx) take = n - idx;
            memcpy(rx->buf + rx->len, bytes + idx, take);
            rx->len += take;
            idx += take;
            rx->need -= take;
            continue;
        }
        size_t cut = find_inserted_header(rx);
        if (cut > 0) {
            rx->stat_half_dropped++;
            memmove(rx->buf, rx->buf + cut, rx->len - cut);
            rx->len -= cut;
            rx->need = rx->frame_len = 0;
            rx->in_frame = false;
            continue;
        }
        if (rx->need == 0) {
            rx->stat_delivered++;
            uint8_t type = rx->buf[2];
            size_t plen = rx->frame_len - BM_HEADER_SIZE;
            if (rx->cb) rx->cb(type, rx->buf + BM_HEADER_SIZE, plen, rx->ctx);
            rx->len = rx->need = rx->frame_len = 0;
            rx->in_frame = false;
            continue;
        }
        break;
    }
}
