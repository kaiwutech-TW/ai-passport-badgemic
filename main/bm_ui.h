// main/bm_ui.h - Badge Mic screens (LVGL). Every function takes the LVGL lock itself,
// so callers must NOT hold it.
//
// Layers, bottom to top: badge page image (or the default "pair me" screen),
// status pills (BLE, battery, upload progress), pairing screen, full-screen banner
// (recording / sent / not connected).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bm_ptt.h"
#include "esp_err.h"

typedef enum {
    BM_UI_LINK_IDLE = 0,   // advertising, nobody connected
    BM_UI_LINK_PAIRING,    // connected, not yet usable
    BM_UI_LINK_READY,      // encrypted and subscribed
} bm_ui_link_t;

esp_err_t bm_ui_init(const char *device_name);
// Shows an RGB565-LE 240x320 image from memory (flash mmap). NULL shows the default
// screen. When this returns LVGL no longer references the previous image data.
void bm_ui_show_page(const uint8_t *rgb565);
void bm_ui_set_battery(int pct);              // -1 hides the indicator
void bm_ui_set_link(bm_ui_link_t state);
void bm_ui_show_passkey(uint32_t passkey);    // full-screen pairing code
void bm_ui_hide_passkey(void);
void bm_ui_show_banner(bm_banner_kind_t kind);  // BM_BANNER_NONE returns to the badge
void bm_ui_set_level(uint8_t level_0_100);
void bm_ui_set_status(const char *text);      // small bottom pill; NULL hides
// Panel + backlight. panel_on=false blanks the panel (DISPOFF) and turns the backlight
// off; wake order is panel first, backlight second.
void bm_ui_set_display(bool panel_on, uint8_t backlight_pct);
