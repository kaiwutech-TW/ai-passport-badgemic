#include "bm_pages.h"

#include <string.h>

void bm_pages_init(bm_pages_t *p)
{
    memset(p, 0, sizeof(*p));
}

uint32_t bm_slot_offset(int page, int slot)
{
    return (uint32_t)(page * BM_SLOTS_PER_PAGE + slot) * BM_SLOT_STRIDE;
}

uint8_t bm_pages_inactive_slot(const bm_pages_t *p, int page)
{
    return (uint8_t)(p->page[page].slot ^ 1u);
}

void bm_pages_activate(bm_pages_t *p, int page, uint8_t slot, uint32_t crc)
{
    p->page[page].valid = true;
    p->page[page].slot = slot & 1u;
    p->page[page].crc = crc;
}

void bm_pages_clear(bm_pages_t *p, int page)
{
    p->page[page].valid = false;
    p->page[page].crc = 0;
}

int bm_pages_first_valid(const bm_pages_t *p)
{
    for (int i = 0; i < BM_PAGE_COUNT; i++) if (p->page[i].valid) return i;
    return -1;
}

int bm_pages_next_valid(const bm_pages_t *p, int current)
{
    for (int step = 1; step <= BM_PAGE_COUNT; step++) {
        int i = ((current < 0 ? -1 : current) + step + BM_PAGE_COUNT) % BM_PAGE_COUNT;
        if (p->page[i].valid) return i;
    }
    return -1;
}

void bm_page_meta_encode(const bm_page_meta_t *m, uint8_t out[BM_PAGE_META_BYTES])
{
    out[0] = 'P';
    out[1] = m->valid ? 1 : 0;
    out[2] = m->slot & 1u;
    out[3] = 0;
    out[4] = (uint8_t)m->crc;
    out[5] = (uint8_t)(m->crc >> 8);
    out[6] = (uint8_t)(m->crc >> 16);
    out[7] = (uint8_t)(m->crc >> 24);
}

bool bm_page_meta_decode(const uint8_t in[BM_PAGE_META_BYTES], bm_page_meta_t *m)
{
    if (in[0] != 'P' || in[1] > 1 || in[2] > 1) return false;
    m->valid = in[1] == 1;
    m->slot = in[2];
    m->crc = (uint32_t)in[4] | ((uint32_t)in[5] << 8) | ((uint32_t)in[6] << 16) | ((uint32_t)in[7] << 24);
    return true;
}
