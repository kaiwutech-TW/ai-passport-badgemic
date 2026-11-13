// main/bm_meter.h - microphone level and gain helpers (pure C, host-tested).
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Frame peak at or above this counts as clipped.
#define BM_AUDIO_CLIP_PEAK 32000
// The level meter stays red this long after a clipped frame.
#define BM_CLIP_HOLD_MS 300U

#define BM_MIC_GAIN_DEFAULT 24
#define BM_MIC_GAIN_MAX 42
#define BM_MIC_GAIN_STEP 6

// Peak (0..32768) -> 0..100 on a -50..0 dBFS scale.
uint8_t bm_meter_level_from_peak(int peak);
// Clamps to 0..42 and rounds down to a 6 dB step (what the ES8311 can do).
int bm_mic_gain_normalize(int64_t db);
