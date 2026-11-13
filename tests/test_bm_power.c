// Host tests: display settings clamping and idle-stage policy.
#include <assert.h>
#include <stdio.h>

#include "bm_power.h"

#define S(x) ((uint32_t)(x) * 1000u)

static void test_defaults_and_clamp(void)
{
    bm_display_cfg_t c;
    bm_display_cfg_default(&c);
    assert(!c.always_on && c.dim_s == 30 && c.off_s == 120 && c.sleep_min == 30 &&
           c.brightness == 80 && c.dim_brightness == 20);

    bm_display_patch_t p = {.mask = BM_PATCH_DIM_S | BM_PATCH_OFF_S | BM_PATCH_SLEEP_MIN |
                                    BM_PATCH_BRIGHTNESS | BM_PATCH_DIM_BRIGHTNESS,
                            .dim_s = 1, .off_s = 99999, .sleep_min = 1,
                            .brightness = 5, .dim_brightness = 0};
    bm_display_cfg_apply(&c, &p);
    assert(c.dim_s == 5 && c.off_s == 3600 && c.sleep_min == 5 && c.brightness == 10 &&
           c.dim_brightness == 5);

    p.dim_s = 99999; p.off_s = 10; p.sleep_min = 9999; p.brightness = 100; p.dim_brightness = 100;
    bm_display_cfg_apply(&c, &p);
    assert(c.dim_s == 600 && c.off_s == 600 /* >= dim_s */ && c.sleep_min == 240 &&
           c.brightness == 100 && c.dim_brightness == 100);

    p.mask = BM_PATCH_SLEEP_MIN; p.sleep_min = 0;   // 0 = never
    bm_display_cfg_apply(&c, &p);
    assert(c.sleep_min == 0);
    p.sleep_min = -4;
    bm_display_cfg_apply(&c, &p);
    assert(c.sleep_min == 0);

    // lowering brightness drags dim_brightness down with it
    bm_display_patch_t q = {.mask = BM_PATCH_BRIGHTNESS, .brightness = 30};
    bm_display_cfg_apply(&c, &q);
    assert(c.brightness == 30 && c.dim_brightness == 30);

    // lowering only dim_s... off_s stays >= dim_s
    bm_display_cfg_default(&c);
    bm_display_patch_t r = {.mask = BM_PATCH_DIM_S, .dim_s = 300};
    bm_display_cfg_apply(&c, &r);
    assert(c.dim_s == 300 && c.off_s == 300);
}

static void test_stage_transitions(void)
{
    bm_display_cfg_t cfg;
    bm_display_cfg_default(&cfg);
    bm_power_t p;
    bm_power_init(&p, &cfg, 1000);
    bm_power_tick_t t = bm_power_tick(&p, 1000 + S(29), 80, false);
    assert(p.stage == BM_STAGE_ACTIVE && !t.stage_changed && bm_power_backlight(&p) == 80);
    t = bm_power_tick(&p, 1000 + S(30), 80, false);
    assert(p.stage == BM_STAGE_DIM && t.stage_changed && bm_power_backlight(&p) == 20);
    t = bm_power_tick(&p, 1000 + S(119), 80, false);
    assert(p.stage == BM_STAGE_DIM && !t.stage_changed);
    t = bm_power_tick(&p, 1000 + S(120), 80, false);
    assert(p.stage == BM_STAGE_OFF && t.stage_changed && bm_power_backlight(&p) == 0);
    assert(!t.deep_sleep_due);
    t = bm_power_tick(&p, 1000 + S(30 * 60 - 1), 80, false);
    assert(!t.deep_sleep_due);
    t = bm_power_tick(&p, 1000 + S(30 * 60), 80, false);
    assert(t.deep_sleep_due);

    // activity wakes; previous stage is reported for "first press only wakes"
    bm_stage_t before = bm_power_activity(&p, 1000 + S(31 * 60));
    assert(before == BM_STAGE_OFF && bm_power_press_only_wakes(before));
    assert(p.stage == BM_STAGE_ACTIVE && bm_power_backlight(&p) == 80);
    before = bm_power_activity(&p, 1000 + S(31 * 60) + 10);
    assert(before == BM_STAGE_ACTIVE && !bm_power_press_only_wakes(before));
    bm_power_tick(&p, 1000 + S(31 * 60) + 10 + S(30), 80, false);
    assert(p.stage == BM_STAGE_DIM && !bm_power_press_only_wakes(p.stage));   // dim: press is reported
}

static void test_busy_and_custom_values(void)
{
    bm_display_cfg_t cfg = {.always_on = false, .dim_s = 5, .off_s = 10, .sleep_min = 5,
                            .brightness = 60, .dim_brightness = 10};
    bm_power_t p;
    bm_power_init(&p, &cfg, 0);
    bm_power_tick(&p, S(6), 50, true);   // busy keeps it awake
    assert(p.stage == BM_STAGE_ACTIVE);
    bm_power_tick(&p, S(6) + S(5), 50, false);
    assert(p.stage == BM_STAGE_DIM && bm_power_backlight(&p) == 10);
    bm_power_tick_t t = bm_power_tick(&p, S(6) + S(10), 50, false);
    assert(p.stage == BM_STAGE_OFF && !t.deep_sleep_due);
    t = bm_power_tick(&p, S(6) + S(300), 50, false);
    assert(t.deep_sleep_due);

    // sleep_min = 0 never sleeps
    cfg.sleep_min = 0;
    bm_power_init(&p, &cfg, 0);
    t = bm_power_tick(&p, S(100000), 50, false);
    assert(p.stage == BM_STAGE_OFF && !t.deep_sleep_due);

    // sleep earlier than screen-off is deferred until after off_s
    bm_display_cfg_t late = {.always_on = false, .dim_s = 30, .off_s = 3600, .sleep_min = 5,
                             .brightness = 80, .dim_brightness = 20};
    bm_power_init(&p, &late, 0);
    t = bm_power_tick(&p, S(600), 50, false);
    assert(!t.deep_sleep_due);
    t = bm_power_tick(&p, S(3600), 50, false);
    assert(t.deep_sleep_due);
}

static void test_always_on(void)
{
    bm_display_cfg_t cfg;
    bm_display_cfg_default(&cfg);
    cfg.always_on = true;
    bm_power_t p;
    bm_power_init(&p, &cfg, 0);
    bm_power_tick_t t = bm_power_tick(&p, S(100000), 80, false);
    assert(p.stage == BM_STAGE_ACTIVE && !t.deep_sleep_due && !t.left_always_on);
    t = bm_power_tick(&p, S(100000), -1, false);   // unknown battery: stay
    assert(p.cfg.always_on && !t.left_always_on);
    t = bm_power_tick(&p, S(100001), 15, false);   // 15 % is still fine
    assert(p.cfg.always_on && !t.left_always_on);
    t = bm_power_tick(&p, S(100002), 14, false);
    assert(t.left_always_on && !p.cfg.always_on && p.stage == BM_STAGE_ACTIVE);
    t = bm_power_tick(&p, S(100002) + S(30), 14, false);   // auto timers start from the exit
    assert(p.stage == BM_STAGE_DIM);

    // asking for always_on on a low battery is refused
    bm_power_init(&p, &cfg, 0);
    p.cfg.always_on = false;
    bool refused = bm_power_set_cfg(&p, &cfg, 10, S(5));
    assert(refused && !p.cfg.always_on);
    refused = bm_power_set_cfg(&p, &cfg, 80, S(6));
    assert(!refused && p.cfg.always_on);
}

static void test_off_key(void)
{
    bm_display_cfg_t cfg;
    bm_display_cfg_default(&cfg);
    assert(cfg.off_key == BM_OFF_KEY_NONE);
    assert(!bm_power_is_off_key(&cfg, true, true));
    cfg.off_key = BM_OFF_KEY_UP_LONG;
    assert(bm_power_is_off_key(&cfg, true, true));
    assert(!bm_power_is_off_key(&cfg, true, false));    // click is still a normal key
    assert(!bm_power_is_off_key(&cfg, false, true));
    cfg.off_key = BM_OFF_KEY_DOWN_LONG;
    assert(bm_power_is_off_key(&cfg, false, true) && !bm_power_is_off_key(&cfg, true, true));
    cfg.off_key = 9;
    bm_display_cfg_clamp(&cfg);
    assert(cfg.off_key == BM_OFF_KEY_NONE);

    // always_on: forced off stays off until activity, then always_on again
    bm_display_cfg_default(&cfg);
    cfg.always_on = true;
    cfg.sleep_min = 0;
    bm_power_t p;
    bm_power_init(&p, &cfg, 0);
    bm_power_force_off(&p);
    assert(p.stage == BM_STAGE_OFF && bm_power_backlight(&p) == 0);
    bm_power_tick_t t = bm_power_tick(&p, S(5), 80, false);
    assert(p.stage == BM_STAGE_OFF && !t.stage_changed);
    t = bm_power_tick(&p, S(3600), 80, false);
    assert(p.stage == BM_STAGE_OFF && !t.deep_sleep_due);
    assert(bm_power_activity(&p, S(3601)) == BM_STAGE_OFF);
    assert(bm_power_press_only_wakes(BM_STAGE_OFF));
    t = bm_power_tick(&p, S(3700), 80, false);
    assert(p.stage == BM_STAGE_ACTIVE && !p.manual_off);

    // auto mode: forced off before dim_s; deep sleep still counts from the last activity
    bm_display_cfg_default(&cfg);   // dim 30 s, off 120 s, sleep 30 min
    bm_power_init(&p, &cfg, 0);
    bm_power_force_off(&p);
    t = bm_power_tick(&p, S(10), 80, false);
    assert(p.stage == BM_STAGE_OFF && !t.deep_sleep_due);
    t = bm_power_tick(&p, S(30 * 60), 80, false);
    assert(p.stage == BM_STAGE_OFF && t.deep_sleep_due);

    // busy (PTT / upload) wakes it
    bm_power_init(&p, &cfg, 0);
    bm_power_force_off(&p);
    t = bm_power_tick(&p, S(1), 80, true);
    assert(p.stage == BM_STAGE_ACTIVE && t.stage_changed && !p.manual_off);

    // low battery while forced off in always_on: leaves always_on, stays off
    bm_display_cfg_default(&cfg);
    cfg.always_on = true;
    bm_power_init(&p, &cfg, 0);
    bm_power_force_off(&p);
    t = bm_power_tick(&p, S(1), 10, false);
    assert(t.left_always_on && !p.cfg.always_on && p.stage == BM_STAGE_OFF);
}

static void test_battery_classify(void)
{
    assert(bm_battery_classify(93, 4114) == BM_BATT_OK);
    assert(bm_battery_classify(0, 3400) == BM_BATT_OK);           // a really empty cell
    assert(bm_battery_classify(0, 3699) == BM_BATT_OK);
    assert(bm_battery_classify(0, 3700) == BM_BATT_IMPLAUSIBLE);  // 0 % with a healthy voltage
    assert(bm_battery_classify(0, 4114) == BM_BATT_IMPLAUSIBLE);
    assert(bm_battery_classify(0, 0) == BM_BATT_IMPLAUSIBLE);     // cold-start zero pair
    assert(bm_battery_classify(62, 2499) == BM_BATT_IMPLAUSIBLE);
    assert(bm_battery_classify(62, 2500) == BM_BATT_OK);
    assert(bm_battery_classify(62, -1) == BM_BATT_OK);            // voltage read failed
    assert(bm_battery_classify(-1, 4000) == BM_BATT_UNAVAILABLE);
    assert(bm_battery_classify(101, 4000) == BM_BATT_UNAVAILABLE);
}

int main(void)
{
    test_defaults_and_clamp();
    test_stage_transitions();
    test_busy_and_custom_values();
    test_always_on();
    test_off_key();
    test_battery_classify();
    puts("test_bm_power: 6 tests PASS");
    return 0;
}
