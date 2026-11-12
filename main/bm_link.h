// main/bm_link.h - NimBLE Nordic UART peripheral for Badge Mic.
// Ported from the MIT-licensed Pocket Intercom oc_link by Shinku-Chen.
//
// The link only moves bytes; framing lives in bm_proto. "Ready" means connected +
// encrypted (MITM, bonded, 16-byte key) + notifications subscribed. Callbacks run in
// the NimBLE host task: copy / enqueue only.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define BM_LINK_NAME_PREFIX "Passport-"

typedef enum {
    BM_LINK_EV_CONNECTED,
    BM_LINK_EV_DISCONNECTED,
    BM_LINK_EV_SECURED,
    BM_LINK_EV_SUBSCRIBED,   // notifications enabled or disabled; check bm_link_ready()
    BM_LINK_EV_PASSKEY,
    BM_LINK_EV_RX,           // bytes are waiting in the RX ring
    BM_LINK_EV_TX_DROP,      // rate-limited: a frame was dropped (not ready / back-pressure)
} bm_link_event_type_t;

typedef struct {
    bm_link_event_type_t type;
    uint32_t passkey;        // BM_LINK_EV_PASSKEY
} bm_link_event_t;

typedef void (*bm_link_event_cb_t)(const bm_link_event_t *ev, void *ctx);

esp_err_t bm_link_init(bm_link_event_cb_t cb, void *ctx);
esp_err_t bm_link_start(void);          // NimBLE host + GATT + advertising
void bm_link_disconnect(void);          // terminate the link and stop advertising
bool bm_link_ready(void);
bool bm_link_connected(void);
// Queues one complete frame (magic header included). Never blocks.
esp_err_t bm_link_send(const uint8_t *frame, size_t len);
size_t bm_link_read(uint8_t *out, size_t cap);
void bm_link_flush_rx(void);
const char *bm_link_device_name(void);
uint32_t bm_link_tx_drop_count(void);
