// main/bm_audio.h - microphone capture + Opus encoding for the PTT uplink.
// Ported from the MIT-licensed Pocket Intercom oc_audio by Shinku-Chen (capture
// half only; no playback).
//
// One resident task with a static stack captures 16 kHz mono PCM, encodes 60 ms
// Opus frames and hands each one to the callback (which must only copy / enqueue).
// The codec is suspended whenever no capture is requested.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define BM_AUDIO_HZ 16000U
#define BM_AUDIO_FRAME_MS 60U
#define BM_AUDIO_FRAME_SAMPLES (BM_AUDIO_HZ / 1000U * BM_AUDIO_FRAME_MS)   // 960

// level: peak of the frame, 0..100.
typedef void (*bm_audio_frame_cb_t)(const uint8_t *opus, size_t len, uint8_t level, void *ctx);

typedef struct {
    uint32_t frames_encoded;
    uint32_t read_errors;
    uint32_t encode_us_max;  // per turn (reset by bm_audio_begin)
    uint32_t encode_us_first; // first frame of the turn (cold flash cache)
    uint64_t encode_us_sum;   // per turn; average = sum / frames_encoded
    uint32_t wake_ms;        // codec wake + format time of the last capture start
    uint32_t stack_free;     // task stack high-water mark (bytes still free)
} bm_audio_stats_t;

esp_err_t bm_audio_init(bm_audio_frame_cb_t cb, void *ctx);
esp_err_t bm_audio_start(void);
// Starts capturing; frames appear through the callback.
esp_err_t bm_audio_begin(void);
// Stops capturing. With flush_tail the partial last frame is zero-padded and
// delivered before returning (waits up to ~300 ms); without it the tail is dropped.
void bm_audio_end(bool flush_tail);
bool bm_audio_active(void);
void bm_audio_get_stats(bm_audio_stats_t *out);
