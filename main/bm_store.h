// main/bm_store.h - flash storage of badge pages (`badge` partition) and settings (NVS).
//
// Pattern borrowed from the senior-safety-card jpeg_store: erase + write through the
// partition API, display through esp_partition_mmap so nothing is copied to RAM.
// Single-threaded: only the app task calls into this module. It never touches the
// `cardid` or `recovery` partitions.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bm_pages.h"
#include "bm_power.h"
#include "bm_upload.h"
#include "esp_err.h"
#include "esp_partition.h"

// Call after nvs_flash_init(). Finds the partition, loads page metadata and settings.
esp_err_t bm_store_init(void);
const bm_pages_t *bm_store_pages(void);
const bm_store_ops_t *bm_store_ops(void);

typedef struct {
    const uint8_t *data;   // BM_PAGE_BYTES of RGB565 little-endian, or NULL
    esp_partition_mmap_handle_t handle;
} bm_map_t;

// Memory-maps the active slot of a page. ESP_ERR_NOT_FOUND when the page is empty.
esp_err_t bm_store_map_page(int page, bm_map_t *out);
void bm_store_unmap(bm_map_t *m);
// Marks the page empty (the old slot content is left in flash, unreferenced).
esp_err_t bm_store_clear_page(int page);

void bm_store_load_display(bm_display_cfg_t *cfg);
esp_err_t bm_store_save_display(const bm_display_cfg_t *cfg);
