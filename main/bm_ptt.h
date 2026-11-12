// main/bm_ptt.h - push-to-talk turn state machine and recording banner (pure C).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BM_PTT_MAX_MS 120000u
#define BM_SENT_BANNER_MS 800u
#define BM_NOT_CONNECTED_BANNER_MS 1500u

typedef enum { BM_PTT_IDLE = 0, BM_PTT_PRESSED, BM_PTT_TALKING } bm_ptt_state_t;

typedef enum {
    BM_PTT_ACT_NONE = 0,
    BM_PTT_ACT_START_CAPTURE,  // OK down: start encoding into the pre-roll
    BM_PTT_ACT_DISCARD,        // noise: stop capture, drop audio, nothing else
    BM_PTT_ACT_TAP,            // stop capture, drop audio, cycle page
    BM_PTT_ACT_NOT_CONNECTED,  // 300 ms reached, link not ready: stop, show banner
    BM_PTT_ACT_START_TURN,     // send ptt_start, flush pre-roll, go live
    BM_PTT_ACT_END_TURN,       // stop capture, flush, send ptt_end(reason)
} bm_ptt_act_t;

typedef enum { BM_PTT_REASON_NONE = 0, BM_PTT_REASON_TIMEOUT, BM_PTT_REASON_LINK } bm_ptt_reason_t;

typedef struct {
    bm_ptt_act_t act;
    bm_ptt_reason_t reason;
} bm_ptt_out_t;

typedef struct {
    bm_ptt_state_t state;
    uint32_t id;       // id of the current / last turn
    uint32_t start_ms;
    uint32_t frames;   // frames handed to the link
    uint32_t dropped;  // frames lost (pre-roll overflow, link back-pressure)
    bool live;         // ptt_ack received
} bm_ptt_t;

void bm_ptt_init(bm_ptt_t *t);
bm_ptt_out_t bm_ptt_ok_down(bm_ptt_t *t);
bm_ptt_out_t bm_ptt_ok_noise(bm_ptt_t *t);
bm_ptt_out_t bm_ptt_ok_tap(bm_ptt_t *t);
bm_ptt_out_t bm_ptt_ok_hold(bm_ptt_t *t, bool link_ready, uint32_t now_ms);
bm_ptt_out_t bm_ptt_ok_up(bm_ptt_t *t);
// Enforces the 120 s cap and ends the turn when the link drops.
bm_ptt_out_t bm_ptt_tick(bm_ptt_t *t, bool link_ready, uint32_t now_ms);
// Returns true when the ack matched the running turn.
bool bm_ptt_ack(bm_ptt_t *t, uint32_t id);
bool bm_ptt_busy(const bm_ptt_t *t);

// Full-screen state shown over the badge.
typedef enum {
    BM_BANNER_NONE = 0,
    BM_BANNER_CAPTURING,
    BM_BANNER_LIVE,
    BM_BANNER_SENT,
    BM_BANNER_NOT_CONNECTED,
} bm_banner_kind_t;

typedef struct {
    bm_banner_kind_t kind;
    uint32_t deadline_ms;  // 0 = no deadline
} bm_banner_t;

void bm_banner_init(bm_banner_t *b);
void bm_banner_show(bm_banner_t *b, bm_banner_kind_t kind, uint32_t now_ms);
// Returns true when a timed banner just expired (kind is NONE afterwards).
bool bm_banner_tick(bm_banner_t *b, uint32_t now_ms);
