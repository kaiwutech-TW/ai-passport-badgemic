// main/bm_audio.c - see bm_audio.h. Ported from the MIT-licensed Pocket Intercom
// oc_audio by Shinku-Chen.
#include "bm_audio.h"

#include <string.h>

#include "bm_ring.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "opus.h"

static const char *TAG = "bm_audio";

// The SILK encoder keeps large locals on the stack. 24 KB (the reference firmware's
// size) left only 472 B free on the device (2026-11-13), so 32 KB. The stack is
// static: after NimBLE and LVGL the heap has no block this large left.
#define BM_AUDIO_TASK_STACK 32768U
// Above the app task (LVGL page/level redraws) and the BLE TX task (both 5): the I2S
// DMA ring holds only ~90 ms (6 x 240 frames), so a preempted encode loses audio.
#define BM_AUDIO_TASK_PRIO 6U
#define BM_AUDIO_IDLE_POLL_MS 10U
#define BM_AUDIO_END_TIMEOUT_MS 400U
#define BM_AUDIO_READ_SAMPLES 480U       // 30 ms per read
#define BM_AUDIO_TAIL_MIN_SAMPLES (BM_AUDIO_HZ / 100U)

static struct {
    bm_audio_frame_cb_t cb;
    void *ctx;
    TaskHandle_t task;
    StaticTask_t task_tcb;
    StackType_t task_stack[BM_AUDIO_TASK_STACK / sizeof(StackType_t)];
    OpusEncoder *enc;
    SemaphoreHandle_t done_sem;      // end(): codec suspended, tail delivered
    int16_t pcm[BM_AUDIO_FRAME_SAMPLES];
    size_t pcm_fill;
    uint8_t last_toc;
    uint8_t opus_buf[BM_RING_FRAME_MAX];
    volatile bool turn_requested;
    volatile bool flush_tail;
    volatile bool collecting;        // task confirms the codec is awake and capturing
    volatile bool running;
    bm_audio_stats_t stats;
} s_a;

static void encode_and_emit(bool pad)
{
    if (pad && s_a.pcm_fill < BM_AUDIO_TAIL_MIN_SAMPLES) {
        s_a.pcm_fill = 0;
        return;
    }
    if (pad) {
        memset(&s_a.pcm[s_a.pcm_fill], 0, (BM_AUDIO_FRAME_SAMPLES - s_a.pcm_fill) * sizeof(int16_t));
    }
    int peak = 0;
    for (size_t i = 0; i < BM_AUDIO_FRAME_SAMPLES; i++) {
        int v = s_a.pcm[i] < 0 ? -s_a.pcm[i] : s_a.pcm[i];
        if (v > peak) peak = v;
    }
    int64_t t0 = esp_timer_get_time();
    int n = opus_encode(s_a.enc, s_a.pcm, BM_AUDIO_FRAME_SAMPLES, s_a.opus_buf, sizeof(s_a.opus_buf));
    uint32_t dt = (uint32_t)(esp_timer_get_time() - t0);
    s_a.pcm_fill = 0;
    if (n <= 0) {
        // Never skip a frame: the Mac needs traffic every 60 ms. Send a TOC-only DTX packet.
        ESP_LOGW(TAG, "opus_encode returned %d, sending DTX packet", n);
        s_a.opus_buf[0] = (uint8_t)(s_a.last_toc & 0xFCu);
        n = 1;
    } else {
        s_a.last_toc = s_a.opus_buf[0];
    }
    if (s_a.stats.frames_encoded == 0) s_a.stats.encode_us_first = dt;
    s_a.stats.frames_encoded++;
    s_a.stats.encode_us_sum += dt;
    if (dt > s_a.stats.encode_us_max) s_a.stats.encode_us_max = dt;
    bool clip = peak >= BM_AUDIO_CLIP_PEAK;
    if (clip) s_a.stats.clip_frames++;
    if (s_a.cb) s_a.cb(s_a.opus_buf, (size_t)n, bm_meter_level_from_peak(peak), clip, s_a.ctx);
}

static void audio_task(void *arg)
{
    (void)arg;
    bool codec_awake = false;
    while (s_a.running) {
        if (!s_a.turn_requested) {
            if (codec_awake) {
                if (s_a.flush_tail && s_a.pcm_fill > 0) encode_and_emit(true);
                s_a.pcm_fill = 0;
                if (bsp_audio_sleep() != ESP_OK) ESP_LOGW(TAG, "codec suspend failed");
                codec_awake = false;
                s_a.collecting = false;
                s_a.stats.stack_free = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
                xSemaphoreGive(s_a.done_sem);
            }
            vTaskDelay(pdMS_TO_TICKS(BM_AUDIO_IDLE_POLL_MS));
            continue;
        }
        if (!codec_awake) {
            int64_t t0 = esp_timer_get_time();
            if (bsp_audio_wake() != ESP_OK || bsp_audio_set_format(BM_AUDIO_HZ, 16, 1) != ESP_OK) {
                ESP_LOGE(TAG, "codec wake / format failed");
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            codec_awake = true;
            s_a.pcm_fill = 0;
            s_a.collecting = true;
            s_a.stats.wake_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
            ESP_LOGI(TAG, "capture started (codec wake %u ms)", (unsigned)s_a.stats.wake_ms);
        }
        int16_t *dst = &s_a.pcm[s_a.pcm_fill];
        if (bsp_audio_read(dst, BM_AUDIO_READ_SAMPLES * sizeof(int16_t)) != ESP_OK) {
            s_a.stats.read_errors++;
            continue;
        }
        s_a.pcm_fill += BM_AUDIO_READ_SAMPLES;
        if (s_a.pcm_fill >= BM_AUDIO_FRAME_SAMPLES) encode_and_emit(false);
    }
    vTaskDelete(NULL);
}

esp_err_t bm_audio_init(bm_audio_frame_cb_t cb, void *ctx)
{
    if (cb == NULL) return ESP_ERR_INVALID_ARG;
    memset(&s_a, 0, sizeof(s_a));
    s_a.last_toc = 0x58;
    s_a.cb = cb;
    s_a.ctx = ctx;
    s_a.done_sem = xSemaphoreCreateBinary();
    if (s_a.done_sem == NULL) return ESP_ERR_NO_MEM;

    esp_err_t e = bsp_audio_init();
    if (e != ESP_OK) return e;
    e = bsp_audio_set_format(BM_AUDIO_HZ, 16, 1);
    if (e != ESP_OK) return e;

    // Created now, before NimBLE, while a large contiguous block still exists.
    int err = OPUS_OK;
    s_a.enc = opus_encoder_create(BM_AUDIO_HZ, 1, OPUS_APPLICATION_VOIP, &err);
    if (s_a.enc == NULL || err != OPUS_OK) {
        ESP_LOGE(TAG, "opus_encoder_create failed: %d", err);
        return ESP_ERR_NO_MEM;
    }
    opus_encoder_ctl(s_a.enc, OPUS_SET_COMPLEXITY(0));
    opus_encoder_ctl(s_a.enc, OPUS_SET_DTX(1));
    // Keep the codec suspended until the first press.
    if (bsp_audio_sleep() != ESP_OK) ESP_LOGW(TAG, "initial codec suspend failed");
    ESP_LOGI(TAG, "audio ready: %u Hz, %u ms frames, complexity 0, DTX", (unsigned)BM_AUDIO_HZ,
             (unsigned)BM_AUDIO_FRAME_MS);
    return ESP_OK;
}

esp_err_t bm_audio_start(void)
{
    if (s_a.enc == NULL) return ESP_ERR_INVALID_STATE;
    if (s_a.running) return ESP_OK;
    s_a.running = true;
    s_a.task = xTaskCreateStatic(audio_task, "bm_audio", BM_AUDIO_TASK_STACK, NULL, BM_AUDIO_TASK_PRIO,
                                 s_a.task_stack, &s_a.task_tcb);
    if (s_a.task == NULL) {
        s_a.running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t bm_audio_begin(void)
{
    if (!s_a.running) return ESP_ERR_INVALID_STATE;
    // A previous end() that timed out may still be winding down: let it finish first.
    for (int i = 0; i < 40 && s_a.collecting && !s_a.turn_requested; i++) vTaskDelay(pdMS_TO_TICKS(10));
    while (xSemaphoreTake(s_a.done_sem, 0) == pdTRUE) {}   // drop stale completion
    s_a.stats.frames_encoded = 0;
    s_a.stats.encode_us_max = 0;
    s_a.stats.encode_us_first = 0;
    s_a.stats.encode_us_sum = 0;
    s_a.stats.clip_frames = 0;
    s_a.flush_tail = false;
    s_a.turn_requested = true;
    return ESP_OK;
}

void bm_audio_end(bool flush_tail)
{
    if (!s_a.running || !s_a.turn_requested) return;
    s_a.flush_tail = flush_tail;
    bool was_collecting = s_a.collecting;
    s_a.turn_requested = false;
    // Not collecting yet (a very short press, codec still waking): the task suspends
    // the codec by itself on its next pass, there is nothing to wait for.
    if (was_collecting && xSemaphoreTake(s_a.done_sem, pdMS_TO_TICKS(BM_AUDIO_END_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "capture did not stop within %u ms", (unsigned)BM_AUDIO_END_TIMEOUT_MS);
    }
}

void bm_audio_set_mic_gain(int db)
{
    if (bsp_audio_set_mic_gain((float)db) != ESP_OK) ESP_LOGW(TAG, "set mic gain %d dB failed", db);
    else ESP_LOGI(TAG, "mic gain %d dB", db);
}

bool bm_audio_active(void)
{
    return s_a.turn_requested;
}

void bm_audio_get_stats(bm_audio_stats_t *out)
{
    if (out) *out = s_a.stats;
}
