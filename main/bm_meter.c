#include "bm_meter.h"

#include <math.h>

uint8_t bm_meter_level_from_peak(int peak)
{
    if (peak <= 0) return 0;
    float db = 20.0f * log10f((float)peak / 32768.0f);
    float lv = (db + 50.0f) * 2.0f;
    if (lv <= 0.0f) return 0;
    if (lv >= 100.0f) return 100;
    return (uint8_t)(lv + 0.5f);
}

int bm_mic_gain_normalize(int64_t db)
{
    if (db < 0) db = 0;
    if (db > BM_MIC_GAIN_MAX) db = BM_MIC_GAIN_MAX;
    return (int)(db - db % BM_MIC_GAIN_STEP);
}
