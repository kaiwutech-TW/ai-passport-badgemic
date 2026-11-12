// main/bm_app.h - Badge Mic application: wires link, storage, audio, power and UI.
#pragma once

#include "bsp_button.h"
#include "esp_err.h"

// Initializes everything except the button driver. Display and LVGL must be up
// (bsp_display_init + bsp_lvgl_init) and nvs_flash_init() done.
esp_err_t bm_app_start(void);

// Button callback sink (esp_timer task): timestamps and enqueues, never blocks.
void bm_app_post_key(bsp_btn_t btn, bsp_btn_ev_t ev);
