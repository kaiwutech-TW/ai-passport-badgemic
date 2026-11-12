// main/main.c - Badge Mic entry point: bring up the board, start the app, feed it keys.
//
// Deep-sleep key wake: the chip reboots while the waking key is still held, the button
// driver then sees a press that is already down and would grade it a long press, firing
// an unintended action. key_guard.h swallows key actions until that press is released.
#include "bm_app.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "key_guard.h"
#include "nvs_flash.h"

static const char *TAG = "main";

#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;
static volatile bool s_input_ready;

// Runs on the shared esp_timer task: enqueue only.
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    // Consume the guard before the readiness check: a release can arrive after the
    // post-init voltage sample but before input is ready, and dropping it would leave
    // the guard armed to swallow the next real press.
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) ESP_LOGI(TAG, "wake key released, keys live");
        return;
    }
    if (!s_input_ready) return;
    bm_app_post_key(btn, ev);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Badge Mic starting");
    ESP_LOGI(TAG, "heap boot        free=%u largest=%u", (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));

    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "key wake: ignoring key actions until that key is released");
    }

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    if (nvs != ESP_OK) ESP_LOGE(TAG, "NVS unavailable: %s", esp_err_to_name(nvs));

    bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display / LVGL init failed (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)", BSP_LCD_MOSI,
                 BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }

    if (bm_app_start() != ESP_OK) {
        ESP_LOGE(TAG, "application start failed");
        return;
    }

    esp_err_t button_err = bsp_button_init(on_key, NULL);
    if (button_err != ESP_OK) {
        ESP_LOGE(TAG, "button init failed: %s", esp_err_to_name(button_err));
    } else if (s_key_guard.armed) {
        // If the wake key was released before the driver started there will be no
        // release event: sync with the real voltage.
        key_guard_sync_released(&s_key_guard, bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN);
    }
    s_input_ready = true;
    ESP_LOGI(TAG, "ready");
}
