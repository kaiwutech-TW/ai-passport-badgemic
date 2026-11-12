#include "bm_upload.h"

#include "bm_crc32.h"

void bm_upload_init(bm_upload_t *u, const bm_store_ops_t *ops, void *ctx)
{
    u->ops = ops;
    u->ctx = ctx;
    u->active = false;
}

bool bm_upload_active(const bm_upload_t *u)
{
    return u->active;
}

void bm_upload_abort(bm_upload_t *u)
{
    u->active = false;
}

const char *bm_upload_begin(bm_upload_t *u, int page, int page_count, uint32_t size,
                            uint32_t expected_size, uint32_t crc, int slot, uint32_t now_ms)
{
    u->active = false;
    if (page < 0 || page >= page_count) return "bad_page";
    if (size != expected_size) return "bad_size";
    if (u->ops->erase(u->ctx, page, slot) != 0) return "erase_failed";
    u->active = true;
    u->page = page;
    u->slot = slot;
    u->size = size;
    u->expect_crc = crc;
    u->offset = 0;
    u->running_crc = 0;
    u->last_data_ms = now_ms;
    return NULL;
}

const char *bm_upload_data(bm_upload_t *u, const uint8_t *payload, size_t len, uint32_t now_ms)
{
    if (!u->active) return "no_upload";
    if (len < 4) { u->active = false; return "bad_frame"; }
    uint32_t off = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
                   ((uint32_t)payload[2] << 8) | payload[3];
    size_t n = len - 4;
    if (off != u->offset) { u->active = false; return "bad_offset"; }
    if (n > u->size - u->offset) { u->active = false; return "overflow"; }
    if (n) {
        if (u->ops->write(u->ctx, u->page, u->slot, off, payload + 4, n) != 0) {
            u->active = false;
            return "write_failed";
        }
        u->running_crc = bm_crc32_update(u->running_crc, payload + 4, n);
        u->offset += (uint32_t)n;
    }
    u->last_data_ms = now_ms;
    return NULL;
}

const char *bm_upload_end(bm_upload_t *u, int page)
{
    if (!u->active || page != u->page) return "no_upload";
    u->active = false;
    if (u->offset != u->size) return "incomplete";
    if (u->running_crc != u->expect_crc) return "crc";
    // Read the slot back: proves the flash holds what was received.
    uint8_t buf[256];
    uint32_t crc = 0;
    for (uint32_t off = 0; off < u->size;) {
        size_t n = u->size - off < sizeof(buf) ? u->size - off : sizeof(buf);
        if (u->ops->read(u->ctx, u->page, u->slot, off, buf, n) != 0) return "verify_failed";
        crc = bm_crc32_update(crc, buf, n);
        off += (uint32_t)n;
    }
    if (crc != u->expect_crc) return "verify_failed";
    if (u->ops->commit(u->ctx, u->page, u->slot, u->size, u->expect_crc) != 0) return "commit_failed";
    return NULL;
}

bool bm_upload_check_stall(bm_upload_t *u, uint32_t now_ms)
{
    if (u->active && now_ms - u->last_data_ms >= BM_UPLOAD_STALL_MS) {
        u->active = false;
        return true;
    }
    return false;
}
