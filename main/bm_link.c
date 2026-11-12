// main/bm_link.c - NUS BLE peripheral (see bm_link.h).
// Ported from the MIT-licensed Pocket Intercom oc_link by Shinku-Chen.
#include "bm_link.h"

#include <stdio.h>
#include <string.h>

#include "bm_proto.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "bm_link";

// Nordic UART Service UUIDs (little-endian byte order for BLE_UUID128_INIT).
#define BM_NUS_SERVICE_UUID_BYTES                                                      \
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, \
        0x40, 0x6e
#define BM_NUS_RX_UUID_BYTES                                                           \
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, \
        0x40, 0x6e
#define BM_NUS_TX_UUID_BYTES                                                           \
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, \
        0x40, 0x6e

#define BM_LINK_TX_CHUNK_MAX 244U      // one notify: MTU 247 - 3
#define BM_LINK_TX_SLOTS 12U
#define BM_LINK_TX_SLOT_MAX BM_FRAME_MAX
#define BM_LINK_RX_RING 4096U
#define BM_LINK_TX_TASK_STACK 3072U
#define BM_LINK_TX_TASK_PRIO 5U

typedef struct {
    uint8_t data[BM_LINK_TX_SLOT_MAX];
    size_t len;
} bm_link_tx_slot_t;

static struct {
    SemaphoreHandle_t mutex;
    bm_link_event_cb_t event_cb;
    void *event_ctx;
    bool initialized;
    bool host_running;
    bool host_synced;
    bool start_requested;
    bool secure;               // encrypted + authenticated + bonded + 16-byte key
    bool notify_subscribed;
    uint16_t conn_handle;
    uint16_t tx_value_handle;
    uint8_t own_addr_type;
    char device_name[24];

    bm_link_tx_slot_t tx_slots[BM_LINK_TX_SLOTS];
    QueueHandle_t tx_free;
    QueueHandle_t tx_filled;
    SemaphoreHandle_t tx_free_sem;
    SemaphoreHandle_t tx_filled_sem;
    TaskHandle_t tx_task;
    uint32_t tx_drop;

    uint8_t rx_ring[BM_LINK_RX_RING];
    size_t rx_head;
    size_t rx_tail;
    volatile bool rx_signaled;   // one wake-up per drain, otherwise the event queue floods
    int64_t last_tx_drop_us;
} s_link;

static const ble_uuid128_t s_nus_service_uuid = BLE_UUID128_INIT(BM_NUS_SERVICE_UUID_BYTES);
static const ble_uuid128_t s_nus_rx_uuid = BLE_UUID128_INIT(BM_NUS_RX_UUID_BYTES);
static const ble_uuid128_t s_nus_tx_uuid = BLE_UUID128_INIT(BM_NUS_TX_UUID_BYTES);

static int bm_gap_event(struct ble_gap_event *event, void *arg);
static int bm_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static int bm_reconcile_advertising(void);
static void bm_tx_task(void *arg);

// One device pairs with one Mac: after a new peer bonds, forget the others, otherwise an
// old Mac that is still in range keeps taking the single connection slot.
static void bm_forget_other_bonds(const ble_addr_t *keep)
{
    ble_addr_t peers[MYNEWT_VAL(BLE_STORE_MAX_BONDS)];
    int num = 0;
    if (ble_store_util_bonded_peers(peers, &num, MYNEWT_VAL(BLE_STORE_MAX_BONDS)) != 0) return;
    for (int i = 0; i < num; i++) {
        if (ble_addr_cmp(&peers[i], keep) == 0) continue;
        if (ble_store_util_delete_peer(&peers[i]) == 0) {
            ESP_LOGI(TAG, "removed an older bonded peer");
        }
    }
}

static void bm_emit(const bm_link_event_t *ev)
{
    if (s_link.event_cb != NULL) s_link.event_cb(ev, s_link.event_ctx);
}

static bool link_ready_locked(void)
{
    return s_link.conn_handle != BLE_HS_CONN_HANDLE_NONE && s_link.secure && s_link.notify_subscribed;
}

// Passkey must be >= 100000: Android rejects values with leading zeros.
static int bm_inject_passkey(uint16_t conn_handle)
{
    uint32_t passkey = 100000U + (uint32_t)(esp_random() % 900000U);
    struct ble_sm_io io = { .action = BLE_SM_IOACT_DISP, .passkey = passkey };
    int rc = ble_sm_inject_io(conn_handle, &io);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_sm_inject_io failed: %d", rc);
        return rc;
    }
    bm_link_event_t ev = { .type = BM_LINK_EV_PASSKEY, .passkey = passkey };
    bm_emit(&ev);
    ESP_LOGI(TAG, "passkey shown on screen");
    return 0;
}

static int bm_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const char tag[4] = { 'B', 'M', '1', 0 };
        return os_mbuf_append(ctxt->om, tag, sizeof(tag)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    xSemaphoreTake(s_link.mutex, portMAX_DELAY);
    bool accept = (s_link.conn_handle == conn_handle) && s_link.secure;
    xSemaphoreGive(s_link.mutex);
    if (!accept) return BLE_ATT_ERR_INSUFFICIENT_ENC;

    size_t total = 0;
    for (struct os_mbuf *m = ctxt->om; m != NULL; m = SLIST_NEXT(m, om_next)) {
        if (m->om_len == 0) continue;
        xSemaphoreTake(s_link.mutex, portMAX_DELAY);
        size_t used = (s_link.rx_head + BM_LINK_RX_RING - s_link.rx_tail) % BM_LINK_RX_RING;
        size_t space = BM_LINK_RX_RING - 1U - used;
        size_t take = m->om_len < space ? m->om_len : space;
        for (size_t i = 0; i < take; i++) {
            s_link.rx_ring[(s_link.rx_head + i) % BM_LINK_RX_RING] = m->om_data[i];
        }
        s_link.rx_head = (s_link.rx_head + take) % BM_LINK_RX_RING;
        xSemaphoreGive(s_link.mutex);
        total += take;
        if (take < m->om_len) {
            ESP_LOGW(TAG, "RX ring full, dropped %u bytes", (unsigned)(m->om_len - take));
            break;
        }
    }
    if (total > 0 && !s_link.rx_signaled) {
        s_link.rx_signaled = true;
        bm_link_event_t ev = { .type = BM_LINK_EV_RX };
        bm_emit(&ev);
    }
    return 0;
}

static const struct ble_gatt_svc_def s_gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_nus_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                // Mac -> device: CONTROL / PUT_DATA frames
                .uuid = &s_nus_rx_uuid.u,
                .access_cb = bm_gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                // device -> Mac: EVENT / AUDIO_OPUS frames (notify)
                .uuid = &s_nus_tx_uuid.u,
                .access_cb = bm_gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY |
                         BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC,
                .val_handle = &s_link.tx_value_handle,
            },
            {0},
        },
    },
    {0},
};

// ESP-IDF 5.5.3 only adds WRITE_ENC to the generated CCCD. Wrap the registration
// (see the --wrap link flag in main/CMakeLists.txt) so it also gets READ_ENC, otherwise
// the central cannot read the CCCD and subscribing fails.
void ble_store_config_init(void);

typedef int bm_att_svr_access_fn_t(uint16_t conn_handle, uint16_t attr_handle, uint8_t att_op,
                                   uint16_t offset, struct os_mbuf **om, void *arg);

int __real_ble_att_svr_register(const ble_uuid_t *uuid, uint8_t flags, uint8_t min_key_size,
                                uint16_t *handle_id, bm_att_svr_access_fn_t *cb, void *cb_arg);

int __wrap_ble_att_svr_register(const ble_uuid_t *uuid, uint8_t flags, uint8_t min_key_size,
                                uint16_t *handle_id, bm_att_svr_access_fn_t *cb, void *cb_arg)
{
    if (uuid != NULL && ble_uuid_cmp(uuid, BLE_UUID16_DECLARE(0x2902)) == 0) {
        flags |= BLE_ATT_F_READ_AUTHEN | BLE_ATT_F_READ_ENC;
    }
    return __real_ble_att_svr_register(uuid, flags, min_key_size, handle_id, cb, cb_arg);
}

static int bm_reconcile_advertising(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields response_fields;
    struct ble_gap_adv_params params;

    xSemaphoreTake(s_link.mutex, portMAX_DELAY);
    bool desired = s_link.start_requested && s_link.host_synced &&
                   s_link.conn_handle == BLE_HS_CONN_HANDLE_NONE;
    xSemaphoreGive(s_link.mutex);

    if (!desired) return ble_gap_adv_active() ? ble_gap_adv_stop() : 0;
    if (ble_gap_adv_active()) return 0;

    // The service UUID is in the advertisement (the Mac filters on it), the name in the scan response.
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_nus_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;

    memset(&response_fields, 0, sizeof(response_fields));
    response_fields.name = (uint8_t *)s_link.device_name;
    response_fields.name_len = strlen(s_link.device_name);
    response_fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response_fields);
    if (rc != 0) return rc;

    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(s_link.own_addr_type, NULL, BLE_HS_FOREVER, &params, bm_gap_event, NULL);
}

static int bm_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        if (event->connect.status != 0) {
            ESP_LOGW(TAG, "connect failed status=%d", event->connect.status);
            return bm_reconcile_advertising();
        }
        xSemaphoreTake(s_link.mutex, portMAX_DELAY);
        if (s_link.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            xSemaphoreGive(s_link.mutex);
            return ble_gap_terminate(event->connect.conn_handle, BLE_ERR_CONN_LIMIT);
        }
        s_link.conn_handle = event->connect.conn_handle;
        s_link.secure = false;
        s_link.notify_subscribed = false;
        xSemaphoreGive(s_link.mutex);

        bm_link_event_t ev = { .type = BM_LINK_EV_CONNECTED };
        bm_emit(&ev);
        ESP_LOGI(TAG, "connected, starting pairing");
        // No keyboard: start security ourselves; the passkey is injected on PASSKEY_ACTION.
        int rc = ble_gap_security_initiate(event->connect.conn_handle);
        if (rc != 0) ESP_LOGE(TAG, "ble_gap_security_initiate failed: %d", rc);
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT: {
        xSemaphoreTake(s_link.mutex, portMAX_DELAY);
        bool was_connected = s_link.conn_handle == event->disconnect.conn.conn_handle;
        if (was_connected) {
            s_link.conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_link.secure = false;
            s_link.notify_subscribed = false;
        }
        xSemaphoreGive(s_link.mutex);
        if (was_connected) {
            bm_link_event_t ev = { .type = BM_LINK_EV_DISCONNECTED };
            bm_emit(&ev);
            ESP_LOGI(TAG, "disconnected reason=%d", event->disconnect.reason);
        }
        return bm_reconcile_advertising();
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        return bm_reconcile_advertising();

    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc desc = {0};
        bool publish = false;
        if (event->enc_change.status == 0 &&
            ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
            xSemaphoreTake(s_link.mutex, portMAX_DELAY);
            if (s_link.conn_handle == event->enc_change.conn_handle) {
                s_link.secure = desc.sec_state.encrypted && desc.sec_state.authenticated &&
                                desc.sec_state.bonded && (desc.sec_state.key_size == 16U);
                publish = true;
            }
            xSemaphoreGive(s_link.mutex);
        }
        if (publish) {
            ESP_LOGI(TAG, "encryption enc=%d auth=%d bonded=%d key=%u", desc.sec_state.encrypted,
                     desc.sec_state.authenticated, desc.sec_state.bonded, desc.sec_state.key_size);
            if (s_link.secure) bm_forget_other_bonds(&desc.peer_id_addr);
            bm_link_event_t ev = { .type = BM_LINK_EV_SECURED };
            bm_emit(&ev);
        } else {
            ESP_LOGW(TAG, "encryption failed status=%d", event->enc_change.status);
        }
        return 0;
    }

    case BLE_GAP_EVENT_SUBSCRIBE: {
        if (event->subscribe.attr_handle != s_link.tx_value_handle) return 0;
        xSemaphoreTake(s_link.mutex, portMAX_DELAY);
        if (s_link.conn_handle == event->subscribe.conn_handle) {
            s_link.notify_subscribed = event->subscribe.cur_notify != 0;
        }
        xSemaphoreGive(s_link.mutex);
        bm_link_event_t ev = { .type = BM_LINK_EV_SUBSCRIBED };
        bm_emit(&ev);
        ESP_LOGI(TAG, "notify subscribed=%d", event->subscribe.cur_notify != 0);
        return 0;
    }

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            return bm_inject_passkey(event->passkey.conn_handle);
        }
        return BLE_HS_ENOTSUP;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // The Mac lost its bond (or keys differ): delete ours and retry once.
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) != 0) {
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        return ble_store_util_delete_peer(&desc.peer_id_addr) == 0 ? BLE_GAP_REPEAT_PAIRING_RETRY
                                                                   : BLE_GAP_REPEAT_PAIRING_IGNORE;
    }

    default:
        return 0;
    }
}

// ---- TX: ring + dedicated task so the audio task never waits on the radio ----
static esp_err_t bm_notify_frame(const uint8_t *data, size_t len)
{
    xSemaphoreTake(s_link.mutex, portMAX_DELAY);
    uint16_t conn = s_link.conn_handle;
    bool ok = link_ready_locked();
    xSemaphoreGive(s_link.mutex);
    if (!ok) return ESP_ERR_INVALID_STATE;

    uint16_t mtu = ble_att_mtu(conn);
    size_t frag = mtu > 3U ? (size_t)mtu - 3U : 0;
    if (frag > BM_LINK_TX_CHUNK_MAX) frag = BM_LINK_TX_CHUNK_MAX;
    if (frag == 0) return ESP_ERR_INVALID_STATE;

    for (size_t off = 0; off < len;) {
        size_t chunk = len - off < frag ? len - off : frag;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data + off, (uint16_t)chunk);
        if (om == NULL) return ESP_ERR_NO_MEM;
        if (ble_gatts_notify_custom(conn, s_link.tx_value_handle, om) != 0) return ESP_ERR_NO_MEM;
        off += chunk;
    }
    return ESP_OK;
}

static void bm_tx_task(void *arg)
{
    (void)arg;
    int idx = 0;
    for (;;) {
        if (xSemaphoreTake(s_link.tx_filled_sem, portMAX_DELAY) != pdTRUE) continue;
        if (xQueueReceive(s_link.tx_filled, &idx, portMAX_DELAY) != pdTRUE) continue;
        bm_link_tx_slot_t *slot = &s_link.tx_slots[idx];
        if (bm_notify_frame(slot->data, slot->len) != ESP_OK) {
            s_link.tx_drop++;
            int64_t now = esp_timer_get_time();
            if (now - s_link.last_tx_drop_us > 1000000) {
                s_link.last_tx_drop_us = now;
                bm_link_event_t ev = { .type = BM_LINK_EV_TX_DROP };
                bm_emit(&ev);
            }
        }
        xQueueSend(s_link.tx_free, &idx, 0);
        xSemaphoreGive(s_link.tx_free_sem);
    }
}

esp_err_t bm_link_send(const uint8_t *frame, size_t len)
{
    if (frame == NULL || len == 0 || len > BM_LINK_TX_SLOT_MAX) return ESP_ERR_INVALID_ARG;
    if (!s_link.initialized) return ESP_ERR_INVALID_STATE;
    if (!bm_link_ready()) return ESP_ERR_INVALID_STATE;

    int idx = 0;
    if (xSemaphoreTake(s_link.tx_free_sem, 0) != pdTRUE ||
        xQueueReceive(s_link.tx_free, &idx, 0) != pdTRUE) {
        s_link.tx_drop++;
        return ESP_ERR_NO_MEM;   // full: drop this frame, never make the caller wait
    }
    bm_link_tx_slot_t *slot = &s_link.tx_slots[idx];
    memcpy(slot->data, frame, len);
    slot->len = len;
    xQueueSend(s_link.tx_filled, &idx, 0);
    xSemaphoreGive(s_link.tx_filled_sem);
    return ESP_OK;
}

uint32_t bm_link_tx_drop_count(void)
{
    return s_link.tx_drop;
}

size_t bm_link_read(uint8_t *out, size_t cap)
{
    if (out == NULL || cap == 0 || !s_link.initialized || s_link.mutex == NULL) return 0;
    xSemaphoreTake(s_link.mutex, portMAX_DELAY);
    size_t avail = (s_link.rx_head + BM_LINK_RX_RING - s_link.rx_tail) % BM_LINK_RX_RING;
    size_t n = avail < cap ? avail : cap;
    for (size_t i = 0; i < n; i++) out[i] = s_link.rx_ring[(s_link.rx_tail + i) % BM_LINK_RX_RING];
    s_link.rx_tail = (s_link.rx_tail + n) % BM_LINK_RX_RING;
    if (n == 0) s_link.rx_signaled = false;   // drained: the next write may wake us again
    xSemaphoreGive(s_link.mutex);
    return n;
}

void bm_link_flush_rx(void)
{
    if (!s_link.initialized) return;
    xSemaphoreTake(s_link.mutex, portMAX_DELAY);
    s_link.rx_tail = s_link.rx_head;
    s_link.rx_signaled = false;
    xSemaphoreGive(s_link.mutex);
}

const char *bm_link_device_name(void)
{
    return s_link.device_name;
}

bool bm_link_ready(void)
{
    if (!s_link.initialized) return false;
    xSemaphoreTake(s_link.mutex, portMAX_DELAY);
    bool ready = link_ready_locked();
    xSemaphoreGive(s_link.mutex);
    return ready;
}

bool bm_link_connected(void)
{
    if (!s_link.initialized) return false;
    xSemaphoreTake(s_link.mutex, portMAX_DELAY);
    bool connected = s_link.conn_handle != BLE_HS_CONN_HANDLE_NONE;
    xSemaphoreGive(s_link.mutex);
    return connected;
}

void bm_link_disconnect(void)
{
    if (!s_link.initialized) return;
    xSemaphoreTake(s_link.mutex, portMAX_DELAY);
    s_link.start_requested = false;
    uint16_t conn = s_link.conn_handle;
    xSemaphoreGive(s_link.mutex);
    if (ble_gap_adv_active()) ble_gap_adv_stop();
    if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}

// ---- host lifecycle ----
static void bm_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_link.own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "address selection failed: %d", rc);
        return;
    }
    xSemaphoreTake(s_link.mutex, portMAX_DELAY);
    s_link.host_synced = true;
    xSemaphoreGive(s_link.mutex);
    int adv = bm_reconcile_advertising();
    if (adv != 0) ESP_LOGE(TAG, "advertising failed: %d", adv);
}

static void bm_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset: %d", reason);
    xSemaphoreTake(s_link.mutex, portMAX_DELAY);
    s_link.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_link.secure = false;
    s_link.notify_subscribed = false;
    s_link.host_synced = false;
    xSemaphoreGive(s_link.mutex);
}

static void bm_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static int bm_gatt_init(void)
{
    ble_hs_cfg.reset_cb = bm_on_reset;
    ble_hs_cfg.sync_cb = bm_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Display only: the device shows a random passkey, the Mac user types it.
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_sc_only = 0;   // relaxed for stack compatibility; the link is only "ready" when authenticated
    ble_hs_cfg.sm_sec_lvl = 4;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_store_config_init();   // NVS-backed bond storage
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_gatt_services);
    if (rc == 0) rc = ble_gatts_add_svcs(s_gatt_services);
    if (rc == 0) rc = ble_svc_gap_device_name_set(s_link.device_name);
    return rc;
}

esp_err_t bm_link_init(bm_link_event_cb_t cb, void *ctx)
{
    if (cb == NULL) return ESP_ERR_INVALID_ARG;
    memset(&s_link, 0, sizeof(s_link));
    s_link.event_cb = cb;
    s_link.event_ctx = ctx;
    s_link.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_link.mutex = xSemaphoreCreateMutex();
    s_link.tx_free = xQueueCreate(BM_LINK_TX_SLOTS, sizeof(int));
    s_link.tx_filled = xQueueCreate(BM_LINK_TX_SLOTS, sizeof(int));
    s_link.tx_free_sem = xSemaphoreCreateCounting(BM_LINK_TX_SLOTS, BM_LINK_TX_SLOTS);
    s_link.tx_filled_sem = xSemaphoreCreateCounting(BM_LINK_TX_SLOTS, 0);
    if (!s_link.mutex || !s_link.tx_free || !s_link.tx_filled || !s_link.tx_free_sem ||
        !s_link.tx_filled_sem) {
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < (int)BM_LINK_TX_SLOTS; i++) xQueueSend(s_link.tx_free, &i, 0);

    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_BT) != ESP_OK) esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_link.device_name, sizeof(s_link.device_name), BM_LINK_NAME_PREFIX "%02X%02X", mac[4], mac[5]);

    s_link.initialized = true;
    ESP_LOGI(TAG, "link ready, advertising name %s", s_link.device_name);
    return ESP_OK;
}

esp_err_t bm_link_start(void)
{
    if (!s_link.initialized) return ESP_ERR_INVALID_STATE;

    // Bond storage and the PHY calibration cache live in NVS; it must be up before the radio.
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs = nvs_flash_init();
    }
    if (nvs != ESP_OK) ESP_LOGW(TAG, "nvs_flash_init: %s", esp_err_to_name(nvs));

    xSemaphoreTake(s_link.mutex, portMAX_DELAY);
    s_link.start_requested = true;
    bool launch_host = !s_link.host_running;
    if (launch_host) s_link.host_running = true;
    xSemaphoreGive(s_link.mutex);

    if (!launch_host) return bm_reconcile_advertising();

    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        goto fail;
    }
    rc = bm_gatt_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT registration failed: %d", rc);
        goto fail;
    }
    if (xTaskCreate(bm_tx_task, "bm_link_tx", BM_LINK_TX_TASK_STACK, NULL, BM_LINK_TX_TASK_PRIO,
                    &s_link.tx_task) != pdPASS) {
        rc = ESP_ERR_NO_MEM;
        goto fail;
    }
    nimble_port_freertos_init(bm_host_task);
    return ESP_OK;

fail:
    xSemaphoreTake(s_link.mutex, portMAX_DELAY);
    s_link.host_running = false;
    xSemaphoreGive(s_link.mutex);
    return ESP_FAIL;
}
