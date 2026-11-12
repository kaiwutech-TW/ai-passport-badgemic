#include "bm_tx.h"

#include <string.h>

void bm_tx_begin(bm_tx_t *t, uint32_t now_ms)
{
    t->seq = 0;
    t->toc = BM_TX_DEFAULT_TOC;
    t->last_ms = now_ms;
    t->packed = 0;
}

size_t bm_tx_pack(bm_tx_t *t, const uint8_t *opus, size_t len, uint32_t now_ms, uint8_t *out)
{
    out[0] = t->seq++;
    if (opus == NULL || len == 0 || len > BM_OPUS_PACKET_MAX) {
        out[1] = (uint8_t)(t->toc & 0xFCu);   // code 0 with no frame data = DTX / lost frame
        len = 1;
    } else {
        memcpy(out + 1, opus, len);
        t->toc = opus[0];
    }
    t->last_ms = now_ms;
    t->packed++;
    return len + 1;
}

bool bm_tx_keepalive_due(const bm_tx_t *t, uint32_t now_ms)
{
    return now_ms - t->last_ms >= BM_TX_KEEPALIVE_MS;
}
