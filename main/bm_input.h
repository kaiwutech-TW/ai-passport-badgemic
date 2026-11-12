// main/bm_input.h - key gesture classifier (pure C).
//
// OK: press starts capture (OK_DOWN); release < 300 ms = OK_TAP; 300 ms reached
// = OK_HOLD; release after that = OK_UP. UP/DOWN: click / long (>= 600 ms),
// reported on release. Electrical-noise guards (ADC ladder, see
// docs/reference/shinku-chen/adc-ladder-keypad-long-press-misread.md): a press
// while another key is held is ignored, a different key pressed within
// BM_KEY_CROSS_GUARD_MS of a release is ignored, and presses shorter than
// BM_KEY_MIN_PRESS_MS are noise.
//
// Call bm_input_tick(ts) with the release timestamp before bm_input_release(ts)
// so a hold that crossed 300 ms while the app task was busy is reported first.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BM_OK_HOLD_MS 300u
#define BM_KEY_LONG_MS 600u
#define BM_KEY_MIN_PRESS_MS 30u
#define BM_KEY_CROSS_GUARD_MS 80u
#define BM_KEY_STUCK_MS 130000u

typedef enum { BM_K_UP = 0, BM_K_DOWN, BM_K_OK, BM_K_COUNT } bm_key_t;

typedef enum {
    BM_IN_NONE = 0,
    BM_IN_OK_DOWN,
    BM_IN_OK_TAP,
    BM_IN_OK_HOLD,
    BM_IN_OK_UP,
    BM_IN_OK_NOISE,
    BM_IN_CLICK,
    BM_IN_LONG,
} bm_in_kind_t;

typedef struct {
    bm_in_kind_t kind;
    bm_key_t key;
} bm_in_ev_t;

typedef struct {
    int held;  // -1 or bm_key_t
    uint32_t press_ms;
    bool hold_fired;
    bool ignored[BM_K_COUNT];
    bool have_release;
    bm_key_t last_release_key;
    uint32_t last_release_ms;
} bm_input_t;

void bm_input_init(bm_input_t *in);
bm_in_ev_t bm_input_press(bm_input_t *in, bm_key_t key, uint32_t now_ms);
bm_in_ev_t bm_input_release(bm_input_t *in, bm_key_t key, uint32_t now_ms);
bm_in_ev_t bm_input_tick(bm_input_t *in, uint32_t now_ms);
