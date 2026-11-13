#include "bm_power.h"

static int64_t clamp64(int64_t v, int64_t lo, int64_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void bm_display_cfg_default(bm_display_cfg_t *cfg)
{
    cfg->always_on = false;
    cfg->dim_s = 30;
    cfg->off_s = 120;
    cfg->sleep_min = 30;
    cfg->brightness = 80;
    cfg->dim_brightness = 20;
    cfg->off_key = BM_OFF_KEY_NONE;
}

void bm_display_cfg_clamp(bm_display_cfg_t *cfg)
{
    cfg->dim_s = (uint16_t)clamp64(cfg->dim_s, 5, 600);
    cfg->off_s = (uint16_t)clamp64(cfg->off_s, cfg->dim_s, 3600);
    if (cfg->sleep_min != 0) cfg->sleep_min = (uint16_t)clamp64(cfg->sleep_min, 5, 240);
    cfg->brightness = (uint8_t)clamp64(cfg->brightness, 10, 100);
    cfg->dim_brightness = (uint8_t)clamp64(cfg->dim_brightness, 5, cfg->brightness);
    if (cfg->off_key > BM_OFF_KEY_DOWN_LONG) cfg->off_key = BM_OFF_KEY_NONE;
}

void bm_display_cfg_apply(bm_display_cfg_t *cfg, const bm_display_patch_t *p)
{
    if (p->mask & BM_PATCH_MODE) cfg->always_on = p->always_on;
    if (p->mask & BM_PATCH_DIM_S) cfg->dim_s = (uint16_t)clamp64(p->dim_s, 5, 600);
    // Clamp off_s against the (already updated) dim_s.
    if (p->mask & BM_PATCH_OFF_S) cfg->off_s = (uint16_t)clamp64(p->off_s, 0, 3600);
    if (p->mask & BM_PATCH_SLEEP_MIN) {
        cfg->sleep_min = p->sleep_min <= 0 ? 0 : (uint16_t)clamp64(p->sleep_min, 5, 240);
    }
    if (p->mask & BM_PATCH_BRIGHTNESS) cfg->brightness = (uint8_t)clamp64(p->brightness, 10, 100);
    if (p->mask & BM_PATCH_DIM_BRIGHTNESS) cfg->dim_brightness = (uint8_t)clamp64(p->dim_brightness, 0, 100);
    if (p->mask & BM_PATCH_OFF_KEY) cfg->off_key = p->off_key;
    bm_display_cfg_clamp(cfg);
}

bool bm_display_cfg_equal(const bm_display_cfg_t *a, const bm_display_cfg_t *b)
{
    return a->always_on == b->always_on && a->dim_s == b->dim_s && a->off_s == b->off_s &&
           a->sleep_min == b->sleep_min && a->brightness == b->brightness &&
           a->dim_brightness == b->dim_brightness && a->off_key == b->off_key;
}

void bm_power_init(bm_power_t *p, const bm_display_cfg_t *cfg, uint32_t now_ms)
{
    p->cfg = *cfg;
    bm_display_cfg_clamp(&p->cfg);
    p->stage = BM_STAGE_ACTIVE;
    p->last_activity_ms = now_ms;
    p->manual_off = false;
}

bm_stage_t bm_power_activity(bm_power_t *p, uint32_t now_ms)
{
    bm_stage_t before = p->stage;
    p->stage = BM_STAGE_ACTIVE;
    p->last_activity_ms = now_ms;
    p->manual_off = false;
    return before;
}

bool bm_power_set_cfg(bm_power_t *p, const bm_display_cfg_t *cfg, int battery_pct, uint32_t now_ms)
{
    bool refused = false;
    p->cfg = *cfg;
    bm_display_cfg_clamp(&p->cfg);
    if (p->cfg.always_on && battery_pct >= 0 && battery_pct < BM_LOW_BATTERY_PCT) {
        p->cfg.always_on = false;
        refused = true;
    }
    (void)bm_power_activity(p, now_ms);
    return refused;
}

bm_power_tick_t bm_power_tick(bm_power_t *p, uint32_t now_ms, int battery_pct, bool busy)
{
    bm_power_tick_t out = {0};
    if (busy) {
        out.stage_changed = bm_power_activity(p, now_ms) != BM_STAGE_ACTIVE;
        return out;
    }
    if (p->manual_off) {
        out.stage_changed = p->stage != BM_STAGE_OFF;
        p->stage = BM_STAGE_OFF;
        if (p->cfg.always_on && battery_pct >= 0 && battery_pct < BM_LOW_BATTERY_PCT) {
            p->cfg.always_on = false;
            out.left_always_on = true;
        }
        if (p->cfg.sleep_min != 0) {
            uint32_t sleep_ms = (uint32_t)p->cfg.sleep_min * 60000u;
            uint32_t off_ms = (uint32_t)p->cfg.off_s * 1000u;
            if (sleep_ms < off_ms) sleep_ms = off_ms;
            out.deep_sleep_due = now_ms - p->last_activity_ms >= sleep_ms;
        }
        return out;
    }
    if (p->cfg.always_on) {
        if (battery_pct >= 0 && battery_pct < BM_LOW_BATTERY_PCT) {
            p->cfg.always_on = false;
            out.left_always_on = true;
            p->last_activity_ms = now_ms;  // start the auto timers from now
        } else {
            out.stage_changed = p->stage != BM_STAGE_ACTIVE;
            p->stage = BM_STAGE_ACTIVE;
            return out;
        }
    }
    uint32_t idle = now_ms - p->last_activity_ms;
    bm_stage_t want = BM_STAGE_ACTIVE;
    if (idle >= (uint32_t)p->cfg.off_s * 1000u) want = BM_STAGE_OFF;
    else if (idle >= (uint32_t)p->cfg.dim_s * 1000u) want = BM_STAGE_DIM;
    out.stage_changed = want != p->stage;
    p->stage = want;
    if (p->cfg.sleep_min != 0) {
        uint32_t sleep_ms = (uint32_t)p->cfg.sleep_min * 60000u;
        uint32_t off_ms = (uint32_t)p->cfg.off_s * 1000u;
        if (sleep_ms < off_ms) sleep_ms = off_ms;
        out.deep_sleep_due = idle >= sleep_ms;
    }
    return out;
}

void bm_power_force_off(bm_power_t *p)
{
    p->stage = BM_STAGE_OFF;
    p->manual_off = true;
}

bool bm_power_is_off_key(const bm_display_cfg_t *cfg, bool up, bool long_press)
{
    if (!long_press) return false;
    return (up && cfg->off_key == BM_OFF_KEY_UP_LONG) || (!up && cfg->off_key == BM_OFF_KEY_DOWN_LONG);
}

uint8_t bm_power_backlight(const bm_power_t *p)
{
    switch (p->stage) {
    case BM_STAGE_ACTIVE: return p->cfg.brightness;
    case BM_STAGE_DIM: return p->cfg.dim_brightness;
    default: return 0;
    }
}

bool bm_power_press_only_wakes(bm_stage_t before)
{
    return before == BM_STAGE_OFF;
}
