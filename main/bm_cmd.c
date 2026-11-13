#include "bm_cmd.h"

#include "bm_meter.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef enum { V_NONE, V_STR, V_INT, V_BOOL, V_OTHER } vtype_t;
typedef struct {
    vtype_t type;
    char str[24];
    int64_t i;
} val_t;

typedef struct {
    const char *p;
    const char *end;
} cur_t;

static void skip_ws(cur_t *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r')) c->p++;
}

// Parses a JSON string. Stores up to cap-1 chars (escapes decoded for the few
// simple ones; \uXXXX becomes '?').
static bool parse_string(cur_t *c, char *out, size_t cap)
{
    if (c->p >= c->end || *c->p != '"') return false;
    c->p++;
    size_t n = 0;
    while (c->p < c->end && *c->p != '"') {
        char ch = *c->p++;
        if (ch == '\\') {
            if (c->p >= c->end) return false;
            char e = *c->p++;
            switch (e) {
            case 'n': ch = '\n'; break;
            case 't': ch = '\t'; break;
            case 'u':
                if (c->end - c->p < 4) return false;
                c->p += 4;
                ch = '?';
                break;
            default: ch = e; break;
            }
        }
        if (out && n + 1 < cap) out[n++] = ch;
    }
    if (c->p >= c->end) return false;
    c->p++;
    if (out && cap) out[n] = '\0';
    return true;
}

static bool skip_composite(cur_t *c)
{
    int depth = 0;
    while (c->p < c->end) {
        char ch = *c->p;
        if (ch == '"') {
            if (!parse_string(c, NULL, 0)) return false;
            continue;
        }
        c->p++;
        if (ch == '{' || ch == '[') depth++;
        else if (ch == '}' || ch == ']') {
            if (--depth == 0) return true;
        }
    }
    return false;
}

static bool parse_value(cur_t *c, val_t *v)
{
    memset(v, 0, sizeof(*v));
    skip_ws(c);
    if (c->p >= c->end) return false;
    char ch = *c->p;
    if (ch == '"') {
        v->type = V_STR;
        return parse_string(c, v->str, sizeof(v->str));
    }
    if (ch == '{' || ch == '[') {
        v->type = V_OTHER;
        return skip_composite(c);
    }
    if (ch == '-' || isdigit((unsigned char)ch)) {
        bool neg = ch == '-';
        if (neg) c->p++;
        if (c->p >= c->end || !isdigit((unsigned char)*c->p)) return false;
        int64_t val = 0;
        while (c->p < c->end && isdigit((unsigned char)*c->p)) {
            if (val < (int64_t)1 << 50) val = val * 10 + (*c->p - '0');
            c->p++;
        }
        bool frac = false;
        while (c->p < c->end && (isdigit((unsigned char)*c->p) || *c->p == '.' || *c->p == 'e' ||
                                  *c->p == 'E' || *c->p == '+' || *c->p == '-')) {
            frac = true;
            c->p++;
        }
        v->type = frac ? V_OTHER : V_INT;
        v->i = neg ? -val : val;
        return true;
    }
    static const char *const lits[] = {"true", "false", "null"};
    for (int k = 0; k < 3; k++) {
        size_t l = strlen(lits[k]);
        if ((size_t)(c->end - c->p) >= l && memcmp(c->p, lits[k], l) == 0) {
            c->p += l;
            v->type = k == 2 ? V_OTHER : V_BOOL;
            v->i = k == 0;
            return true;
        }
    }
    return false;
}

static bool int_in(const val_t *v, int64_t lo, int64_t hi, int64_t *out)
{
    if (v->type != V_INT || v->i < lo || v->i > hi) return false;
    *out = v->i;
    return true;
}

bool bm_cmd_parse(const uint8_t *json, size_t len, bm_cmd_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!json) return false;
    cur_t c = {(const char *)json, (const char *)json + len};
    skip_ws(&c);
    if (c.p >= c.end || *c.p != '{') return false;
    c.p++;

    val_t cmd = {0}, proto = {0}, id = {0}, page = {0}, size = {0}, crc = {0}, mode = {0}, off_key = {0}, gain = {0};
    val_t disp[5] = {{0}};  // dim_s, off_s, sleep_min, brightness, dim_brightness
    static const char *const disp_keys[5] = {"dim_s", "off_s", "sleep_min", "brightness", "dim_brightness"};
    bool closed = false;

    for (;;) {
        skip_ws(&c);
        if (c.p >= c.end) return false;
        if (*c.p == '}') { c.p++; closed = true; break; }
        char key[24];
        if (!parse_string(&c, key, sizeof(key))) return false;
        skip_ws(&c);
        if (c.p >= c.end || *c.p != ':') return false;
        c.p++;
        val_t v;
        if (!parse_value(&c, &v)) return false;
        if (!strcmp(key, "cmd")) cmd = v;
        else if (!strcmp(key, "proto")) proto = v;
        else if (!strcmp(key, "id")) id = v;
        else if (!strcmp(key, "page")) page = v;
        else if (!strcmp(key, "size")) size = v;
        else if (!strcmp(key, "crc32")) crc = v;
        else if (!strcmp(key, "mode")) mode = v;
        else if (!strcmp(key, "off_key")) off_key = v;
        else if (!strcmp(key, "mic_gain_db")) gain = v;
        else {
            for (int k = 0; k < 5; k++) if (!strcmp(key, disp_keys[k])) disp[k] = v;
        }
        skip_ws(&c);
        if (c.p < c.end && *c.p == ',') { c.p++; continue; }
        if (c.p < c.end && *c.p == '}') { c.p++; closed = true; break; }
        return false;
    }
    if (!closed) return false;

    if (cmd.type != V_STR) {
        out->kind = BM_CMD_UNKNOWN;
        return true;
    }
    memcpy(out->name, cmd.str, sizeof(out->name));
    int64_t t;
    if (!strcmp(cmd.str, "hello")) {
        out->kind = BM_CMD_HELLO;
        if (int_in(&proto, 0, 1 << 30, &t)) out->proto = t; else out->bad_args = true;
    } else if (!strcmp(cmd.str, "get_hello")) {
        out->kind = BM_CMD_GET_HELLO;
    } else if (!strcmp(cmd.str, "ptt_ack")) {
        out->kind = BM_CMD_PTT_ACK;
        if (int_in(&id, 0, 0xFFFFFFFFll, &t)) out->id = (uint32_t)t; else out->bad_args = true;
    } else if (!strcmp(cmd.str, "set_display")) {
        out->kind = BM_CMD_SET_DISPLAY;
        bm_display_patch_t *p = &out->patch;
        if (mode.type != V_NONE) {
            if (mode.type == V_STR && !strcmp(mode.str, "auto")) { p->mask |= BM_PATCH_MODE; p->always_on = false; }
            else if (mode.type == V_STR && !strcmp(mode.str, "always_on")) { p->mask |= BM_PATCH_MODE; p->always_on = true; }
            else out->bad_args = true;
        }
        if (off_key.type != V_NONE) {
            static const char *const names[3] = {"none", "up_long", "down_long"};
            int k = -1;
            for (int i = 0; off_key.type == V_STR && i < 3; i++) if (!strcmp(off_key.str, names[i])) k = i;
            if (k < 0) out->bad_args = true;
            else { p->mask |= BM_PATCH_OFF_KEY; p->off_key = (uint8_t)k; }
        }
        static const uint32_t bits[5] = {BM_PATCH_DIM_S, BM_PATCH_OFF_S, BM_PATCH_SLEEP_MIN,
                                         BM_PATCH_BRIGHTNESS, BM_PATCH_DIM_BRIGHTNESS};
        int64_t *dst[5] = {&p->dim_s, &p->off_s, &p->sleep_min, &p->brightness, &p->dim_brightness};
        for (int k = 0; k < 5; k++) {
            if (disp[k].type == V_NONE) continue;
            if (disp[k].type != V_INT) { out->bad_args = true; continue; }
            p->mask |= bits[k];
            *dst[k] = disp[k].i;  // clamped by bm_display_cfg_apply
        }
    } else if (!strcmp(cmd.str, "set_audio")) {
        out->kind = BM_CMD_SET_AUDIO;
        if (gain.type == V_INT) out->mic_gain_db = bm_mic_gain_normalize(gain.i);
        else out->bad_args = true;
    } else if (!strcmp(cmd.str, "put_begin")) {
        out->kind = BM_CMD_PUT_BEGIN;
        int64_t pg = 0, sz = 0, cr = 0;
        if (int_in(&page, 0, 255, &pg) && int_in(&size, 0, 0x7FFFFFFFll, &sz) &&
            int_in(&crc, 0, 0xFFFFFFFFll, &cr)) {
            out->page = (int)pg; out->size = (uint32_t)sz; out->crc32 = (uint32_t)cr;
        } else out->bad_args = true;
    } else if (!strcmp(cmd.str, "put_end") || !strcmp(cmd.str, "clear_page")) {
        out->kind = cmd.str[0] == 'p' ? BM_CMD_PUT_END : BM_CMD_CLEAR_PAGE;
        int64_t pg = 0;
        if (int_in(&page, 0, 255, &pg)) out->page = (int)pg; else out->bad_args = true;
    } else {
        out->kind = BM_CMD_UNKNOWN;
    }
    return true;
}

// ---- event builders ----
static size_t finish(int n, size_t cap)
{
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

static size_t cfg_fields(char *buf, size_t cap, const bm_display_cfg_t *c)
{
    static const char *const off_keys[3] = {"none", "up_long", "down_long"};
    int n = snprintf(buf, cap, "\"mode\":\"%s\",\"dim_s\":%u,\"off_s\":%u,\"sleep_min\":%u,"
                               "\"brightness\":%u,\"dim_brightness\":%u,\"off_key\":\"%s\"",
                     c->always_on ? "always_on" : "auto", c->dim_s, c->off_s, c->sleep_min,
                     c->brightness, c->dim_brightness, off_keys[c->off_key <= 2 ? c->off_key : 0]);
    return finish(n, cap);
}

size_t bm_ev_hello(char *buf, size_t cap, const char *fw, const char *model, int page_count,
                   int battery, const bm_display_cfg_t *cfg, int mic_gain_db)
{
    char disp[160];
    if (!cfg_fields(disp, sizeof(disp), cfg)) return 0;
    int n = snprintf(buf, cap,
                     "{\"ev\":\"hello\",\"proto\":%u,\"fw\":\"%s\",\"model\":\"%s\","
                     "\"caps\":[\"opus\",\"put\",\"display\",\"audio\"],"
                     "\"page\":{\"w\":240,\"h\":320,\"fmt\":\"rgb565le\",\"count\":%d},"
                     "\"battery\":%d,\"display\":{%s},\"audio\":{\"mic_gain_db\":%d}}",
                     (unsigned)1, fw, model, page_count, battery, disp, mic_gain_db);
    return finish(n, cap);
}

size_t bm_ev_ptt_start(char *buf, size_t cap, uint32_t id)
{
    return finish(snprintf(buf, cap, "{\"ev\":\"ptt_start\",\"id\":%u}", (unsigned)id), cap);
}

size_t bm_ev_ptt_end(char *buf, size_t cap, uint32_t id, uint32_t frames, uint32_t dropped,
                     const char *reason, const bm_turn_stats_t *st)
{
    char rs[32] = "", ss[200] = "";
    if (reason) snprintf(rs, sizeof(rs), ",\"reason\":\"%s\"", reason);
    if (st) {
        snprintf(ss, sizeof(ss),
                 ",\"stats\":{\"enc_first_us\":%u,\"enc_avg_us\":%u,\"enc_max_us\":%u,"
                 "\"stack_free\":%u,\"read_err\":%u,\"clip\":%u}",
                 (unsigned)st->enc_first_us, (unsigned)st->enc_avg_us, (unsigned)st->enc_max_us,
                 (unsigned)st->stack_free, (unsigned)st->read_err, (unsigned)st->clip);
    }
    int n = snprintf(buf, cap, "{\"ev\":\"ptt_end\",\"id\":%u,\"frames\":%u,\"dropped\":%u%s%s}",
                     (unsigned)id, (unsigned)frames, (unsigned)dropped, rs, ss);
    return finish(n, cap);
}

size_t bm_ev_audio(char *buf, size_t cap, int mic_gain_db)
{
    return finish(snprintf(buf, cap, "{\"ev\":\"audio\",\"mic_gain_db\":%d}", mic_gain_db), cap);
}

size_t bm_ev_key(char *buf, size_t cap, bool up, bool long_press)
{
    return finish(snprintf(buf, cap, "{\"ev\":\"key\",\"key\":\"%s\",\"gesture\":\"%s\"}",
                           up ? "up" : "down", long_press ? "long" : "click"), cap);
}

size_t bm_ev_display(char *buf, size_t cap, const bm_display_cfg_t *cfg)
{
    char disp[160];
    if (!cfg_fields(disp, sizeof(disp), cfg)) return 0;
    return finish(snprintf(buf, cap, "{\"ev\":\"display\",%s}", disp), cap);
}

size_t bm_ev_put_ready(char *buf, size_t cap, int page)
{
    return finish(snprintf(buf, cap, "{\"ev\":\"put_ready\",\"page\":%d}", page), cap);
}

size_t bm_ev_put_done(char *buf, size_t cap, int page, bool ok, const char *err)
{
    if (ok) return finish(snprintf(buf, cap, "{\"ev\":\"put_done\",\"page\":%d,\"ok\":true}", page), cap);
    return finish(snprintf(buf, cap, "{\"ev\":\"put_done\",\"page\":%d,\"ok\":false,\"err\":\"%s\"}",
                           page, err ? err : "failed"), cap);
}

size_t bm_ev_battery(char *buf, size_t cap, int pct)
{
    return finish(snprintf(buf, cap, "{\"ev\":\"battery\",\"battery\":%d}", pct), cap);
}

size_t bm_ev_error(char *buf, size_t cap, const char *cmd_name, const char *err)
{
    char safe[24];
    size_t n = 0;
    for (const char *s = cmd_name ? cmd_name : ""; *s && n + 1 < sizeof(safe); s++) {
        safe[n++] = (isalnum((unsigned char)*s) || *s == '_') ? *s : '?';
    }
    safe[n] = '\0';
    return finish(snprintf(buf, cap, "{\"ev\":\"error\",\"cmd\":\"%s\",\"err\":\"%s\"}", safe, err), cap);
}
