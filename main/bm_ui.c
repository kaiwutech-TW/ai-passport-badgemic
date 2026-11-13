// main/bm_ui.c - see bm_ui.h. Own design: full-screen badge image with small status
// pills, a pairing screen, and a full-screen recording / result banner.
#include "bm_ui.h"

#include <stdio.h>
#include <string.h>

#include "bm_pages.h"
#include "bsp_display.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "bm_ui";

#define LOCK_MS 300

static struct {
    lv_obj_t *scr;
    lv_obj_t *img;
    lv_obj_t *empty;
    lv_obj_t *ble;
    lv_obj_t *battery;
    lv_obj_t *status;
    lv_obj_t *pair;
    lv_obj_t *pair_code;
    lv_obj_t *banner;
    lv_obj_t *ban_title;
    lv_obj_t *ban_sub;
    lv_obj_t *ban_icon;
    lv_obj_t *ban_mic;
    lv_obj_t *ban_ring;
    lv_obj_t *ban_bar;
    lv_image_dsc_t dsc[2];   // alternate so the source pointer always changes
    int dsc_idx;
    bool page_shown;
    bool panel_on;
    int64_t sleep_in_us;      // last Sleep In; ST7789 needs 120 ms before Sleep Out
} s_ui;

static lv_obj_t *plain(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, text);
    return l;
}

// A small translucent pill so the text stays readable over any badge image.
static lv_obj_t *pill_label(lv_obj_t *parent, const lv_font_t *font)
{
    lv_obj_t *l = label(parent, font, 0xFFFFFF, "");
    lv_obj_set_style_bg_color(l, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_60, 0);
    lv_obj_set_style_radius(l, 10, 0);
    lv_obj_set_style_pad_hor(l, 7, 0);
    lv_obj_set_style_pad_ver(l, 3, 0);
    return l;
}

static void ring_anim_cb(void *var, int32_t v)
{
    lv_obj_t *ring = var;
    int size = 110 + v;
    lv_obj_set_size(ring, size, size);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, 150 - size / 2);
    lv_obj_set_style_border_opa(ring, (lv_opa_t)(200 - v * 2), 0);
}

static void ring_anim(bool on)
{
    lv_anim_delete(s_ui.ban_ring, ring_anim_cb);
    if (!on) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_ui.ban_ring);
    lv_anim_set_exec_cb(&a, ring_anim_cb);
    lv_anim_set_values(&a, 0, 80);
    lv_anim_set_duration(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

// Mic glyph from plain shapes (LVGL has no microphone symbol).
static void build_mic(lv_obj_t *parent)
{
    s_ui.ban_ring = plain(parent, 110, 110);
    lv_obj_set_style_radius(s_ui.ban_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ui.ban_ring, 3, 0);
    lv_obj_set_style_border_color(s_ui.ban_ring, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_ui.ban_ring, LV_ALIGN_TOP_MID, 0, 95);

    s_ui.ban_mic = plain(parent, 60, 110);
    lv_obj_align(s_ui.ban_mic, LV_ALIGN_TOP_MID, 0, 95);
    lv_obj_t *cap = plain(s_ui.ban_mic, 30, 52);
    lv_obj_set_style_bg_color(cap, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cap, 15, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *u = plain(s_ui.ban_mic, 52, 38);
    lv_obj_set_style_radius(u, 26, 0);
    lv_obj_set_style_border_width(u, 4, 0);
    lv_obj_set_style_border_color(u, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_side(u, (lv_border_side_t)(LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_LEFT |
                                                        LV_BORDER_SIDE_RIGHT), 0);
    lv_obj_align(u, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_t *stem = plain(s_ui.ban_mic, 4, 16);
    lv_obj_set_style_bg_color(stem, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, 0);
    lv_obj_align(stem, LV_ALIGN_TOP_MID, 0, 66);
    lv_obj_t *base = plain(s_ui.ban_mic, 30, 4);
    lv_obj_set_style_bg_color(base, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
    lv_obj_align(base, LV_ALIGN_TOP_MID, 0, 82);
}

esp_err_t bm_ui_init(const char *device_name)
{
    if (!bsp_lvgl_lock(1000)) return ESP_ERR_TIMEOUT;
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.panel_on = true;

    s_ui.scr = lv_obj_create(NULL);
    lv_obj_set_scrollable(s_ui.scr, false);
    lv_obj_set_style_bg_color(s_ui.scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_ui.scr, LV_OPA_COVER, 0);

    // Default screen (no page uploaded yet).
    s_ui.empty = plain(s_ui.scr, BM_PAGE_W, BM_PAGE_H);
    lv_obj_t *icon = label(s_ui.empty, &lv_font_montserrat_48, 0x4FC3F7, LV_SYMBOL_BLUETOOTH);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_t *name = label(s_ui.empty, &lv_font_montserrat_32, 0xFFFFFF, device_name ? device_name : "Passport");
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_t *hint = label(s_ui.empty, &lv_font_montserrat_20, 0xB0BEC5, "Pair in BadgeMic\non your Mac");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 195);

    s_ui.img = lv_image_create(s_ui.scr);
    lv_obj_set_pos(s_ui.img, 0, 0);
    lv_obj_set_hidden(s_ui.img, true);

    // Status pills sit inside the rounded corners (radius 30).
    s_ui.ble = pill_label(s_ui.scr, &lv_font_montserrat_14);
    lv_obj_align(s_ui.ble, LV_ALIGN_TOP_LEFT, 22, 10);
    s_ui.battery = pill_label(s_ui.scr, &lv_font_montserrat_14);
    lv_obj_align(s_ui.battery, LV_ALIGN_TOP_RIGHT, -22, 10);
    lv_obj_set_hidden(s_ui.battery, true);
    s_ui.status = pill_label(s_ui.scr, &lv_font_montserrat_14);
    lv_obj_align(s_ui.status, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_hidden(s_ui.status, true);

    // Pairing screen.
    s_ui.pair = plain(s_ui.scr, BM_PAGE_W, BM_PAGE_H);
    lv_obj_set_style_bg_color(s_ui.pair, lv_color_hex(0x0D1B2A), 0);
    lv_obj_set_style_bg_opa(s_ui.pair, LV_OPA_COVER, 0);
    lv_obj_t *pt = label(s_ui.pair, &lv_font_montserrat_32, 0xFFFFFF, "Pairing");
    lv_obj_align(pt, LV_ALIGN_TOP_MID, 0, 50);
    s_ui.pair_code = label(s_ui.pair, &lv_font_montserrat_48, 0x4FC3F7, "000000");
    lv_obj_set_style_text_letter_space(s_ui.pair_code, 3, 0);
    lv_obj_align(s_ui.pair_code, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_t *ph = label(s_ui.pair, &lv_font_montserrat_20, 0xB0BEC5, "Enter this code\non your Mac");
    lv_obj_align(ph, LV_ALIGN_TOP_MID, 0, 210);
    lv_obj_set_hidden(s_ui.pair, true);

    // Full-screen banner: recording / sent / not connected.
    s_ui.banner = plain(s_ui.scr, BM_PAGE_W, BM_PAGE_H);
    lv_obj_set_style_bg_opa(s_ui.banner, LV_OPA_COVER, 0);
    build_mic(s_ui.banner);
    s_ui.ban_icon = label(s_ui.banner, &lv_font_montserrat_48, 0xFFFFFF, LV_SYMBOL_OK);
    lv_obj_align(s_ui.ban_icon, LV_ALIGN_TOP_MID, 0, 110);
    s_ui.ban_title = label(s_ui.banner, &lv_font_montserrat_32, 0xFFFFFF, "");
    lv_obj_align(s_ui.ban_title, LV_ALIGN_TOP_MID, 0, 225);
    s_ui.ban_sub = label(s_ui.banner, &lv_font_montserrat_14, 0xE0E0E0, "");
    lv_obj_set_width(s_ui.ban_sub, 200);
    lv_obj_align(s_ui.ban_sub, LV_ALIGN_TOP_MID, 0, 268);
    s_ui.ban_bar = lv_bar_create(s_ui.banner);
    lv_obj_set_size(s_ui.ban_bar, 160, 10);
    lv_obj_align(s_ui.ban_bar, LV_ALIGN_TOP_MID, 0, 290);
    lv_bar_set_range(s_ui.ban_bar, 0, 100);
    lv_obj_set_style_bg_color(s_ui.ban_bar, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.ban_bar, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.ban_bar, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
    lv_obj_set_hidden(s_ui.banner, true);

    lv_screen_load(s_ui.scr);
    bsp_lvgl_unlock();
    bm_ui_set_link(BM_UI_LINK_IDLE);
    return ESP_OK;
}

void bm_ui_show_page(const uint8_t *rgb565)
{
    if (!bsp_lvgl_lock(LOCK_MS)) {
        ESP_LOGW(TAG, "show_page: LVGL lock timeout");
        return;
    }
    if (rgb565 == NULL) {
        s_ui.page_shown = false;
        lv_obj_set_hidden(s_ui.img, true);
        lv_obj_set_hidden(s_ui.empty, false);
        lv_image_set_src(s_ui.img, NULL);
    } else {
        // The wire format is RGB565 little-endian = LVGL's native RGB565 in memory, so
        // the descriptor points straight at the flash mapping; byte swapping for the
        // panel happens in the port's flush (swap_bytes), after LVGL has drawn.
        s_ui.dsc_idx ^= 1;
        lv_image_dsc_t *d = &s_ui.dsc[s_ui.dsc_idx];
        memset(d, 0, sizeof(*d));
        d->header.magic = LV_IMAGE_HEADER_MAGIC;
        d->header.cf = LV_COLOR_FORMAT_RGB565;
        d->header.w = BM_PAGE_W;
        d->header.h = BM_PAGE_H;
        d->header.stride = BM_PAGE_W * 2;
        d->data_size = BM_PAGE_BYTES;
        d->data = rgb565;
        lv_image_set_src(s_ui.img, d);
        lv_obj_invalidate(s_ui.img);
        lv_obj_set_hidden(s_ui.img, false);
        lv_obj_set_hidden(s_ui.empty, true);
        s_ui.page_shown = true;
    }
    bsp_lvgl_unlock();
}

void bm_ui_set_battery(int pct)
{
    if (!bsp_lvgl_lock(LOCK_MS)) return;
    if (pct < 0 || pct > 100) {
        lv_obj_set_hidden(s_ui.battery, true);
    } else {
        const char *sym = pct > 75 ? LV_SYMBOL_BATTERY_FULL : pct > 50 ? LV_SYMBOL_BATTERY_3
                        : pct > 25 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1
                        : LV_SYMBOL_BATTERY_EMPTY;
        lv_label_set_text_fmt(s_ui.battery, "%s %d%%", sym, pct);
        lv_obj_set_style_text_color(s_ui.battery, lv_color_hex(pct < 15 ? 0xFF5252 : 0xFFFFFF), 0);
        lv_obj_set_hidden(s_ui.battery, false);
    }
    bsp_lvgl_unlock();
}

void bm_ui_set_link(bm_ui_link_t state)
{
    if (!bsp_lvgl_lock(LOCK_MS)) return;
    uint32_t color = state == BM_UI_LINK_READY ? 0x4FC3F7 : state == BM_UI_LINK_PAIRING ? 0xFFC107 : 0x78909C;
    lv_label_set_text(s_ui.ble, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(s_ui.ble, lv_color_hex(color), 0);
    bsp_lvgl_unlock();
}

void bm_ui_show_passkey(uint32_t passkey)
{
    if (!bsp_lvgl_lock(LOCK_MS)) return;
    lv_label_set_text_fmt(s_ui.pair_code, "%06u", (unsigned)passkey);
    lv_obj_set_hidden(s_ui.pair, false);
    bsp_lvgl_unlock();
}

void bm_ui_hide_passkey(void)
{
    if (!bsp_lvgl_lock(LOCK_MS)) return;
    lv_obj_set_hidden(s_ui.pair, true);
    bsp_lvgl_unlock();
}

static bool s_meter_red;

static void meter_color(bool red)
{
    s_meter_red = red;
    lv_obj_set_style_bg_color(s_ui.ban_bar, lv_color_hex(red ? 0xFF3B30 : 0xFFFFFF), LV_PART_INDICATOR);
}

void bm_ui_show_banner(bm_banner_kind_t kind)
{
    if (!bsp_lvgl_lock(LOCK_MS)) return;
    bool rec = kind == BM_BANNER_CAPTURING || kind == BM_BANNER_LIVE;
    if (kind == BM_BANNER_NONE) {
        ring_anim(false);
        lv_obj_set_hidden(s_ui.banner, true);
        bsp_lvgl_unlock();
        return;
    }
    uint32_t bg = kind == BM_BANNER_CAPTURING ? 0x7F1D1D : kind == BM_BANNER_LIVE ? 0x14532D
                : kind == BM_BANNER_SENT ? 0x14532D : 0x78350F;
    lv_obj_set_style_bg_color(s_ui.banner, lv_color_hex(bg), 0);
    if (rec) {
        lv_obj_set_hidden(s_ui.ban_mic, false);
        lv_obj_set_hidden(s_ui.ban_ring, false);
        lv_obj_set_hidden(s_ui.ban_bar, false);
        lv_obj_set_hidden(s_ui.ban_icon, true);
        lv_label_set_text(s_ui.ban_title, kind == BM_BANNER_LIVE ? "LIVE" : "CAPTURING");
        lv_label_set_text(s_ui.ban_sub, kind == BM_BANNER_LIVE ? "Speak now" : "Waiting for your Mac");
        lv_bar_set_value(s_ui.ban_bar, 0, LV_ANIM_OFF);
        if (kind == BM_BANNER_CAPTURING && s_meter_red) meter_color(false);   // new turn starts white
    } else {
        lv_obj_set_hidden(s_ui.ban_mic, true);
        lv_obj_set_hidden(s_ui.ban_ring, true);
        lv_obj_set_hidden(s_ui.ban_bar, true);
        lv_obj_set_hidden(s_ui.ban_icon, false);
        lv_label_set_text(s_ui.ban_icon, kind == BM_BANNER_SENT ? LV_SYMBOL_OK : LV_SYMBOL_WARNING);
        lv_label_set_text(s_ui.ban_title, kind == BM_BANNER_SENT ? "SENT" : "NOT CONNECTED");
        lv_label_set_text(s_ui.ban_sub, kind == BM_BANNER_SENT ? "" : "Open BadgeMic on your Mac");
    }
    lv_obj_set_style_text_font(s_ui.ban_title, kind == BM_BANNER_NOT_CONNECTED ? &lv_font_montserrat_20
                                                                               : &lv_font_montserrat_32, 0);
    lv_obj_set_hidden(s_ui.banner, false);
    ring_anim(rec);
    bsp_lvgl_unlock();
}

void bm_ui_set_level(uint8_t level, bool clip)
{
    if (!bsp_lvgl_lock(50)) return;
    if (!lv_obj_is_hidden(s_ui.banner)) {
        if (clip != s_meter_red) meter_color(clip);   // restyle only on change
        lv_bar_set_value(s_ui.ban_bar, level, LV_ANIM_ON);
    }
    bsp_lvgl_unlock();
}

void bm_ui_set_status(const char *text)
{
    if (!bsp_lvgl_lock(LOCK_MS)) return;
    if (text == NULL) {
        lv_obj_set_hidden(s_ui.status, true);
    } else {
        lv_label_set_text(s_ui.status, text);
        lv_obj_set_hidden(s_ui.status, false);
    }
    bsp_lvgl_unlock();
}

void bm_ui_set_display(bool panel_on, uint8_t backlight_pct)
{
    esp_lcd_panel_handle_t panel = bsp_display_panel();
    if (!panel_on) {
        bsp_display_backlight(0);
        if (s_ui.panel_on && panel) {
            esp_lcd_panel_disp_on_off(panel, false);
            // Sleep In stops the panel's DC/DC, oscillator and scanning; frame memory and
            // the SPI interface keep working, so LVGL may keep flushing. Blocks ~100 ms.
            if (esp_lcd_panel_disp_sleep(panel, true) != ESP_OK) ESP_LOGW(TAG, "LCD sleep in failed");
            s_ui.sleep_in_us = esp_timer_get_time();
        }
        s_ui.panel_on = false;
        return;
    }
    if (!s_ui.panel_on && panel) {                                         // panel first
        int64_t since_ms = (esp_timer_get_time() - s_ui.sleep_in_us) / 1000;
        if (since_ms < 120) vTaskDelay(pdMS_TO_TICKS(120 - since_ms) + 1);
        if (esp_lcd_panel_disp_sleep(panel, false) != ESP_OK) ESP_LOGW(TAG, "LCD sleep out failed");
        esp_lcd_panel_disp_on_off(panel, true);
    }
    s_ui.panel_on = true;
    bsp_display_backlight(backlight_pct);                                  // backlight second
}
