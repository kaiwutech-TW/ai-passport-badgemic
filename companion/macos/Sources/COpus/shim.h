#ifndef BADGEMIC_COPUS_SHIM_H
#define BADGEMIC_COPUS_SHIM_H
#include <opus.h>

// opus_*_ctl are variadic and therefore not importable into Swift; wrap what we need.
static inline int bm_opus_set_dtx(OpusEncoder *e, int v) { return opus_encoder_ctl(e, OPUS_SET_DTX(v)); }
static inline int bm_opus_set_complexity(OpusEncoder *e, int v) { return opus_encoder_ctl(e, OPUS_SET_COMPLEXITY(v)); }
static inline int bm_opus_set_bitrate(OpusEncoder *e, int v) { return opus_encoder_ctl(e, OPUS_SET_BITRATE(v)); }
#endif
