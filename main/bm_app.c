// main/bm_app.c - see bm_app.h. One task owns all state; other contexts (button
// callbacks, NimBLE host, audio encoder) only enqueue or push into the pre-roll ring.
#include "bm_app.h"

#include <string.h>

#include "bm_audio.h"
#include "bm_cmd.h"
#include "bm_input.h"
#include "bm_link.h"
#include "bm_pages.h"
#include "bm_power.h"
#include "bm_proto.h"
#include "bm_ptt.h"
#include "bm_ring.h"
#include "bm_store.h"
#include "bm_tx.h"
#include "bm_ui.h"
#include "bm_upload.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "bm_app";

#define BM_FW_VERSION "0.1.0"
#define APP_TASK_STACK 6144
#define APP_TASK_PRIO 5
#define QUEUE_DEPTH 48
#define BATTERY_POLL_MS 30000u
#define BATTERY_EVENT_MIN_MS 60000u
#define DEEP_SLEEP_RETRY_MS 5000u

typedef enum { EV_KEY, EV_LINK } app_ev_type_t;

typedef struct {
    app_ev_type_t type;
    bsp_btn_t btn;
    bool down;
    uint32_t ts_ms;
    bm_link_event_t link;
} app_ev_t;

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_ring_mx;
static bm_ring_t s_ring;
static volatile uint8_t s_level;

static bm_input_t s_input;
static bm_ptt_t s_ptt;
static bm_banner_t s_banner;
static bm_power_t s_power;
static bm_upload_t s_upload;
static bm_reassembler_t s_rx;

static bm_map_t s_map;           // mapping of the page on screen
static int s_cur_page = -1;      // page on screen, -1 = default screen
static bool s_mac_hello_ok;      // Mac hello received with a matching proto
static bool s_wake_only[BM_K_COUNT];
static bool s_ok_wake_only;
static bm_tx_t s_tx;
static int s_battery = -1;
static int s_battery_sent = -2;
static uint32_t s_battery_poll_ms;
static uint32_t s_battery_sent_ms;
static uint32_t s_deep_retry_ms;
static uint32_t s_level_ms;
static uint32_t s_progress_frames;
static bool s_first_ptt_logged;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void log_heap(const char *why)
{
    ESP_LOGI(TAG, "heap %-12s free=%u largest=%u min=%u", why, (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
             (unsigned)esp_get_minimum_free_heap_size());
}

static bool usable(void)
{
    return bm_link_ready() && s_mac_hello_ok;
}

// ---- sending ----
static void send_event(const char *json, size_t n)
{
    if (n == 0) return;
    uint8_t frame[BM_HEADER_SIZE + BM_JSON_PAYLOAD_MAX];
    size_t fl = bm_encode(frame, sizeof(frame), BM_FRAME_EVENT, (const uint8_t *)json, n);
    if (fl == 0) return;
    // Events are rare and must not be lost behind a burst of audio: retry briefly.
    for (int i = 0; i < 20; i++) {
        esp_err_t e = bm_link_send(frame, fl);
        if (e != ESP_ERR_NO_MEM) return;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGW(TAG, "event dropped: %.40s", json);
}

static void send_hello(void)
{
    char b[BM_JSON_PAYLOAD_MAX];
    send_event(b, bm_ev_hello(b, sizeof(b), BM_FW_VERSION, bm_link_device_name(), BM_PAGE_COUNT,
                              s_battery, &s_power.cfg));
}

static void send_display(void)
{
    char b[BM_JSON_PAYLOAD_MAX];
    send_event(b, bm_ev_display(b, sizeof(b), &s_power.cfg));
}

static void send_error(const char *cmd, const char *err)
{
    char b[BM_JSON_PAYLOAD_MAX];
    send_event(b, bm_ev_error(b, sizeof(b), cmd, err));
}

static void send_put_done(int page, const char *err)
{
    char b[BM_JSON_PAYLOAD_MAX];
    send_event(b, bm_ev_put_done(b, sizeof(b), page, err == NULL, err));
}

// ---- display / power ----
static void apply_stage(void)
{
    bool on = s_power.stage != BM_STAGE_OFF;
    bm_ui_set_display(on, bm_power_backlight(&s_power));
    esp_pm_config_t pm = {.max_freq_mhz = 160, .min_freq_mhz = on ? 160 : 40, .light_sleep_enable = false};
    esp_pm_configure(&pm);
}

static void note_activity(void)
{
    if (bm_power_activity(&s_power, now_ms()) != BM_STAGE_ACTIVE) apply_stage();
}

static void set_banner(bm_banner_kind_t kind)
{
    bm_banner_show(&s_banner, kind, now_ms());
    bm_ui_show_banner(kind);
}

// ---- pages ----
static void show_page(int page)
{
    if (page < 0) {
        bm_ui_show_page(NULL);
        bm_store_unmap(&s_map);
        s_cur_page = -1;
        return;
    }
    bm_map_t fresh;
    esp_err_t e = bm_store_map_page(page, &fresh);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "map page %d failed: %s", page, esp_err_to_name(e));
        return;
    }
    bm_ui_show_page(fresh.data);       // LVGL no longer uses the old mapping after this
    bm_store_unmap(&s_map);
    s_map = fresh;
    s_cur_page = page;
}

static void cycle_page(void)
{
    int next = bm_pages_next_valid(bm_store_pages(), s_cur_page);
    if (next >= 0 && next != s_cur_page) show_page(next);
}

// ---- audio ----
static void on_audio_frame(const uint8_t *opus, size_t len, uint8_t level, void *ctx)
{
    (void)ctx;
    s_level = level;
    xSemaphoreTake(s_ring_mx, portMAX_DELAY);
    bm_ring_push(&s_ring, opus, len);
    xSemaphoreGive(s_ring_mx);
}

static void ring_clear(void)
{
    xSemaphoreTake(s_ring_mx, portMAX_DELAY);
    bm_ring_clear(&s_ring);
    s_ring.overwritten = 0;
    xSemaphoreGive(s_ring_mx);
}

static void send_audio(const uint8_t *opus, size_t len)
{
    uint8_t payload[1 + BM_OPUS_PACKET_MAX];
    uint8_t frame[BM_HEADER_SIZE + 1 + BM_OPUS_PACKET_MAX];
    size_t pl = bm_tx_pack(&s_tx, opus, len, now_ms(), payload);   // SEQ advances even on failure
    size_t fl = bm_encode(frame, sizeof(frame), BM_FRAME_AUDIO_OPUS, payload, pl);
    if (fl && bm_link_send(frame, fl) == ESP_OK) s_ptt.frames++;
    else s_ptt.dropped++;
}

// Every encoded frame is sent, DTX / silence packets included (the Mac releases the
// hotkey after 2 s without traffic). If the encoder stalls, a TOC-only packet goes out
// at least every BM_TX_KEEPALIVE_MS.
static void drain_ring(void)
{
    uint8_t pkt[BM_OPUS_PACKET_MAX];
    for (;;) {
        xSemaphoreTake(s_ring_mx, portMAX_DELAY);
        size_t n = bm_ring_pop(&s_ring, pkt, sizeof(pkt));
        xSemaphoreGive(s_ring_mx);
        if (n == 0) break;
        send_audio(pkt, n);
    }
    if (s_ptt.state == BM_PTT_TALKING && bm_tx_keepalive_due(&s_tx, now_ms())) send_audio(NULL, 0);
}

static void end_turn(bm_ptt_reason_t reason)
{
    bm_audio_end(true);
    bool can_send = reason != BM_PTT_REASON_LINK;
    if (can_send) drain_ring();
    xSemaphoreTake(s_ring_mx, portMAX_DELAY);
    uint32_t overwritten = s_ring.overwritten;
    xSemaphoreGive(s_ring_mx);
    uint32_t dropped = s_ptt.dropped + overwritten;
    bm_audio_stats_t st;
    bm_audio_get_stats(&st);
    ESP_LOGI(TAG, "turn %u end: frames=%u dropped=%u reason=%d enc_max=%uus stack_free=%u tx_drops=%u",
             (unsigned)s_ptt.id, (unsigned)s_ptt.frames, (unsigned)dropped, (int)reason,
             (unsigned)st.encode_us_max, (unsigned)st.stack_free, (unsigned)bm_link_tx_drop_count());
    if (can_send) {
        char b[BM_JSON_PAYLOAD_MAX];
        const char *r = reason == BM_PTT_REASON_TIMEOUT ? "timeout" : NULL;
        send_event(b, bm_ev_ptt_end(b, sizeof(b), s_ptt.id, s_ptt.frames, dropped, r));
        set_banner(BM_BANNER_SENT);
    } else {
        set_banner(BM_BANNER_NONE);
    }
    ring_clear();
}

static void run_ptt(bm_ptt_out_t out)
{
    switch (out.act) {
    case BM_PTT_ACT_START_CAPTURE:
        ring_clear();
        bm_audio_begin();
        break;
    case BM_PTT_ACT_DISCARD:
        bm_audio_end(false);
        ring_clear();
        break;
    case BM_PTT_ACT_TAP:
        if (!s_ok_wake_only) cycle_page();     // before the (slower) capture shutdown
        bm_audio_end(false);
        ring_clear();
        break;
    case BM_PTT_ACT_NOT_CONNECTED:
        bm_audio_end(false);
        ring_clear();
        set_banner(BM_BANNER_NOT_CONNECTED);
        break;
    case BM_PTT_ACT_START_TURN: {
        if (!s_first_ptt_logged) {
            s_first_ptt_logged = true;
            log_heap("first-ptt");
        }
        xSemaphoreTake(s_ring_mx, portMAX_DELAY);
        s_ring.overwritten = 0;                // pre-roll trimming is not a loss
        xSemaphoreGive(s_ring_mx);
        bm_tx_begin(&s_tx, now_ms());
        char b[BM_JSON_PAYLOAD_MAX];
        send_event(b, bm_ev_ptt_start(b, sizeof(b), s_ptt.id));
        set_banner(BM_BANNER_CAPTURING);
        drain_ring();                          // pre-roll first, then live frames
        break;
    }
    case BM_PTT_ACT_END_TURN:
        end_turn(out.reason);
        break;
    default:
        break;
    }
}

// ---- input ----
static void handle_input(bm_in_ev_t e)
{
    switch (e.kind) {
    case BM_IN_OK_DOWN:
        run_ptt(bm_ptt_ok_down(&s_ptt));
        break;
    case BM_IN_OK_NOISE:
        run_ptt(bm_ptt_ok_noise(&s_ptt));
        break;
    case BM_IN_OK_TAP:
        run_ptt(bm_ptt_ok_tap(&s_ptt));
        break;
    case BM_IN_OK_HOLD:
        run_ptt(bm_ptt_ok_hold(&s_ptt, usable(), now_ms()));
        break;
    case BM_IN_OK_UP:
        run_ptt(bm_ptt_ok_up(&s_ptt));
        break;
    case BM_IN_CLICK:
    case BM_IN_LONG:
        if (s_wake_only[e.key]) {
            s_wake_only[e.key] = false;        // this press only woke the screen
        } else if (usable() && e.key != BM_K_OK) {
            char b[BM_JSON_PAYLOAD_MAX];
            send_event(b, bm_ev_key(b, sizeof(b), e.key == BM_K_UP, e.kind == BM_IN_LONG));
        }
        break;
    default:
        break;
    }
}

static void on_key_edge(bsp_btn_t btn, bool down, uint32_t ts)
{
    bm_key_t key = btn == BSP_BTN_UP ? BM_K_UP : btn == BSP_BTN_DOWN ? BM_K_DOWN : BM_K_OK;
    if (down) {
        bm_stage_t before = bm_power_activity(&s_power, now_ms());
        if (before != BM_STAGE_ACTIVE) apply_stage();
        bool wake_only = bm_power_press_only_wakes(before);
        if (key == BM_K_OK) s_ok_wake_only = wake_only;
        else s_wake_only[key] = wake_only;
        handle_input(bm_input_press(&s_input, key, ts));
    } else {
        note_activity();
        handle_input(bm_input_tick(&s_input, ts));    // report a hold that crossed 300 ms first
        handle_input(bm_input_release(&s_input, key, ts));
    }
}

// ---- commands ----
static void update_upload_progress(void)
{
    char t[32];
    snprintf(t, sizeof(t), "Receiving %u%%", (unsigned)((uint64_t)s_upload.offset * 100u / BM_PAGE_BYTES));
    bm_ui_set_status(t);
}

static void handle_put_data(const uint8_t *payload, size_t len)
{
    if (s_upload.ops == NULL) return;
    int page = s_upload.page;
    const char *err = bm_upload_data(&s_upload, payload, len, now_ms());
    if (err == NULL) {
        note_activity();
        if (++s_progress_frames % 24 == 0) update_upload_progress();
        return;
    }
    if (strcmp(err, "no_upload") == 0) return;
    ESP_LOGW(TAG, "upload aborted: %s", err);
    bm_ui_set_status(NULL);
    send_put_done(page, err);
}

static void handle_control(const uint8_t *payload, size_t len)
{
    bm_cmd_t c;
    if (!bm_cmd_parse(payload, len, &c)) {
        ESP_LOGW(TAG, "malformed CONTROL ignored");
        return;
    }
    if (c.bad_args) {
        send_error(c.name, "bad_args");
        return;
    }
    switch (c.kind) {
    case BM_CMD_HELLO:
        if (c.proto != BM_PROTO_VERSION) {
            s_mac_hello_ok = false;
            send_error("hello", "proto");
        } else {
            s_mac_hello_ok = true;
        }
        break;
    case BM_CMD_PTT_ACK:
        if (bm_ptt_ack(&s_ptt, c.id)) {
            set_banner(BM_BANNER_LIVE);
        }
        break;
    case BM_CMD_SET_DISPLAY: {
        bm_display_cfg_t cfg = s_power.cfg;
        bm_display_cfg_apply(&cfg, &c.patch);
        bm_power_set_cfg(&s_power, &cfg, s_battery, now_ms());
        esp_err_t e = bm_store_save_display(&s_power.cfg);
        if (e != ESP_OK) ESP_LOGW(TAG, "saving display settings failed: %s", esp_err_to_name(e));
        apply_stage();
        send_display();
        break;
    }
    case BM_CMD_PUT_BEGIN: {
        if (bm_ptt_busy(&s_ptt)) {
            send_put_done(c.page, "busy");
            break;
        }
        const char *err = NULL;
        if (c.page < 0 || c.page >= BM_PAGE_COUNT) {
            err = "bad_page";
        } else if (s_upload.ops == NULL) {
            err = "no_storage";
        } else {
            bm_ui_set_status("Preparing...");
            err = bm_upload_begin(&s_upload, c.page, BM_PAGE_COUNT, c.size, BM_PAGE_BYTES, c.crc32,
                                  bm_pages_inactive_slot(bm_store_pages(), c.page), now_ms());
        }
        note_activity();
        if (err) {
            bm_ui_set_status(NULL);
            send_put_done(c.page, err);
        } else {
            s_progress_frames = 0;
            update_upload_progress();
            char b[BM_JSON_PAYLOAD_MAX];
            send_event(b, bm_ev_put_ready(b, sizeof(b), c.page));
        }
        break;
    }
    case BM_CMD_PUT_END: {
        if (s_upload.ops == NULL) {
            send_put_done(c.page, "no_storage");
            break;
        }
        const char *err = bm_upload_end(&s_upload, c.page);
        bm_ui_set_status(NULL);
        note_activity();
        if (err) {
            send_put_done(c.page, err);
            break;
        }
        if (s_cur_page == c.page || s_cur_page < 0) show_page(c.page);
        send_put_done(c.page, NULL);
        break;
    }
    case BM_CMD_CLEAR_PAGE:
        if (c.page < 0 || c.page >= BM_PAGE_COUNT) {
            send_error(c.name, "bad_page");
            break;
        }
        if (bm_upload_active(&s_upload) && s_upload.page == c.page) {
            bm_upload_abort(&s_upload);
            bm_ui_set_status(NULL);
        }
        if (s_upload.ops == NULL || bm_store_clear_page(c.page) != ESP_OK) {
            send_error(c.name, "store");
            break;
        }
        if (s_cur_page == c.page) show_page(bm_pages_first_valid(bm_store_pages()));
        note_activity();
        break;
    default:
        send_error(c.name, "unknown");
        break;
    }
}

static void on_frame(uint8_t type, const uint8_t *payload, size_t len, void *ctx)
{
    (void)ctx;
    if (type == BM_FRAME_CONTROL) handle_control(payload, len);
    else if (type == BM_FRAME_PUT_DATA) handle_put_data(payload, len);
}

// ---- link events ----
static void on_link_event(const bm_link_event_t *ev, void *ctx)
{
    (void)ctx;
    app_ev_t e = {.type = EV_LINK, .link = *ev};
    xQueueSend(s_queue, &e, 0);
}

static void handle_link(const bm_link_event_t *ev)
{
    switch (ev->type) {
    case BM_LINK_EV_CONNECTED:
        s_mac_hello_ok = false;
        bm_reassembler_reset(&s_rx);
        bm_link_flush_rx();
        bm_ui_set_link(BM_UI_LINK_PAIRING);
        note_activity();
        break;
    case BM_LINK_EV_PASSKEY:
        note_activity();
        bm_ui_show_passkey(ev->passkey);
        break;
    case BM_LINK_EV_SECURED:
        bm_ui_hide_passkey();
        break;
    case BM_LINK_EV_SUBSCRIBED:
        if (bm_link_ready()) {
            bm_ui_hide_passkey();
            bm_ui_set_link(BM_UI_LINK_READY);
            s_battery_sent = s_battery;
            s_battery_sent_ms = now_ms();
            send_hello();
            note_activity();
        } else if (bm_link_connected()) {
            bm_ui_set_link(BM_UI_LINK_PAIRING);
        }
        break;
    case BM_LINK_EV_DISCONNECTED:
        s_mac_hello_ok = false;
        bm_ui_hide_passkey();
        bm_ui_set_link(BM_UI_LINK_IDLE);
        bm_reassembler_reset(&s_rx);
        if (bm_upload_active(&s_upload)) {
            bm_upload_abort(&s_upload);
            bm_ui_set_status(NULL);
        }
        break;
    case BM_LINK_EV_RX: {
        uint8_t buf[128];
        size_t n;
        while ((n = bm_link_read(buf, sizeof(buf))) > 0) bm_reassembler_push(&s_rx, buf, n);
        break;
    }
    case BM_LINK_EV_TX_DROP:
        ESP_LOGW(TAG, "link TX back-pressure (total drops %u)", (unsigned)bm_link_tx_drop_count());
        break;
    }
}

// ---- deep sleep ----
static void enter_deep_sleep(void)
{
    // GPIO0 is both the key ADC node and the wake pin: a held key would wake us at once.
    int mv = bsp_button_read_mv();
    if (mv >= 0 && mv < BSP_BTN_MV_RELEASED_MIN) {
        ESP_LOGW(TAG, "key held (%d mV), deferring deep sleep", mv);
        s_deep_retry_ms = now_ms() + DEEP_SLEEP_RETRY_MS;
        return;
    }
    ESP_LOGW(TAG, "idle: entering deep sleep (press any key to wake)");
    bm_link_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));    // let the disconnect reach the Mac
    int level = 0;
    if (bsp_button_prepare_deep_sleep(&level) != ESP_OK) {
        ESP_LOGE(TAG, "key hand-off failed, restarting");
        esp_restart();
    }
    if (level != 1) ESP_LOGW(TAG, "key still low after hand-off, sleep may end at once");
    // The argument is a pin bitmask, not a pin number.
    esp_err_t e = esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
    if (e != ESP_OK) ESP_LOGE(TAG, "GPIO wake arm failed: %s", esp_err_to_name(e));
    bsp_battery_sleep();               // CW2017 shares I2C with the codec: finish it first
    bsp_audio_sleep();
    bsp_audio_prepare_deep_sleep();
    bsp_i2c_prepare_deep_sleep();
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "cannot stop LVGL before sleep, restarting");
        esp_restart();
    }
    bsp_display_prepare_deep_sleep();
    esp_deep_sleep_start();
    ESP_LOGE(TAG, "deep sleep returned, restarting");
    esp_restart();
}

// ---- periodic work ----
static void poll_battery(uint32_t now)
{
    s_battery_poll_ms = now;
    int pct = bsp_battery_soc();
    if (pct > 100) pct = 100;
    if (pct != s_battery) {
        s_battery = pct;
        bm_ui_set_battery(pct);
    }
    if (usable() && s_battery != s_battery_sent && now - s_battery_sent_ms >= BATTERY_EVENT_MIN_MS) {
        s_battery_sent = s_battery;
        s_battery_sent_ms = now;
        char b[BM_JSON_PAYLOAD_MAX];
        send_event(b, bm_ev_battery(b, sizeof(b), s_battery));
    }
}

static void periodic(void)
{
    uint32_t now = now_ms();
    bool link_ok = usable();

    handle_input(bm_input_tick(&s_input, now));
    bm_ptt_out_t out = bm_ptt_tick(&s_ptt, link_ok, now);
    if (out.act != BM_PTT_ACT_NONE) run_ptt(out);
    if (s_ptt.state == BM_PTT_TALKING) drain_ring();

    if (bm_banner_tick(&s_banner, now)) bm_ui_show_banner(BM_BANNER_NONE);
    if ((s_banner.kind == BM_BANNER_CAPTURING || s_banner.kind == BM_BANNER_LIVE) && now - s_level_ms >= 80) {
        s_level_ms = now;
        bm_ui_set_level(s_level);
    }

    if (bm_upload_check_stall(&s_upload, now)) {
        ESP_LOGW(TAG, "upload stalled, aborted");
        bm_ui_set_status(NULL);
        send_put_done(s_upload.page, "timeout");
    }

    bool busy = bm_ptt_busy(&s_ptt) || bm_upload_active(&s_upload);
    if (!busy && now - s_battery_poll_ms >= BATTERY_POLL_MS) poll_battery(now);

    bm_power_tick_t t = bm_power_tick(&s_power, now, s_battery, busy);
    if (t.left_always_on) {
        ESP_LOGW(TAG, "battery below %d%%: leaving always_on", BM_LOW_BATTERY_PCT);
        bm_store_save_display(&s_power.cfg);
        if (usable()) send_display();
    }
    if (t.stage_changed) apply_stage();
    if (t.deep_sleep_due && !busy && (int32_t)(now - s_deep_retry_ms) >= 0) enter_deep_sleep();
}

static void app_task(void *arg)
{
    (void)arg;
    app_ev_t e;
    for (;;) {
        bool fast = bm_ptt_busy(&s_ptt) || s_banner.kind != BM_BANNER_NONE || s_input.held >= 0 ||
                    bm_upload_active(&s_upload);
        if (xQueueReceive(s_queue, &e, pdMS_TO_TICKS(fast ? 20 : 250)) == pdTRUE) {
            if (e.type == EV_KEY) on_key_edge(e.btn, e.down, e.ts_ms);
            else handle_link(&e.link);
        }
        periodic();
    }
}

void bm_app_post_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_queue || (ev != BSP_BTN_PRESS && ev != BSP_BTN_RELEASE)) return;
    app_ev_t e = {.type = EV_KEY, .btn = btn, .down = ev == BSP_BTN_PRESS, .ts_ms = now_ms()};
    xQueueSend(s_queue, &e, 0);
}

esp_err_t bm_app_start(void)
{
    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(app_ev_t));
    s_ring_mx = xSemaphoreCreateMutex();
    if (!s_queue || !s_ring_mx) return ESP_ERR_NO_MEM;

    bm_input_init(&s_input);
    bm_ptt_init(&s_ptt);
    bm_banner_init(&s_banner);
    bm_ring_clear(&s_ring);
    bm_reassembler_init(&s_rx, BM_RX_ACCEPT_MASK, on_frame, NULL);

    esp_err_t store_err = bm_store_init();
    if (store_err != ESP_OK) ESP_LOGE(TAG, "storage unavailable: %s (badge pages disabled)", esp_err_to_name(store_err));
    else bm_upload_init(&s_upload, bm_store_ops(), NULL);

    bm_display_cfg_t cfg;
    bm_store_load_display(&cfg);
    bm_power_init(&s_power, &cfg, now_ms());

    if (bsp_battery_init() != ESP_OK) ESP_LOGW(TAG, "battery gauge not found");
    s_battery = -2;                          // force the first poll to publish
    poll_battery(now_ms());

    // Microphone and Opus first, while a large contiguous heap block still exists.
    if (bm_audio_init(on_audio_frame, NULL) != ESP_OK) ESP_LOGE(TAG, "audio init failed: PTT unavailable");
    log_heap("after-audio");

    if (bm_link_init(on_link_event, NULL) != ESP_OK) return ESP_FAIL;
    if (bm_ui_init(bm_link_device_name()) != ESP_OK) return ESP_FAIL;
    bm_ui_set_battery(s_battery);

    int first = store_err == ESP_OK ? bm_pages_first_valid(bm_store_pages()) : -1;
    int boot_page = (store_err == ESP_OK && bm_store_pages()->page[0].valid) ? 0 : first;
    if (boot_page >= 0) show_page(boot_page);
    apply_stage();                           // backlight on, only after the first frame is set up
    log_heap("after-ui");

    bm_audio_start();
    if (bm_link_start() != ESP_OK) ESP_LOGE(TAG, "BLE start failed");
    log_heap("after-ble");

    if (xTaskCreate(app_task, "bm_app", APP_TASK_STACK, NULL, APP_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
