// main/bm_power.h - display settings and idle-stage policy (pure C).
//
// ACTIVE -> DIM after dim_s idle, -> OFF after off_s idle (BLE link stays up),
// -> deep sleep after sleep_min idle (never before off_s). always_on pins ACTIVE
// and leaves by itself below 15 % battery. Contract: badge-mic-protocol.md.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BM_LOW_BATTERY_PCT 15

typedef struct {
    bool always_on;
    uint16_t dim_s;           // 5..600
    uint16_t off_s;           // dim_s..3600
    uint16_t sleep_min;       // 0 (never) or 5..240
    uint8_t brightness;       // 10..100
    uint8_t dim_brightness;   // 5..brightness
} bm_display_cfg_t;

enum {
    BM_PATCH_MODE = 1u << 0,
    BM_PATCH_DIM_S = 1u << 1,
    BM_PATCH_OFF_S = 1u << 2,
    BM_PATCH_SLEEP_MIN = 1u << 3,
    BM_PATCH_BRIGHTNESS = 1u << 4,
    BM_PATCH_DIM_BRIGHTNESS = 1u << 5,
};

// Optional fields of set_display; only fields whose mask bit is set are used.
typedef struct {
    uint32_t mask;
    bool always_on;
    int64_t dim_s, off_s, sleep_min, brightness, dim_brightness;
} bm_display_patch_t;

void bm_display_cfg_default(bm_display_cfg_t *cfg);
// Clamps every field into its documented range (order matters: off_s >= dim_s,
// dim_brightness <= brightness).
void bm_display_cfg_clamp(bm_display_cfg_t *cfg);
// Merges a patch (omitted fields keep their value), then clamps.
void bm_display_cfg_apply(bm_display_cfg_t *cfg, const bm_display_patch_t *patch);
bool bm_display_cfg_equal(const bm_display_cfg_t *a, const bm_display_cfg_t *b);

typedef enum { BM_STAGE_ACTIVE = 0, BM_STAGE_DIM, BM_STAGE_OFF } bm_stage_t;

typedef struct {
    bm_display_cfg_t cfg;
    bm_stage_t stage;
    uint32_t last_activity_ms;
} bm_power_t;

typedef struct {
    bool stage_changed;
    bool deep_sleep_due;
    bool left_always_on;   // battery forced always_on -> auto; persist and report
} bm_power_tick_t;

void bm_power_init(bm_power_t *p, const bm_display_cfg_t *cfg, uint32_t now_ms);
// Records user-visible activity. Returns the stage before the call.
bm_stage_t bm_power_activity(bm_power_t *p, uint32_t now_ms);
// Replaces the config (counts as activity). A request for always_on with battery
// below 15 % is refused (mode becomes auto); returns true when that happened.
bool bm_power_set_cfg(bm_power_t *p, const bm_display_cfg_t *cfg, int battery_pct, uint32_t now_ms);
// busy (PTT turn, upload) counts as activity. battery_pct -1 = unknown.
bm_power_tick_t bm_power_tick(bm_power_t *p, uint32_t now_ms, int battery_pct, bool busy);
// Backlight percent for the current stage.
uint8_t bm_power_backlight(const bm_power_t *p);
// The first UP/DOWN press while the screen is OFF only wakes (not reported).
bool bm_power_press_only_wakes(bm_stage_t stage_before_press);
