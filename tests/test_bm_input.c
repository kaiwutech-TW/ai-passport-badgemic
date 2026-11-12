// Host tests: key gestures, PTT turn state machine, banners.
#include <assert.h>
#include <stdio.h>

#include "bm_input.h"
#include "bm_ptt.h"

static bm_in_ev_t press(bm_input_t *i, bm_key_t k, uint32_t t) { return bm_input_press(i, k, t); }
static bm_in_ev_t release(bm_input_t *i, bm_key_t k, uint32_t t)
{
    bm_in_ev_t h = bm_input_tick(i, t);   // app contract: tick(ts) before release(ts)
    if (h.kind != BM_IN_NONE) {
        assert(h.kind == BM_IN_OK_HOLD);
        bm_in_ev_t up = bm_input_release(i, k, t);
        assert(up.kind == BM_IN_OK_UP);
        return h;   // caller only cares that HOLD fired first
    }
    return bm_input_release(i, k, t);
}

static void test_ok_gestures(void)
{
    bm_input_t in;
    bm_input_init(&in);
    assert(press(&in, BM_K_OK, 1000).kind == BM_IN_OK_DOWN);
    assert(bm_input_tick(&in, 1299).kind == BM_IN_NONE);
    assert(release(&in, BM_K_OK, 1299).kind == BM_IN_OK_TAP);       // 299 ms: tap

    assert(press(&in, BM_K_OK, 5000).kind == BM_IN_OK_DOWN);
    assert(bm_input_tick(&in, 5300).kind == BM_IN_OK_HOLD);          // exactly 300 ms
    assert(bm_input_tick(&in, 5400).kind == BM_IN_NONE);             // fires once
    assert(release(&in, BM_K_OK, 9000).kind == BM_IN_OK_UP);

    // app was busy: release arrives with a 450 ms timestamp, hold still reported first
    assert(press(&in, BM_K_OK, 20000).kind == BM_IN_OK_DOWN);
    assert(release(&in, BM_K_OK, 20450).kind == BM_IN_OK_HOLD);

    // noise: 10 ms blip
    assert(press(&in, BM_K_OK, 30000).kind == BM_IN_OK_DOWN);
    assert(release(&in, BM_K_OK, 30010).kind == BM_IN_OK_NOISE);
}

static void test_up_down_gestures(void)
{
    bm_input_t in;
    bm_input_init(&in);
    assert(press(&in, BM_K_UP, 1000).kind == BM_IN_NONE);
    bm_in_ev_t e = release(&in, BM_K_UP, 1200);
    assert(e.kind == BM_IN_CLICK && e.key == BM_K_UP);
    assert(press(&in, BM_K_DOWN, 3000).kind == BM_IN_NONE);
    e = release(&in, BM_K_DOWN, 3599);
    assert(e.kind == BM_IN_CLICK && e.key == BM_K_DOWN);
    assert(press(&in, BM_K_DOWN, 5000).kind == BM_IN_NONE);
    e = release(&in, BM_K_DOWN, 5600);
    assert(e.kind == BM_IN_LONG && e.key == BM_K_DOWN);               // 600 ms = long, on release
    assert(press(&in, BM_K_UP, 8000).kind == BM_IN_NONE);
    assert(release(&in, BM_K_UP, 8010).kind == BM_IN_NONE);           // noise
}

static void test_ladder_noise_guards(void)
{
    bm_input_t in;
    bm_input_init(&in);
    // UP held; contact break sweeps through DOWN and OK windows
    assert(press(&in, BM_K_UP, 1000).kind == BM_IN_NONE);
    assert(press(&in, BM_K_OK, 1100).kind == BM_IN_NONE);            // ignored: UP is held
    assert(release(&in, BM_K_OK, 1160).kind == BM_IN_NONE);
    bm_in_ev_t e = release(&in, BM_K_UP, 1500);
    assert(e.kind == BM_IN_CLICK);

    // UP released, then the sweep reports OK 20 ms later: cross-key guard
    assert(press(&in, BM_K_OK, 1520).kind == BM_IN_NONE);
    assert(release(&in, BM_K_OK, 1600).kind == BM_IN_NONE);
    // a deliberate OK press after the guard window works
    assert(press(&in, BM_K_OK, 2200).kind == BM_IN_OK_DOWN);
    assert(release(&in, BM_K_OK, 2400).kind == BM_IN_OK_TAP);
    // same key again right away is allowed
    assert(press(&in, BM_K_OK, 2410).kind == BM_IN_OK_DOWN);
    assert(release(&in, BM_K_OK, 2500).kind == BM_IN_OK_TAP);
    // stray release without press
    assert(release(&in, BM_K_DOWN, 9000).kind == BM_IN_NONE);
}

static void test_stuck_key(void)
{
    bm_input_t in;
    bm_input_init(&in);
    press(&in, BM_K_OK, 0);
    assert(bm_input_tick(&in, 300).kind == BM_IN_OK_HOLD);
    assert(bm_input_tick(&in, BM_KEY_STUCK_MS).kind == BM_IN_OK_UP);
    assert(press(&in, BM_K_UP, BM_KEY_STUCK_MS + 1000).kind == BM_IN_NONE);
    assert(release(&in, BM_K_UP, BM_KEY_STUCK_MS + 1100).kind == BM_IN_CLICK);
}

static void test_ptt_flow(void)
{
    bm_ptt_t t;
    bm_ptt_init(&t);
    assert(bm_ptt_ok_down(&t).act == BM_PTT_ACT_START_CAPTURE && bm_ptt_busy(&t));
    assert(bm_ptt_ok_down(&t).act == BM_PTT_ACT_NONE);
    assert(bm_ptt_ok_tap(&t).act == BM_PTT_ACT_TAP && !bm_ptt_busy(&t));
    assert(bm_ptt_ok_tap(&t).act == BM_PTT_ACT_NONE);

    bm_ptt_ok_down(&t);
    assert(bm_ptt_ok_noise(&t).act == BM_PTT_ACT_DISCARD && !bm_ptt_busy(&t));

    // hold with link: turn starts, id increments, ack goes live, release ends
    bm_ptt_ok_down(&t);
    assert(bm_ptt_ok_hold(&t, true, 1000).act == BM_PTT_ACT_START_TURN);
    assert(t.id == 1 && t.state == BM_PTT_TALKING && !t.live);
    assert(!bm_ptt_ack(&t, 2) && bm_ptt_ack(&t, 1) && t.live);
    assert(bm_ptt_tick(&t, true, 5000).act == BM_PTT_ACT_NONE);
    bm_ptt_out_t o = bm_ptt_ok_up(&t);
    assert(o.act == BM_PTT_ACT_END_TURN && o.reason == BM_PTT_REASON_NONE && !bm_ptt_busy(&t));
    assert(!bm_ptt_ack(&t, 1));
    assert(bm_ptt_ok_up(&t).act == BM_PTT_ACT_NONE);

    // hold without link: nothing is sent, no id consumed
    bm_ptt_ok_down(&t);
    assert(bm_ptt_ok_hold(&t, false, 9000).act == BM_PTT_ACT_NOT_CONNECTED);
    assert(t.id == 1 && !bm_ptt_busy(&t));
    assert(bm_ptt_ok_up(&t).act == BM_PTT_ACT_NONE);

    // 120 s cap
    bm_ptt_ok_down(&t);
    bm_ptt_ok_hold(&t, true, 10000);
    assert(t.id == 2);
    assert(bm_ptt_tick(&t, true, 10000 + BM_PTT_MAX_MS - 1).act == BM_PTT_ACT_NONE);
    o = bm_ptt_tick(&t, true, 10000 + BM_PTT_MAX_MS);
    assert(o.act == BM_PTT_ACT_END_TURN && o.reason == BM_PTT_REASON_TIMEOUT && !bm_ptt_busy(&t));
    assert(bm_ptt_ok_up(&t).act == BM_PTT_ACT_NONE);   // later physical release is inert

    // link drop mid-turn
    bm_ptt_ok_down(&t);
    bm_ptt_ok_hold(&t, true, 200000);
    o = bm_ptt_tick(&t, false, 201000);
    assert(o.act == BM_PTT_ACT_END_TURN && o.reason == BM_PTT_REASON_LINK);

    // link not ready while merely pressed does not end anything early
    bm_ptt_ok_down(&t);
    assert(bm_ptt_tick(&t, false, 300000).act == BM_PTT_ACT_NONE && t.state == BM_PTT_PRESSED);
    bm_ptt_ok_up(&t);

    // regression (on-device 2026-11-13): periodic() samples "now" once, then a hold sets
    // start_ms from a later clock read; a tick with that earlier "now" must not time out.
    bm_ptt_ok_down(&t);
    bm_ptt_ok_hold(&t, true, 400005);
    assert(bm_ptt_tick(&t, true, 400000).act == BM_PTT_ACT_NONE && t.state == BM_PTT_TALKING);
    assert(bm_ptt_tick(&t, true, 400005 + 3000).act == BM_PTT_ACT_NONE);
    assert(bm_ptt_ok_up(&t).act == BM_PTT_ACT_END_TURN);
}

static void test_banner(void)
{
    bm_banner_t b;
    bm_banner_init(&b);
    bm_banner_show(&b, BM_BANNER_LIVE, 100);
    assert(!bm_banner_tick(&b, 999999) && b.kind == BM_BANNER_LIVE);
    bm_banner_show(&b, BM_BANNER_SENT, 1000);
    assert(!bm_banner_tick(&b, 1799) && b.kind == BM_BANNER_SENT);
    assert(bm_banner_tick(&b, 1800) && b.kind == BM_BANNER_NONE);
    assert(!bm_banner_tick(&b, 5000));
    bm_banner_show(&b, BM_BANNER_NOT_CONNECTED, 2000);
    assert(!bm_banner_tick(&b, 3499) && bm_banner_tick(&b, 3500) && b.kind == BM_BANNER_NONE);
}

int main(void)
{
    test_ok_gestures();
    test_up_down_gestures();
    test_ladder_noise_guards();
    test_stuck_key();
    test_ptt_flow();
    test_banner();
    puts("test_bm_input: 6 tests PASS");
    return 0;
}
