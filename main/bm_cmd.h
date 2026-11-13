// main/bm_cmd.h - CONTROL command parsing and EVENT JSON builders (pure C).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bm_power.h"

typedef enum {
    BM_CMD_UNKNOWN = 0,
    BM_CMD_HELLO,
    BM_CMD_GET_HELLO,
    BM_CMD_PTT_ACK,
    BM_CMD_SET_DISPLAY,
    BM_CMD_PUT_BEGIN,
    BM_CMD_PUT_END,
    BM_CMD_CLEAR_PAGE,
    BM_CMD_SET_AUDIO,
} bm_cmd_kind_t;

// Per-turn diagnostics sent in ptt_end.stats.
typedef struct {
    uint32_t enc_first_us, enc_avg_us, enc_max_us;
    uint32_t stack_free;
    uint32_t read_err;
    uint32_t clip;
} bm_turn_stats_t;

typedef struct {
    bm_cmd_kind_t kind;
    char name[24];        // "cmd" value (truncated), echoed in error replies
    bool bad_args;        // known command with missing / wrongly typed fields
    int64_t proto;        // hello
    uint32_t id;          // ptt_ack
    int page;             // put_begin / put_end / clear_page
    uint32_t size;        // put_begin
    uint32_t crc32;       // put_begin
    bm_display_patch_t patch;  // set_display
    int mic_gain_db;      // set_audio, already normalized (bm_mic_gain_normalize)
} bm_cmd_t;

// Parses one flat JSON object. Returns false when the payload is not a JSON
// object (malformed: log and ignore). Unknown "cmd" values give BM_CMD_UNKNOWN.
bool bm_cmd_parse(const uint8_t *json, size_t len, bm_cmd_t *out);

// Event builders: return the JSON length written (no NUL counted) or 0 if the
// buffer is too small.
size_t bm_ev_hello(char *buf, size_t cap, const char *fw, const char *model, int page_count,
                   int battery, int battery_mv, const bm_display_cfg_t *cfg, int mic_gain_db);
size_t bm_ev_ptt_start(char *buf, size_t cap, uint32_t id);
size_t bm_ev_ptt_end(char *buf, size_t cap, uint32_t id, uint32_t frames, uint32_t dropped,
                     const char *reason /* NULL, "timeout" or "link" */,
                     const bm_turn_stats_t *stats /* NULL: omitted */);
size_t bm_ev_audio(char *buf, size_t cap, int mic_gain_db);
size_t bm_ev_key(char *buf, size_t cap, bool up, bool long_press);
size_t bm_ev_display(char *buf, size_t cap, const bm_display_cfg_t *cfg);
size_t bm_ev_put_ready(char *buf, size_t cap, int page);
size_t bm_ev_put_done(char *buf, size_t cap, int page, bool ok, const char *err);
// battery_mv < 0 (unknown) omits the "battery_mv" field, also in hello.
size_t bm_ev_battery(char *buf, size_t cap, int pct, int battery_mv);
size_t bm_ev_error(char *buf, size_t cap, const char *cmd_name, const char *err);
