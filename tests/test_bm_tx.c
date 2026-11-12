// Host tests: every frame is sent, SEQ contiguous, keep-alive for the Mac's 2 s watchdog.
#include <assert.h>
#include <stdio.h>

#include "bm_tx.h"

static void test_contiguous_seq_and_dtx(void)
{
    bm_tx_t t;
    uint8_t out[1 + BM_OPUS_PACKET_MAX];
    bm_tx_begin(&t, 1000);
    const uint8_t real[3] = {0x5B, 1, 2};   // config 11, code 3 (arbitrary)
    assert(bm_tx_pack(&t, real, 3, 1060, out) == 4 && out[0] == 0 && out[1] == 0x5B);
    // encoder gave nothing: a TOC-only packet is still produced, with the next SEQ
    assert(bm_tx_pack(&t, NULL, 0, 1120, out) == 2 && out[0] == 1 && out[1] == 0x58);
    assert(bm_tx_pack(&t, real, 0, 1180, out) == 2 && out[0] == 2);
    // oversize is treated as no data, never overflows
    uint8_t big[600] = {0};
    assert(bm_tx_pack(&t, big, 600, 1240, out) == 2 && out[0] == 3);
    // 1-byte DTX packets from the encoder pass through unchanged
    const uint8_t dtx[1] = {0x58};
    assert(bm_tx_pack(&t, dtx, 1, 1300, out) == 2 && out[0] == 4 && out[1] == 0x58);
    assert(t.packed == 5);
}

static void test_seq_wraps_over_256(void)
{
    bm_tx_t t;
    uint8_t out[1 + BM_OPUS_PACKET_MAX];
    const uint8_t p[2] = {0x58, 0};
    bm_tx_begin(&t, 0);
    for (int i = 0; i < 600; i++) {
        bm_tx_pack(&t, p, 2, (uint32_t)i * 60, out);
        assert(out[0] == (uint8_t)i);
    }
}

static void test_keepalive_timing(void)
{
    bm_tx_t t;
    uint8_t out[1 + BM_OPUS_PACKET_MAX];
    bm_tx_begin(&t, 10000);
    assert(!bm_tx_keepalive_due(&t, 10499));
    assert(bm_tx_keepalive_due(&t, 10500));
    // simulate a stalled encoder for 2 s: keep-alives keep the gap below 2 s
    uint32_t now = 10000, last = 10000, worst = 0;
    int expected = 0;
    for (; now < 12500; now += 20) {
        if (bm_tx_keepalive_due(&t, now)) {
            size_t n = bm_tx_pack(&t, NULL, 0, now, out);
            assert(n == 2 && out[0] == expected++);
            if (now - last > worst) worst = now - last;
            last = now;
        }
    }
    assert(worst <= BM_TX_KEEPALIVE_MS + 20 && expected >= 4);
}

int main(void)
{
    test_contiguous_seq_and_dtx();
    test_seq_wraps_over_256();
    test_keepalive_timing();
    puts("test_bm_tx: 3 tests PASS");
    return 0;
}
