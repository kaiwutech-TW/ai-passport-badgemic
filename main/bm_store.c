// main/bm_store.c - see bm_store.h.
#include "bm_store.h"

#include "bm_meter.h"

#include <stddef.h>

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "bm_store";

#define NS "badge"
#define SECTOR 4096u

static const esp_partition_t *s_part;
static bm_pages_t s_pages;

static const char *page_key(int page)
{
    return page == 0 ? "pg0" : "pg1";
}

static esp_err_t save_meta(int page, const bm_page_meta_t *m)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    uint8_t blob[BM_PAGE_META_BYTES];
    bm_page_meta_encode(m, blob);
    e = nvs_set_blob(h, page_key(page), blob, sizeof(blob));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

static int op_erase(void *ctx, int page, int slot)
{
    (void)ctx;
    uint32_t base = bm_slot_offset(page, slot);
    // Sector by sector with a yield in between: a long erase would otherwise starve BLE.
    for (uint32_t off = 0; off < BM_SLOT_STRIDE; off += SECTOR) {
        if (esp_partition_erase_range(s_part, base + off, SECTOR) != ESP_OK) return -1;
        vTaskDelay(1);
    }
    return 0;
}

static int op_write(void *ctx, int page, int slot, uint32_t off, const uint8_t *d, size_t n)
{
    (void)ctx;
    return esp_partition_write(s_part, bm_slot_offset(page, slot) + off, d, n) == ESP_OK ? 0 : -1;
}

static int op_read(void *ctx, int page, int slot, uint32_t off, uint8_t *d, size_t n)
{
    (void)ctx;
    return esp_partition_read(s_part, bm_slot_offset(page, slot) + off, d, n) == ESP_OK ? 0 : -1;
}

static int op_commit(void *ctx, int page, int slot, uint32_t size, uint32_t crc)
{
    (void)ctx;
    (void)size;
    bm_page_meta_t m = {.valid = true, .slot = (uint8_t)slot, .crc = crc};
    if (save_meta(page, &m) != ESP_OK) return -1;   // RAM flips only after NVS accepted it
    bm_pages_activate(&s_pages, page, (uint8_t)slot, crc);
    return 0;
}

static const bm_store_ops_t k_ops = {op_erase, op_write, op_read, op_commit};

const bm_store_ops_t *bm_store_ops(void)
{
    return &k_ops;
}

const bm_pages_t *bm_store_pages(void)
{
    return &s_pages;
}

esp_err_t bm_store_init(void)
{
    bm_pages_init(&s_pages);
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "badge");
    if (s_part == NULL) {
        ESP_LOGE(TAG, "partition 'badge' not found");
        return ESP_ERR_NOT_FOUND;
    }
    if (s_part->size < BM_BADGE_PARTITION_MIN) {
        ESP_LOGE(TAG, "partition 'badge' too small: 0x%x < 0x%x", (unsigned)s_part->size,
                 (unsigned)BM_BADGE_PARTITION_MIN);
        s_part = NULL;
        return ESP_ERR_INVALID_SIZE;
    }
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        for (int i = 0; i < BM_PAGE_COUNT; i++) {
            uint8_t blob[BM_PAGE_META_BYTES];
            size_t len = sizeof(blob);
            bm_page_meta_t m;
            if (nvs_get_blob(h, page_key(i), blob, &len) == ESP_OK && len == sizeof(blob) &&
                bm_page_meta_decode(blob, &m)) {
                s_pages.page[i] = m;
            }
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "badge partition 0x%x+0x%x, pages valid: %d %d", (unsigned)s_part->address,
             (unsigned)s_part->size, s_pages.page[0].valid, s_pages.page[1].valid);
    return ESP_OK;
}

esp_err_t bm_store_map_page(int page, bm_map_t *out)
{
    out->data = NULL;
    out->handle = 0;
    if (!s_part || page < 0 || page >= BM_PAGE_COUNT) return ESP_ERR_INVALID_ARG;
    if (!s_pages.page[page].valid) return ESP_ERR_NOT_FOUND;
    const void *ptr = NULL;
    esp_err_t e = esp_partition_mmap(s_part, bm_slot_offset(page, s_pages.page[page].slot), BM_PAGE_BYTES,
                                     ESP_PARTITION_MMAP_DATA, &ptr, &out->handle);
    if (e == ESP_OK) out->data = ptr;
    return e;
}

void bm_store_unmap(bm_map_t *m)
{
    if (m->data) esp_partition_munmap(m->handle);
    m->data = NULL;
    m->handle = 0;
}

esp_err_t bm_store_clear_page(int page)
{
    if (page < 0 || page >= BM_PAGE_COUNT) return ESP_ERR_INVALID_ARG;
    bm_page_meta_t m = s_pages.page[page];
    m.valid = false;
    m.crc = 0;
    esp_err_t e = save_meta(page, &m);
    if (e == ESP_OK) bm_pages_clear(&s_pages, page);
    return e;
}

void bm_store_load_display(bm_display_cfg_t *cfg)
{
    bm_display_cfg_default(cfg);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    bm_display_cfg_t stored;
    bm_display_cfg_default(&stored);
    size_t len = sizeof(stored);
    // Blobs written before off_key existed end right before it: keep them, off_key = none.
    if (nvs_get_blob(h, "disp", &stored, &len) == ESP_OK &&
        (len == sizeof(stored) || len == offsetof(bm_display_cfg_t, off_key))) {
        if (len != sizeof(stored)) stored.off_key = BM_OFF_KEY_NONE;
        *cfg = stored;
        bm_display_cfg_clamp(cfg);
    }
    nvs_close(h);
}

esp_err_t bm_store_save_display(const bm_display_cfg_t *cfg)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(h, "disp", cfg, sizeof(*cfg));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

int bm_store_load_mic_gain(void)
{
    int db = BM_MIC_GAIN_DEFAULT;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return db;
    uint8_t v;
    if (nvs_get_u8(h, "mic_gain", &v) == ESP_OK) db = bm_mic_gain_normalize(v);
    nvs_close(h);
    return db;
}

esp_err_t bm_store_save_mic_gain(int db)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_u8(h, "mic_gain", (uint8_t)bm_mic_gain_normalize(db));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}
