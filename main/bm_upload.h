// main/bm_upload.h - badge page upload state machine (pure C, storage injected).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int (*erase)(void *ctx, int page, int slot);                       // 0 = ok
    int (*write)(void *ctx, int page, int slot, uint32_t off, const uint8_t *d, size_t n);
    int (*read)(void *ctx, int page, int slot, uint32_t off, uint8_t *d, size_t n);
    int (*commit)(void *ctx, int page, int slot, uint32_t size, uint32_t crc);
} bm_store_ops_t;

typedef struct {
    const bm_store_ops_t *ops;
    void *ctx;
    bool active;
    int page, slot;
    uint32_t size, expect_crc, offset, running_crc;
    uint32_t last_data_ms;
} bm_upload_t;

#define BM_UPLOAD_STALL_MS 10000u

void bm_upload_init(bm_upload_t *u, const bm_store_ops_t *ops, void *ctx);
bool bm_upload_active(const bm_upload_t *u);
// Error strings are the "err" values of put_done. NULL means success.
// begin restarts any upload in progress.
const char *bm_upload_begin(bm_upload_t *u, int page, int page_count, uint32_t size,
                            uint32_t expected_size, uint32_t crc, int slot, uint32_t now_ms);
// payload = [OFFSET:4 BE][bytes]. A returned error aborts the upload; ignored
// (returns "no_upload") when none is active.
const char *bm_upload_data(bm_upload_t *u, const uint8_t *payload, size_t len, uint32_t now_ms);
const char *bm_upload_end(bm_upload_t *u, int page);
void bm_upload_abort(bm_upload_t *u);
// Aborts and returns true when no data arrived for BM_UPLOAD_STALL_MS.
bool bm_upload_check_stall(bm_upload_t *u, uint32_t now_ms);
