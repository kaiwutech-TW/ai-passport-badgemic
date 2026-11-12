// Host tests: upload state machine, page slots, pre-roll ring.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bm_crc32.h"
#include "bm_pages.h"
#include "bm_ring.h"
#include "bm_upload.h"

typedef struct {
    uint8_t flash[BM_PAGE_COUNT][2][BM_PAGE_BYTES];
    int erased, committed_page, committed_slot;
    uint32_t committed_crc, committed_size;
    int fail_erase, fail_write_at, corrupt_after_write;
} fake_t;

static fake_t g;

static int f_erase(void *c, int page, int slot)
{
    (void)c;
    if (g.fail_erase) return -1;
    memset(g.flash[page][slot], 0xFF, BM_PAGE_BYTES);
    g.erased++;
    return 0;
}
static int f_write(void *c, int page, int slot, uint32_t off, const uint8_t *d, size_t n)
{
    (void)c;
    if (g.fail_write_at && (int)off >= g.fail_write_at) return -1;
    memcpy(&g.flash[page][slot][off], d, n);
    if (g.corrupt_after_write && off == 0) g.flash[page][slot][0] ^= 1;
    return 0;
}
static int f_read(void *c, int page, int slot, uint32_t off, uint8_t *d, size_t n)
{
    (void)c;
    memcpy(d, &g.flash[page][slot][off], n);
    return 0;
}
static int f_commit(void *c, int page, int slot, uint32_t size, uint32_t crc)
{
    (void)c;
    g.committed_page = page; g.committed_slot = slot; g.committed_size = size; g.committed_crc = crc;
    return 0;
}
static const bm_store_ops_t OPS = {f_erase, f_write, f_read, f_commit};

static uint8_t image[BM_PAGE_BYTES];

static void make_image(void)
{
    for (size_t i = 0; i < sizeof(image); i++) image[i] = (uint8_t)(i * 7 + (i >> 8));
}

static const char *send_all(bm_upload_t *u, size_t upto, size_t chunk)
{
    uint8_t pl[4 + 224];
    for (size_t off = 0; off < upto; off += chunk) {
        size_t n = upto - off < chunk ? upto - off : chunk;
        pl[0] = (uint8_t)(off >> 24); pl[1] = (uint8_t)(off >> 16); pl[2] = (uint8_t)(off >> 8); pl[3] = (uint8_t)off;
        memcpy(pl + 4, image + off, n);
        const char *e = bm_upload_data(u, pl, 4 + n, 100);
        if (e) return e;
    }
    return NULL;
}

static void test_happy_path(void)
{
    memset(&g, 0, sizeof(g));
    g.committed_page = -1;
    make_image();
    uint32_t crc = bm_crc32_update(0, image, sizeof(image));
    bm_upload_t u;
    bm_upload_init(&u, &OPS, NULL);
    assert(!bm_upload_begin(&u, 1, BM_PAGE_COUNT, BM_PAGE_BYTES, BM_PAGE_BYTES, crc, 1, 0));
    assert(bm_upload_active(&u) && g.erased == 1);
    assert(!send_all(&u, sizeof(image), 224));
    assert(!bm_upload_end(&u, 1));
    assert(g.committed_page == 1 && g.committed_slot == 1 && g.committed_crc == crc &&
           g.committed_size == BM_PAGE_BYTES);
    assert(memcmp(g.flash[1][1], image, sizeof(image)) == 0);
    assert(!bm_upload_active(&u));
}

static void test_failures_never_commit(void)
{
    bm_upload_t u;
    make_image();
    uint32_t crc = bm_crc32_update(0, image, sizeof(image));
    bm_upload_init(&u, &OPS, NULL);

    memset(&g, 0, sizeof(g)); g.committed_page = -1;
    assert(!strcmp(bm_upload_begin(&u, 2, BM_PAGE_COUNT, BM_PAGE_BYTES, BM_PAGE_BYTES, crc, 0, 0), "bad_page"));
    assert(!strcmp(bm_upload_begin(&u, 0, BM_PAGE_COUNT, 100, BM_PAGE_BYTES, crc, 0, 0), "bad_size"));
    g.fail_erase = 1;
    assert(!strcmp(bm_upload_begin(&u, 0, BM_PAGE_COUNT, BM_PAGE_BYTES, BM_PAGE_BYTES, crc, 0, 0), "erase_failed"));
    g.fail_erase = 0;

    // wrong CRC announced
    assert(!bm_upload_begin(&u, 0, BM_PAGE_COUNT, BM_PAGE_BYTES, BM_PAGE_BYTES, crc ^ 1, 0, 0));
    assert(!send_all(&u, sizeof(image), 224));
    assert(!strcmp(bm_upload_end(&u, 0), "crc"));
    assert(g.committed_page == -1);

    // truncated
    assert(!bm_upload_begin(&u, 0, BM_PAGE_COUNT, BM_PAGE_BYTES, BM_PAGE_BYTES, crc, 0, 0));
    assert(!send_all(&u, 224 * 10, 224));
    assert(!strcmp(bm_upload_end(&u, 0), "incomplete"));

    // gap in offsets aborts at once
    assert(!bm_upload_begin(&u, 0, BM_PAGE_COUNT, BM_PAGE_BYTES, BM_PAGE_BYTES, crc, 0, 0));
    uint8_t pl[4 + 8] = {0, 0, 1, 0};
    assert(!strcmp(bm_upload_data(&u, pl, sizeof(pl), 1), "bad_offset"));
    assert(!bm_upload_active(&u));
    assert(!strcmp(bm_upload_data(&u, pl, sizeof(pl), 1), "no_upload"));

    // more data than announced
    assert(!bm_upload_begin(&u, 0, BM_PAGE_COUNT, BM_PAGE_BYTES, BM_PAGE_BYTES, crc, 0, 0));
    assert(!send_all(&u, sizeof(image), 224));
    uint8_t extra[4 + 4] = {0x00, 0x02, 0x58, 0x00};   // offset 153600
    assert(!strcmp(bm_upload_data(&u, extra, sizeof(extra), 1), "overflow"));

    // flash write error mid-way
    assert(!bm_upload_begin(&u, 0, BM_PAGE_COUNT, BM_PAGE_BYTES, BM_PAGE_BYTES, crc, 0, 0));
    g.fail_write_at = 2000;
    assert(!strcmp(send_all(&u, sizeof(image), 224), "write_failed"));
    g.fail_write_at = 0;

    // flash corrupts data after write: read-back catches it
    g.corrupt_after_write = 1;
    assert(!bm_upload_begin(&u, 0, BM_PAGE_COUNT, BM_PAGE_BYTES, BM_PAGE_BYTES, crc, 0, 0));
    assert(!send_all(&u, sizeof(image), 224));
    assert(!strcmp(bm_upload_end(&u, 0), "verify_failed"));
    assert(g.committed_page == -1);
    g.corrupt_after_write = 0;

    // put_end for another page
    assert(!bm_upload_begin(&u, 0, BM_PAGE_COUNT, BM_PAGE_BYTES, BM_PAGE_BYTES, crc, 0, 0));
    assert(!strcmp(bm_upload_end(&u, 1), "no_upload"));
}

static void test_restart_and_stall(void)
{
    memset(&g, 0, sizeof(g));
    make_image();
    uint32_t crc = bm_crc32_update(0, image, sizeof(image));
    bm_upload_t u;
    bm_upload_init(&u, &OPS, NULL);
    assert(!bm_upload_begin(&u, 0, BM_PAGE_COUNT, BM_PAGE_BYTES, BM_PAGE_BYTES, crc, 0, 0));
    assert(!send_all(&u, 224 * 3, 224));
    assert(!bm_upload_begin(&u, 0, BM_PAGE_COUNT, BM_PAGE_BYTES, BM_PAGE_BYTES, crc, 0, 500));  // restart
    assert(!send_all(&u, sizeof(image), 224));
    assert(!bm_upload_check_stall(&u, 100 + BM_UPLOAD_STALL_MS - 1));
    assert(bm_upload_check_stall(&u, 100 + BM_UPLOAD_STALL_MS));
    assert(!bm_upload_active(&u));
}

static void test_pages(void)
{
    bm_pages_t p;
    bm_pages_init(&p);
    assert(bm_pages_first_valid(&p) == -1 && bm_pages_next_valid(&p, 0) == -1);
    assert(BM_SLOT_STRIDE % 4096 == 0 && BM_SLOT_STRIDE >= BM_PAGE_BYTES);
    assert(BM_BADGE_PARTITION_MIN <= 0x100000);
    assert(bm_slot_offset(0, 0) == 0 && bm_slot_offset(0, 1) == BM_SLOT_STRIDE &&
           bm_slot_offset(1, 0) == 2 * BM_SLOT_STRIDE && bm_slot_offset(1, 1) == 3 * BM_SLOT_STRIDE);

    assert(bm_pages_inactive_slot(&p, 0) == 1);
    bm_pages_activate(&p, 0, 1, 0x1234);
    assert(bm_pages_inactive_slot(&p, 0) == 0);
    assert(bm_pages_first_valid(&p) == 0 && bm_pages_next_valid(&p, 0) == 0);   // only one: stays
    bm_pages_activate(&p, 1, 0, 0x99);
    assert(bm_pages_next_valid(&p, 0) == 1 && bm_pages_next_valid(&p, 1) == 0);
    bm_pages_clear(&p, 0);
    assert(!p.page[0].valid && p.page[0].slot == 1);
    assert(bm_pages_inactive_slot(&p, 0) == 0);   // wear alternates even after clear
    assert(bm_pages_first_valid(&p) == 1 && bm_pages_next_valid(&p, 1) == 1 && bm_pages_next_valid(&p, 0) == 1);

    uint8_t blob[BM_PAGE_META_BYTES];
    bm_page_meta_t m = {true, 1, 0xCAFEBABEu}, d;
    bm_page_meta_encode(&m, blob);
    assert(bm_page_meta_decode(blob, &d) && d.valid && d.slot == 1 && d.crc == 0xCAFEBABEu);
    blob[0] = 'X';
    assert(!bm_page_meta_decode(blob, &d));
    bm_page_meta_encode(&m, blob);
    blob[2] = 7;
    assert(!bm_page_meta_decode(blob, &d));
}

static void test_ring(void)
{
    static bm_ring_t r;
    bm_ring_clear(&r);
    uint8_t out[600];
    assert(bm_ring_pop(&r, out, sizeof(out)) == 0);
    assert(!bm_ring_push(&r, out, 0) && !bm_ring_push(&r, out, 513));
    for (uint8_t i = 0; i < 5; i++) {
        uint8_t f[3] = {i, i, i};
        assert(bm_ring_push(&r, f, 3));
    }
    assert(bm_ring_count(&r) == 5);
    assert(bm_ring_pop(&r, out, sizeof(out)) == 3 && out[0] == 0);
    for (uint8_t i = 5; i < 20; i++) {
        uint8_t f[2] = {i, 0};
        assert(bm_ring_push(&r, f, 2));
    }
    assert(bm_ring_count(&r) == BM_RING_SLOTS);
    assert(r.overwritten == 11);  /* 4 left + 15 pushed - 8 slots */
    uint8_t expect = 20 - BM_RING_SLOTS;   // newest 8 survive, in order
    for (unsigned i = 0; i < BM_RING_SLOTS; i++) {
        assert(bm_ring_pop(&r, out, sizeof(out)) == 2 && out[0] == expect + i);
    }
    assert(bm_ring_pop(&r, out, sizeof(out)) == 0);
    uint8_t big[512];
    memset(big, 0xAB, sizeof(big));
    assert(bm_ring_push(&r, big, 512) && bm_ring_pop(&r, out, sizeof(out)) == 512 && out[511] == 0xAB);
}

int main(void)
{
    test_happy_path();
    test_failures_never_commit();
    test_restart_and_stall();
    test_pages();
    test_ring();
    puts("test_bm_upload: 5 tests PASS");
    return 0;
}
