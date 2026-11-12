#include "bm_ptt.h"

static bm_ptt_out_t out(bm_ptt_act_t act, bm_ptt_reason_t reason)
{
    bm_ptt_out_t o = {act, reason};
    return o;
}

void bm_ptt_init(bm_ptt_t *t)
{
    t->state = BM_PTT_IDLE;
    t->id = 0;
    t->start_ms = 0;
    t->frames = t->dropped = 0;
    t->live = false;
}

bm_ptt_out_t bm_ptt_ok_down(bm_ptt_t *t)
{
    if (t->state != BM_PTT_IDLE) return out(BM_PTT_ACT_NONE, BM_PTT_REASON_NONE);
    t->state = BM_PTT_PRESSED;
    return out(BM_PTT_ACT_START_CAPTURE, BM_PTT_REASON_NONE);
}

bm_ptt_out_t bm_ptt_ok_noise(bm_ptt_t *t)
{
    if (t->state != BM_PTT_PRESSED) return out(BM_PTT_ACT_NONE, BM_PTT_REASON_NONE);
    t->state = BM_PTT_IDLE;
    return out(BM_PTT_ACT_DISCARD, BM_PTT_REASON_NONE);
}

bm_ptt_out_t bm_ptt_ok_tap(bm_ptt_t *t)
{
    if (t->state != BM_PTT_PRESSED) return out(BM_PTT_ACT_NONE, BM_PTT_REASON_NONE);
    t->state = BM_PTT_IDLE;
    return out(BM_PTT_ACT_TAP, BM_PTT_REASON_NONE);
}

bm_ptt_out_t bm_ptt_ok_hold(bm_ptt_t *t, bool link_ready, uint32_t now_ms)
{
    if (t->state != BM_PTT_PRESSED) return out(BM_PTT_ACT_NONE, BM_PTT_REASON_NONE);
    if (!link_ready) {
        t->state = BM_PTT_IDLE;
        return out(BM_PTT_ACT_NOT_CONNECTED, BM_PTT_REASON_NONE);
    }
    t->state = BM_PTT_TALKING;
    t->id++;
    t->start_ms = now_ms;
    t->frames = t->dropped = 0;
    t->live = false;
    return out(BM_PTT_ACT_START_TURN, BM_PTT_REASON_NONE);
}

bm_ptt_out_t bm_ptt_ok_up(bm_ptt_t *t)
{
    if (t->state == BM_PTT_TALKING) {
        t->state = BM_PTT_IDLE;
        return out(BM_PTT_ACT_END_TURN, BM_PTT_REASON_NONE);
    }
    if (t->state == BM_PTT_PRESSED) {  // release without a reported hold: treat as tap
        t->state = BM_PTT_IDLE;
        return out(BM_PTT_ACT_TAP, BM_PTT_REASON_NONE);
    }
    return out(BM_PTT_ACT_NONE, BM_PTT_REASON_NONE);
}

bm_ptt_out_t bm_ptt_tick(bm_ptt_t *t, bool link_ready, uint32_t now_ms)
{
    if (t->state == BM_PTT_PRESSED && !link_ready) {
        // keep capturing; the hold decision happens at 300 ms
        return out(BM_PTT_ACT_NONE, BM_PTT_REASON_NONE);
    }
    if (t->state != BM_PTT_TALKING) return out(BM_PTT_ACT_NONE, BM_PTT_REASON_NONE);
    if (!link_ready) {
        t->state = BM_PTT_IDLE;
        return out(BM_PTT_ACT_END_TURN, BM_PTT_REASON_LINK);
    }
    if (now_ms - t->start_ms >= BM_PTT_MAX_MS) {
        t->state = BM_PTT_IDLE;
        return out(BM_PTT_ACT_END_TURN, BM_PTT_REASON_TIMEOUT);
    }
    return out(BM_PTT_ACT_NONE, BM_PTT_REASON_NONE);
}

bool bm_ptt_ack(bm_ptt_t *t, uint32_t id)
{
    if (t->state != BM_PTT_TALKING || id != t->id) return false;
    t->live = true;
    return true;
}

bool bm_ptt_busy(const bm_ptt_t *t)
{
    return t->state != BM_PTT_IDLE;
}

void bm_banner_init(bm_banner_t *b)
{
    b->kind = BM_BANNER_NONE;
    b->deadline_ms = 0;
}

void bm_banner_show(bm_banner_t *b, bm_banner_kind_t kind, uint32_t now_ms)
{
    b->kind = kind;
    if (kind == BM_BANNER_SENT) b->deadline_ms = now_ms + BM_SENT_BANNER_MS;
    else if (kind == BM_BANNER_NOT_CONNECTED) b->deadline_ms = now_ms + BM_NOT_CONNECTED_BANNER_MS;
    else b->deadline_ms = 0;
}

bool bm_banner_tick(bm_banner_t *b, uint32_t now_ms)
{
    if (b->deadline_ms != 0 && (int32_t)(now_ms - b->deadline_ms) >= 0) {
        b->kind = BM_BANNER_NONE;
        b->deadline_ms = 0;
        return true;
    }
    return false;
}
