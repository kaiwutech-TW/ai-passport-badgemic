// Host tests: CONTROL JSON parsing and EVENT builders.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bm_cmd.h"

static bool parse(const char *s, bm_cmd_t *c)
{
    return bm_cmd_parse((const uint8_t *)s, strlen(s), c);
}

static void test_commands(void)
{
    bm_cmd_t c;
    assert(parse("{\"cmd\":\"hello\",\"proto\":1,\"app\":\"BadgeMic\",\"ver\":\"0.1.0\"}", &c));
    assert(c.kind == BM_CMD_HELLO && !c.bad_args && c.proto == 1);
    assert(parse("{\"cmd\":\"hello\"}", &c) && c.kind == BM_CMD_HELLO && c.bad_args);
    assert(parse("{\"cmd\":\"get_hello\"}", &c) && c.kind == BM_CMD_GET_HELLO && !c.bad_args);

    assert(parse("{ \"cmd\" : \"ptt_ack\" , \"id\" : 7 }", &c));
    assert(c.kind == BM_CMD_PTT_ACK && c.id == 7 && !c.bad_args);

    assert(parse("{\"cmd\":\"put_begin\",\"page\":1,\"size\":153600,\"crc32\":3735928559}", &c));
    assert(c.kind == BM_CMD_PUT_BEGIN && !c.bad_args && c.page == 1 && c.size == 153600 &&
           c.crc32 == 0xDEADBEEFu);
    assert(parse("{\"crc32\":4294967295,\"size\":5,\"page\":0,\"cmd\":\"put_begin\"}", &c));
    assert(!c.bad_args && c.crc32 == 0xFFFFFFFFu);   // key order does not matter
    assert(parse("{\"cmd\":\"put_begin\",\"page\":\"0\",\"size\":5,\"crc32\":1}", &c) && c.bad_args);
    assert(parse("{\"cmd\":\"put_begin\",\"page\":0,\"size\":5,\"crc32\":4294967296}", &c) && c.bad_args);

    assert(parse("{\"cmd\":\"put_end\",\"page\":1}", &c) && c.kind == BM_CMD_PUT_END && c.page == 1);
    assert(parse("{\"cmd\":\"clear_page\",\"page\":0}", &c) && c.kind == BM_CMD_CLEAR_PAGE && !c.bad_args);
    assert(parse("{\"cmd\":\"clear_page\"}", &c) && c.bad_args);
}

static void test_unknown_and_malformed(void)
{
    bm_cmd_t c;
    assert(parse("{\"cmd\":\"reboot\",\"x\":{\"a\":[1,2,{\"b\":\"}\"}]}}", &c));
    assert(c.kind == BM_CMD_UNKNOWN && strcmp(c.name, "reboot") == 0);
    assert(parse("{\"nocmd\":1}", &c) && c.kind == BM_CMD_UNKNOWN);
    assert(!parse("not json", &c));
    assert(!parse("{\"cmd\":\"hello\"", &c));
    assert(!parse("{\"cmd\" \"hello\"}", &c));
    assert(!parse("", &c));
    assert(!parse("[1,2]", &c));
    char longname[100];
    memset(longname, 'a', sizeof(longname));
    memcpy(longname, "{\"cmd\":\"", 8);
    strcpy(longname + 60, "\"}");
    assert(parse(longname, &c) && c.kind == BM_CMD_UNKNOWN && strlen(c.name) == 23);
}

static void test_set_display(void)
{
    bm_cmd_t c;
    assert(parse("{\"cmd\":\"set_display\",\"mode\":\"always_on\"}", &c));
    assert(c.kind == BM_CMD_SET_DISPLAY && !c.bad_args && c.patch.mask == BM_PATCH_MODE && c.patch.always_on);
    assert(parse("{\"cmd\":\"set_display\",\"mode\":\"sideways\"}", &c) && c.bad_args);
    assert(parse("{\"cmd\":\"set_display\",\"dim_s\":\"x\"}", &c) && c.bad_args);

    assert(parse("{\"cmd\":\"set_display\",\"mode\":\"auto\",\"dim_s\":10,\"off_s\":60,\"sleep_min\":0,"
                 "\"brightness\":90,\"dim_brightness\":15}", &c));
    assert(!c.bad_args);
    assert(c.patch.mask == (BM_PATCH_MODE | BM_PATCH_DIM_S | BM_PATCH_OFF_S | BM_PATCH_SLEEP_MIN |
                            BM_PATCH_BRIGHTNESS | BM_PATCH_DIM_BRIGHTNESS));
    bm_display_cfg_t cfg;
    bm_display_cfg_default(&cfg);
    bm_display_cfg_apply(&cfg, &c.patch);
    assert(!cfg.always_on && cfg.dim_s == 10 && cfg.off_s == 60 && cfg.sleep_min == 0 &&
           cfg.brightness == 90 && cfg.dim_brightness == 15);

    // off_key
    assert(parse("{\"cmd\":\"set_display\",\"off_key\":\"up_long\"}", &c) && !c.bad_args);
    assert(c.patch.mask == BM_PATCH_OFF_KEY && c.patch.off_key == BM_OFF_KEY_UP_LONG);
    bm_display_cfg_apply(&cfg, &c.patch);
    assert(cfg.off_key == BM_OFF_KEY_UP_LONG && cfg.dim_s == 10);
    assert(parse("{\"cmd\":\"set_display\",\"off_key\":\"ok_long\"}", &c) && c.bad_args);
    assert(parse("{\"cmd\":\"set_display\",\"off_key\":1}", &c) && c.bad_args);

    // omitted fields keep their value
    assert(parse("{\"cmd\":\"set_display\",\"brightness\":50}", &c));
    bm_display_cfg_apply(&cfg, &c.patch);
    assert(cfg.dim_s == 10 && cfg.brightness == 50 && cfg.dim_brightness == 15 &&
           cfg.off_key == BM_OFF_KEY_UP_LONG);
}

static void test_events(void)
{
    char b[400];
    bm_display_cfg_t cfg;
    bm_display_cfg_default(&cfg);
    size_t n = bm_ev_hello(b, sizeof(b), "0.1.0", "Passport-3A2B", 2, 87, &cfg);
    assert(n && strlen(b) == n);
    assert(strstr(b, "\"ev\":\"hello\",\"proto\":1,\"fw\":\"0.1.0\",\"model\":\"Passport-3A2B\""));
    assert(strstr(b, "\"page\":{\"w\":240,\"h\":320,\"fmt\":\"rgb565le\",\"count\":2}"));
    assert(strstr(b, "\"battery\":87,\"display\":{\"mode\":\"auto\",\"dim_s\":30,\"off_s\":120,"
                     "\"sleep_min\":30,\"brightness\":80,\"dim_brightness\":20,\"off_key\":\"none\"}}"));
    assert(bm_ev_hello(b, 20, "0.1.0", "x", 2, 1, &cfg) == 0);   // too small

    n = bm_ev_display(b, sizeof(b), &cfg);
    assert(n && strcmp(b, "{\"ev\":\"display\",\"mode\":\"auto\",\"dim_s\":30,\"off_s\":120,"
                          "\"sleep_min\":30,\"brightness\":80,\"dim_brightness\":20,\"off_key\":\"none\"}") == 0);
    cfg.always_on = true;
    cfg.off_key = BM_OFF_KEY_DOWN_LONG;
    bm_ev_display(b, sizeof(b), &cfg);
    assert(strstr(b, "\"mode\":\"always_on\"") && strstr(b, "\"off_key\":\"down_long\""));

    bm_ev_ptt_start(b, sizeof(b), 3);
    assert(strcmp(b, "{\"ev\":\"ptt_start\",\"id\":3}") == 0);
    bm_ev_ptt_end(b, sizeof(b), 3, 40, 2, NULL);
    assert(strcmp(b, "{\"ev\":\"ptt_end\",\"id\":3,\"frames\":40,\"dropped\":2}") == 0);
    bm_ev_ptt_end(b, sizeof(b), 3, 40, 2, "timeout");
    assert(strcmp(b, "{\"ev\":\"ptt_end\",\"id\":3,\"frames\":40,\"dropped\":2,\"reason\":\"timeout\"}") == 0);
    bm_ev_key(b, sizeof(b), true, false);
    assert(strcmp(b, "{\"ev\":\"key\",\"key\":\"up\",\"gesture\":\"click\"}") == 0);
    bm_ev_key(b, sizeof(b), false, true);
    assert(strcmp(b, "{\"ev\":\"key\",\"key\":\"down\",\"gesture\":\"long\"}") == 0);
    bm_ev_put_ready(b, sizeof(b), 1);
    assert(strcmp(b, "{\"ev\":\"put_ready\",\"page\":1}") == 0);
    bm_ev_put_done(b, sizeof(b), 0, true, NULL);
    assert(strcmp(b, "{\"ev\":\"put_done\",\"page\":0,\"ok\":true}") == 0);
    bm_ev_put_done(b, sizeof(b), 0, false, "crc");
    assert(strcmp(b, "{\"ev\":\"put_done\",\"page\":0,\"ok\":false,\"err\":\"crc\"}") == 0);
    bm_ev_battery(b, sizeof(b), 86);
    assert(strcmp(b, "{\"ev\":\"battery\",\"battery\":86}") == 0);
    bm_ev_error(b, sizeof(b), "bad\"cmd\\", "unknown");
    assert(strcmp(b, "{\"ev\":\"error\",\"cmd\":\"bad?cmd?\",\"err\":\"unknown\"}") == 0);
}

int main(void)
{
    test_commands();
    test_unknown_and_malformed();
    test_set_display();
    test_events();
    puts("test_bm_cmd: 4 tests PASS");
    return 0;
}
