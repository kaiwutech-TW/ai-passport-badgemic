// Host tests: Badge Mic framing, reassembly and CRC-32.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bm_crc32.h"
#include "bm_proto.h"

typedef struct {
    int count;
    uint8_t type[16];
    uint8_t data[16][BM_FRAME_MAX];
    size_t len[16];
} cap_t;

static void on_frame(uint8_t type, const uint8_t *p, size_t len, void *ctx)
{
    cap_t *c = ctx;
    assert(c->count < 16);
    c->type[c->count] = type;
    c->len[c->count] = len;
    if (len) memcpy(c->data[c->count], p, len);
    c->count++;
}

static void test_crc(void)
{
    assert(bm_crc32_update(0, (const uint8_t *)"123456789", 9) == 0xCBF43926u);
    assert(bm_crc32_update(0, NULL, 0) == 0);
    uint32_t c = bm_crc32_update(0, (const uint8_t *)"1234", 4);
    c = bm_crc32_update(c, (const uint8_t *)"56789", 5);
    assert(c == 0xCBF43926u);
}

static void test_encode(void)
{
    uint8_t out[BM_FRAME_MAX];
    const uint8_t p[3] = {1, 2, 3};
    assert(bm_encode(out, sizeof(out), BM_FRAME_EVENT, p, 3) == 9);
    assert(out[0] == 0xA5 && out[1] == 0x5A && out[2] == 4 && out[3] == 0 && out[4] == 0 && out[5] == 3);
    uint8_t big[600] = {0};
    assert(bm_encode(out, sizeof(out), BM_FRAME_EVENT, big, 513) == 0);
    assert(bm_encode(out, sizeof(out), BM_FRAME_EVENT, big, 512) == 518);
    assert(bm_encode(out, sizeof(out), BM_FRAME_AUDIO_OPUS, big, 513) == 519);
    assert(bm_encode(out, sizeof(out), BM_FRAME_AUDIO_OPUS, big, 514) == 0);
    assert(bm_encode(out, sizeof(out), BM_FRAME_PUT_DATA, big, 228) == 234);
    assert(bm_encode(out, sizeof(out), BM_FRAME_PUT_DATA, big, 229) == 0);
    assert(bm_encode(out, sizeof(out), 0x01, big, 1) == 0);   // not a Badge Mic type
    assert(bm_encode(out, 8, BM_FRAME_EVENT, p, 3) == 0);
}

static void test_reassembly_split_and_batched(void)
{
    uint8_t a[64], b[64];
    const uint8_t j1[] = "{\"cmd\":\"x\"}";
    uint8_t pd[4 + 10] = {0, 0, 1, 0};
    size_t na = bm_encode(a, sizeof(a), BM_FRAME_CONTROL, j1, sizeof(j1) - 1);
    size_t nb = bm_encode(b, sizeof(b), BM_FRAME_PUT_DATA, pd, sizeof(pd));
    uint8_t all[128];
    memcpy(all, a, na);
    memcpy(all + na, b, nb);
    for (size_t step = 1; step <= na + nb; step++) {
        cap_t c = {0};
        bm_reassembler_t rx;
        bm_reassembler_init(&rx, BM_RX_ACCEPT_MASK, on_frame, &c);
        for (size_t i = 0; i < na + nb; i += step) {
            size_t n = na + nb - i < step ? na + nb - i : step;
            bm_reassembler_push(&rx, all + i, n);
        }
        assert(c.count == 2);
        assert(c.type[0] == BM_FRAME_CONTROL && c.len[0] == sizeof(j1) - 1);
        assert(c.type[1] == BM_FRAME_PUT_DATA && c.len[1] == sizeof(pd));
    }
}

static void test_resync_and_filtering(void)
{
    cap_t c = {0};
    bm_reassembler_t rx;
    bm_reassembler_init(&rx, BM_RX_ACCEPT_MASK, on_frame, &c);
    uint8_t f[32];
    size_t n = bm_encode(f, sizeof(f), BM_FRAME_CONTROL, (const uint8_t *)"{}", 2);
    uint8_t junk[] = {0x00, 0xA5, 0xA5, 0x5A, 0x99, 0x01};   // bogus magic-ish bytes
    bm_reassembler_push(&rx, junk, sizeof(junk));
    bm_reassembler_push(&rx, f, n);
    assert(c.count == 1);

    // A frame type the device does not accept from the Mac (AUDIO) is skipped.
    uint8_t audio[16];
    uint8_t ap[3] = {0, 9, 9};
    size_t an = bm_encode(audio, sizeof(audio), BM_FRAME_AUDIO_OPUS, ap, 3);
    bm_reassembler_push(&rx, audio, an);
    bm_reassembler_push(&rx, f, n);
    assert(c.count == 2);

    // Oversized length is misalignment, not a 70000-byte wait.
    uint8_t bad[] = {0xA5, 0x5A, BM_FRAME_CONTROL, 0, 0xFF, 0xFF};
    bm_reassembler_push(&rx, bad, sizeof(bad));
    bm_reassembler_push(&rx, f, n);
    assert(c.count == 3);
}

static void test_half_control_replaced_by_new_header(void)
{
    cap_t c = {0};
    bm_reassembler_t rx;
    bm_reassembler_init(&rx, BM_RX_ACCEPT_MASK, on_frame, &c);
    uint8_t whole[32], half[32];
    size_t n = bm_encode(whole, sizeof(whole), BM_FRAME_CONTROL, (const uint8_t *)"{\"a\":1}", 7);
    size_t h = bm_encode(half, sizeof(half), BM_FRAME_CONTROL, (const uint8_t *)"{\"b\":22222}", 11);
    bm_reassembler_push(&rx, half, h - 9);   // truncated: 9 bytes missing
    bm_reassembler_push(&rx, whole, n);
    assert(c.count == 1);
    assert(c.len[0] == 7 && memcmp(c.data[0], "{\"a\":1}", 7) == 0);
    assert(rx.stat_half_dropped == 1);
}

static void test_put_data_with_magic_inside(void)
{
    // Binary pixels may contain A5 5A 03 00 00 02 (a plausible CONTROL header).
    uint8_t payload[4 + 20] = {0, 0, 0, 0};
    const uint8_t fake[] = {0xA5, 0x5A, 0x03, 0x00, 0x00, 0x02};
    memcpy(payload + 4 + 5, fake, sizeof(fake));
    uint8_t f[64];
    size_t n = bm_encode(f, sizeof(f), BM_FRAME_PUT_DATA, payload, sizeof(payload));
    cap_t c = {0};
    bm_reassembler_t rx;
    bm_reassembler_init(&rx, BM_RX_ACCEPT_MASK, on_frame, &c);
    bm_reassembler_push(&rx, f, n);
    assert(c.count == 1 && c.type[0] == BM_FRAME_PUT_DATA && c.len[0] == sizeof(payload));
    assert(memcmp(c.data[0], payload, sizeof(payload)) == 0);
    assert(rx.stat_half_dropped == 0);
}

int main(void)
{
    test_crc();
    test_encode();
    test_reassembly_split_and_batched();
    test_resync_and_filtering();
    test_half_control_replaced_by_new_header();
    test_put_data_with_magic_inside();
    puts("test_bm_proto: 6 tests PASS");
    return 0;
}
