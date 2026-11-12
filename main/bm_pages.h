// main/bm_pages.h - badge page slot bookkeeping (pure C).
//
// Flash layout of the `badge` partition: 2 pages x 2 slots, each slot
// BM_SLOT_STRIDE bytes (4 KB aligned, >= BM_PAGE_BYTES). An upload goes to the
// slot that is not active; the page flips to it only after verification.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BM_PAGE_W 240u
#define BM_PAGE_H 320u
#define BM_PAGE_BYTES (BM_PAGE_W * BM_PAGE_H * 2u)  // 153600
#define BM_PAGE_COUNT 2
#define BM_SLOTS_PER_PAGE 2
#define BM_SLOT_STRIDE 0x26000u  // 155648 = 38 x 4 KB
#define BM_BADGE_PARTITION_MIN (BM_PAGE_COUNT * BM_SLOTS_PER_PAGE * BM_SLOT_STRIDE)

typedef struct {
    bool valid;
    uint8_t slot;  // active slot while valid; last used slot after clear
    uint32_t crc;
} bm_page_meta_t;

typedef struct {
    bm_page_meta_t page[BM_PAGE_COUNT];
} bm_pages_t;

#define BM_PAGE_META_BYTES 8

void bm_pages_init(bm_pages_t *p);
uint32_t bm_slot_offset(int page, int slot);
// Slot a new upload must use: always the other one than `slot`.
uint8_t bm_pages_inactive_slot(const bm_pages_t *p, int page);
void bm_pages_activate(bm_pages_t *p, int page, uint8_t slot, uint32_t crc);
void bm_pages_clear(bm_pages_t *p, int page);
// First valid page, or -1.
int bm_pages_first_valid(const bm_pages_t *p);
// Next valid page after `current` (cyclic); `current` itself if it is the only
// one; -1 when none is valid.
int bm_pages_next_valid(const bm_pages_t *p, int current);
// NVS blob codec; decode rejects bad magic / slot.
void bm_page_meta_encode(const bm_page_meta_t *m, uint8_t out[BM_PAGE_META_BYTES]);
bool bm_page_meta_decode(const uint8_t in[BM_PAGE_META_BYTES], bm_page_meta_t *m);
