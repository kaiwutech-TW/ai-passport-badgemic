#include "bm_input.h"

void bm_input_init(bm_input_t *in)
{
    in->held = -1;
    in->press_ms = 0;
    in->hold_fired = false;
    for (int i = 0; i < BM_K_COUNT; i++) in->ignored[i] = false;
    in->have_release = false;
    in->last_release_key = BM_K_UP;
    in->last_release_ms = 0;
}

static bm_in_ev_t ev(bm_in_kind_t kind, bm_key_t key)
{
    bm_in_ev_t e = {kind, key};
    return e;
}

bm_in_ev_t bm_input_press(bm_input_t *in, bm_key_t key, uint32_t now_ms)
{
    in->ignored[key] = false;
    if (in->held >= 0) {
        in->ignored[key] = true;  // another key is down: contact bounce / ladder sweep
        return ev(BM_IN_NONE, key);
    }
    if (in->have_release && in->last_release_key != key &&
        now_ms - in->last_release_ms < BM_KEY_CROSS_GUARD_MS) {
        in->ignored[key] = true;
        return ev(BM_IN_NONE, key);
    }
    in->held = (int)key;
    in->press_ms = now_ms;
    in->hold_fired = false;
    return ev(key == BM_K_OK ? BM_IN_OK_DOWN : BM_IN_NONE, key);
}

bm_in_ev_t bm_input_release(bm_input_t *in, bm_key_t key, uint32_t now_ms)
{
    if (in->ignored[key]) {
        in->ignored[key] = false;
        return ev(BM_IN_NONE, key);
    }
    if (in->held != (int)key) return ev(BM_IN_NONE, key);
    uint32_t dur = now_ms - in->press_ms;
    bool fired = in->hold_fired;
    in->held = -1;
    in->hold_fired = false;
    in->have_release = true;
    in->last_release_key = key;
    in->last_release_ms = now_ms;
    if (key == BM_K_OK) {
        if (fired) return ev(BM_IN_OK_UP, key);
        if (dur < BM_KEY_MIN_PRESS_MS) return ev(BM_IN_OK_NOISE, key);
        if (dur < BM_OK_HOLD_MS) return ev(BM_IN_OK_TAP, key);
        return ev(BM_IN_OK_UP, key);
    }
    if (dur < BM_KEY_MIN_PRESS_MS) return ev(BM_IN_NONE, key);
    return ev(dur >= BM_KEY_LONG_MS ? BM_IN_LONG : BM_IN_CLICK, key);
}

bm_in_ev_t bm_input_tick(bm_input_t *in, uint32_t now_ms)
{
    if (in->held < 0) return ev(BM_IN_NONE, BM_K_UP);
    bm_key_t key = (bm_key_t)in->held;
    uint32_t dur = now_ms - in->press_ms;
    if (dur >= BM_KEY_STUCK_MS) {  // lost release event: never stay held forever
        bool fired = in->hold_fired;
        in->held = -1;
        in->hold_fired = false;
        return ev(key == BM_K_OK && fired ? BM_IN_OK_UP : BM_IN_NONE, key);
    }
    if (key == BM_K_OK && !in->hold_fired && dur >= BM_OK_HOLD_MS) {
        in->hold_fired = true;
        return ev(BM_IN_OK_HOLD, key);
    }
    return ev(BM_IN_NONE, key);
}
