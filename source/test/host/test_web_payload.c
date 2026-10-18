/*
 * Host unit tests for the web report form builder (contract with
 * web/water/post.php). Plain C99, no ESP-IDF. Build and run with
 * `make -C test/host run`.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "web_payload.h"

static int s_checks;
static int s_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        s_checks++;                                                              \
        if (!(cond)) {                                                           \
            s_failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

#define CHECK_STR(n, buf, expected)                                              \
    do {                                                                         \
        CHECK((n) == (int)strlen(expected));                                     \
        CHECK(strcmp((buf), (expected)) == 0);                                   \
    } while (0)

static web_post_t sample(void)
{
    web_post_t p = {
        .key = "k-1",
        .device = "a1b2c3d4e5f6",
        .distance_mm = 1750,
        .status = "OK",
        .age_ms = 42,
        .frames_ok = 1000,
        .checksum_err = 3,
        .rssi = -61,
        .uptime_s = 3600,
        .fw = "0.3.0",
    };
    return p;
}

static void test_urlencode(void)
{
    char buf[32];
    int n = web_payload_urlencode(buf, sizeof(buf), "aZ09-_.~");
    CHECK_STR(n, buf, "aZ09-_.~");
    n = web_payload_urlencode(buf, sizeof(buf), "a b&c=d/+");
    CHECK_STR(n, buf, "a%20b%26c%3Dd%2F%2B");
    n = web_payload_urlencode(buf, sizeof(buf), "\xC3\xA9");
    CHECK_STR(n, buf, "%C3%A9");
    n = web_payload_urlencode(buf, sizeof(buf), "");
    CHECK_STR(n, buf, "");

    /* "a%20" needs 5 bytes with NUL. */
    CHECK(web_payload_urlencode(buf, 5, "a ") == 4);
    CHECK(web_payload_urlencode(buf, 4, "a ") == -1);
    CHECK(web_payload_urlencode(buf, 1, "a") == -1);
    CHECK(web_payload_urlencode(NULL, 8, "a") == -1);
    CHECK(web_payload_urlencode(buf, sizeof(buf), NULL) == -1);
}

static void test_device_id(void)
{
    char buf[WEB_PAYLOAD_DEVICE_LEN];
    const uint8_t mac[6] = {0xA1, 0xB2, 0xC3, 0x04, 0x0E, 0xF6};
    int n = web_payload_device_id(buf, sizeof(buf), mac);
    CHECK_STR(n, buf, "a1b2c3040ef6");
    CHECK(web_payload_device_id(buf, WEB_PAYLOAD_DEVICE_LEN - 1, mac) == -1);
}

static void test_form_ok(void)
{
    char buf[WEB_PAYLOAD_FORM_MAX];
    web_post_t p = sample();
    int n = web_payload_form(buf, sizeof(buf), &p);
    CHECK_STR(n, buf,
              "key=k-1&device=a1b2c3d4e5f6&distance_mm=1750&status=OK&age_ms=42"
              "&frames_ok=1000&checksum_err=3&rssi=-61&uptime_s=3600&fw=0.3.0");
}

static void test_form_no_data(void)
{
    char buf[WEB_PAYLOAD_FORM_MAX];
    web_post_t p = sample();
    p.distance_mm = -1;
    p.status = "NO_DATA";
    p.age_ms = -1;
    p.rssi = 0;
    int n = web_payload_form(buf, sizeof(buf), &p);
    CHECK_STR(n, buf,
              "key=k-1&device=a1b2c3d4e5f6&distance_mm=&status=NO_DATA&age_ms=-1"
              "&frames_ok=1000&checksum_err=3&rssi=0&uptime_s=3600&fw=0.3.0");
}

static void test_form_encoding_and_limits(void)
{
    char buf[WEB_PAYLOAD_FORM_MAX];
    web_post_t p = sample();
    p.key = "a&b=c";
    p.fw = "v1.0-3-gabc dirty";
    int n = web_payload_form(buf, sizeof(buf), &p);
    CHECK(n > 0);
    CHECK(strncmp(buf, "key=a%26b%3Dc&", 14) == 0);
    CHECK(strstr(buf, "&fw=v1.0-3-gabc%20dirty") != NULL);

    /* Worst case documented in web_payload.h: 64-char key, 32-char fw, all encoded. */
    char key[65];
    char fw[33];
    memset(key, '%', 64);
    key[64] = '\0';
    memset(fw, ' ', 32);
    fw[32] = '\0';
    p = sample();
    p.key = key;
    p.fw = fw;
    p.status = "OUT_OF_RANGE";
    p.distance_mm = 2147483647;
    p.age_ms = -2147483647;
    p.frames_ok = 4294967295u;
    p.checksum_err = 4294967295u;
    p.rssi = -127;
    p.uptime_s = 4294967295u;
    CHECK(web_payload_form(buf, sizeof(buf), &p) > 0);

    /* Truncation clears the buffer and fails. */
    p = sample();
    CHECK(web_payload_form(buf, 20, &p) == -1);
    CHECK(buf[0] == '\0');

    p.key = NULL;
    CHECK(web_payload_form(buf, sizeof(buf), &p) == -1);
    CHECK(web_payload_form(buf, sizeof(buf), NULL) == -1);
}

static void test_parse_reply(void)
{
    web_reply_t r;
    CHECK(web_payload_parse_reply("{\"ok\":true,\"id\":1234,\"rows\":5678,\"level_pct\":50.0}", &r) == 0);
    CHECK(r.ok && r.id == 1234 && r.rows == 5678);

    /* Older server without id/rows: still a success. */
    CHECK(web_payload_parse_reply("{\"ok\":true,\"level_pct\":null}", &r) == 0);
    CHECK(r.ok && r.id == -1 && r.rows == -1);

    /* Blanks around the colon (pretty-printed JSON). */
    CHECK(web_payload_parse_reply("{ \"ok\" : true, \"id\" : 7 }", &r) == 0);
    CHECK(r.id == 7 && r.rows == -1);

    CHECK(web_payload_parse_reply("{\"ok\":false,\"error\":\"bad key\"}", &r) == -1);
    CHECK(!r.ok);
    /* A string value equal to a key name is not a key. */
    CHECK(web_payload_parse_reply("{\"error\":\"ok\",\"ok\":false}", &r) == -1);
    CHECK(web_payload_parse_reply("<html>ok</html>", &r) == -1);
    CHECK(web_payload_parse_reply("", &r) == -1);
    CHECK(web_payload_parse_reply(NULL, &r) == -1);
    CHECK(!r.ok && r.id == -1);
    CHECK(web_payload_parse_reply("{\"ok\":true}", NULL) == -1);

    /* Negative or non-numeric ids are reported as absent. */
    CHECK(web_payload_parse_reply("{\"ok\":true,\"id\":-3,\"rows\":\"x\"}", &r) == 0);
    CHECK(r.id == -1 && r.rows == -1);

    /* Run #20 reply with the readback echo. */
    CHECK(web_payload_parse_reply("{\"ok\":true,\"stored\":true,\"id\":9,\"rows\":10,"
                                  "\"distance_mm\":1750,\"uptime_s\":3600,\"level_pct\":41.7}",
                                  &r) == 0);
    CHECK(r.ok && r.stored && r.id == 9 && r.rows == 10);
    CHECK(r.distance_mm == 1750 && r.uptime_s == 3600 && r.error[0] == '\0');
    CHECK(web_payload_parse_reply("{\"ok\":true,\"stored\":true,\"id\":9,\"rows\":10,"
                                  "\"distance_mm\":null,\"uptime_s\":5}", &r) == 0);
    CHECK(r.distance_mm == -1 && r.uptime_s == 5);

    /* Error string kept, unsafe characters (here '=' and '<') dropped. */
    CHECK(web_payload_parse_reply("{\"ok\":false,\"error\":\"bad key\"}", &r) == -1);
    CHECK(strcmp(r.error, "bad key") == 0);
    CHECK(web_payload_parse_reply("{\"ok\":false,\"error\":\"a=b<c\"}", &r) == -1);
    CHECK(strcmp(r.error, "abc") == 0);
    CHECK(web_payload_parse_reply("{\"ok\":false,\"error\":\"0123456789012345678901234567890123456789\"}",
                                  &r) == -1);
    CHECK(strlen(r.error) == WEB_REPLY_ERROR_LEN - 1);
}

static void test_check_stored(void)
{
    web_post_t p = sample();
    web_reply_t r;

    web_payload_parse_reply("{\"ok\":true,\"stored\":true,\"id\":9,\"rows\":10,"
                            "\"distance_mm\":1750,\"uptime_s\":3600}", &r);
    CHECK(web_payload_check_stored(&r, &p) == WEB_STORE_CONFIRMED);

    /* Echo differs from what was sent. */
    web_payload_parse_reply("{\"ok\":true,\"stored\":true,\"id\":9,\"rows\":10,"
                            "\"distance_mm\":1751,\"uptime_s\":3600}", &r);
    CHECK(web_payload_check_stored(&r, &p) == WEB_STORE_MISMATCH);
    web_payload_parse_reply("{\"ok\":true,\"stored\":true,\"id\":9,\"rows\":10,"
                            "\"distance_mm\":1750,\"uptime_s\":3599}", &r);
    CHECK(web_payload_check_stored(&r, &p) == WEB_STORE_MISMATCH);
    web_payload_parse_reply("{\"ok\":true,\"stored\":true,\"id\":9,\"rows\":10}", &r);
    CHECK(web_payload_check_stored(&r, &p) == WEB_STORE_MISMATCH);

    /* NO_DATA: empty distance sent, NULL stored. */
    p.distance_mm = -1;
    p.status = "NO_DATA";
    web_payload_parse_reply("{\"ok\":true,\"stored\":true,\"id\":9,\"rows\":10,"
                            "\"distance_mm\":null,\"uptime_s\":3600}", &r);
    CHECK(web_payload_check_stored(&r, &p) == WEB_STORE_CONFIRMED);
    p = sample();

    /* Older post.php: ok without the echo. */
    web_payload_parse_reply("{\"ok\":true,\"id\":9,\"rows\":10}", &r);
    CHECK(web_payload_check_stored(&r, &p) == WEB_STORE_UNCONFIRMED);
    web_payload_parse_reply("{\"ok\":true}", &r);
    CHECK(web_payload_check_stored(&r, &p) == WEB_STORE_UNCONFIRMED);

    web_payload_parse_reply("{\"ok\":false,\"error\":\"readback\"}", &r);
    CHECK(web_payload_check_stored(&r, &p) == WEB_STORE_FAILED);
    web_payload_parse_reply("<html>ok</html>", &r);
    CHECK(web_payload_check_stored(&r, &p) == WEB_STORE_FAILED);
    CHECK(web_payload_check_stored(NULL, &p) == WEB_STORE_FAILED);
    CHECK(web_payload_check_stored(&r, NULL) == WEB_STORE_FAILED);
}

static void test_body_excerpt(void)
{
    char out[96];

    /* Hosting error page: tags and <style> content gone, text kept. */
    CHECK(web_payload_body_excerpt(out, sizeof(out),
                                   "<html><head><style>body{color:red}</style></head><body>"
                                   "<h1>500 Internal Server Error</h1>"
                                   "<p>The server encountered an error.</p></body></html>") > 0);
    CHECK(strcmp(out, "500 Internal Server Error The server encountered an error.") == 0);
    CHECK(web_payload_body_excerpt(out, sizeof(out), "<STYLE>p{}</STYLE>Hi") == 2);
    CHECK(strcmp(out, "Hi") == 0);

    /* PHP error text; '=' and '&' entities never reach the log. */
    web_payload_body_excerpt(out, sizeof(out), "\n<b>Fatal error</b>:  x");
    CHECK(strcmp(out, "Fatal error : x") == 0);
    web_payload_body_excerpt(out, sizeof(out), "a=b&amp;c&lt;d");
    CHECK(strcmp(out, "a b c d") == 0);
    web_payload_body_excerpt(out, sizeof(out), "a & b");
    CHECK(strcmp(out, "a b") == 0);

    /* Body cut off inside a tag or a <script>. */
    web_payload_body_excerpt(out, sizeof(out), "ok <div class");
    CHECK(strcmp(out, "ok") == 0);
    web_payload_body_excerpt(out, sizeof(out), "x<script>var a");
    CHECK(strcmp(out, "x") == 0);

    /* Truncation, then the trailing blank trimmed. */
    CHECK(web_payload_body_excerpt(out, 8, "abcdefghij") == 7);
    CHECK(strcmp(out, "abcdefg") == 0);
    CHECK(web_payload_body_excerpt(out, 5, "abc defg") == 3);
    CHECK(strcmp(out, "abc") == 0);

    CHECK(web_payload_body_excerpt(out, sizeof(out), "") == 0 && out[0] == '\0');
    CHECK(web_payload_body_excerpt(out, sizeof(out), NULL) == 0 && out[0] == '\0');
    CHECK(web_payload_body_excerpt(out, 0, "x") == -1);
    CHECK(web_payload_body_excerpt(NULL, sizeof(out), "x") == -1);
}

static void test_page_dir(void)
{
    char buf[64];
    int n = web_payload_page_dir(buf, sizeof(buf), "https://randoresto.ch/water/post.php");
    CHECK_STR(n, buf, "https://randoresto.ch/water/");
    n = web_payload_page_dir(buf, sizeof(buf), "http://h/a/b/post.php?x=/y#z");
    CHECK_STR(n, buf, "http://h/a/b/");
    n = web_payload_page_dir(buf, sizeof(buf), "https://host");
    CHECK_STR(n, buf, "https://host/");
    n = web_payload_page_dir(buf, sizeof(buf), "https://host?q=1");
    CHECK_STR(n, buf, "https://host/");

    CHECK(web_payload_page_dir(buf, sizeof(buf), "randoresto.ch/water/post.php") == -1);
    CHECK(buf[0] == '\0');
    CHECK(web_payload_page_dir(buf, sizeof(buf), "post.php?u=http://x/") == -1);
    /* "http://h/" needs 10 bytes with NUL. */
    CHECK(web_payload_page_dir(buf, 10, "http://h/p.php") == 9);
    CHECK(web_payload_page_dir(buf, 9, "http://h/p.php") == -1);
    CHECK(web_payload_page_dir(buf, 9, "http://h") == -1);
    CHECK(web_payload_page_dir(NULL, 9, "http://h") == -1);
    CHECK(web_payload_page_dir(buf, sizeof(buf), NULL) == -1);
}

/* api.php reply as json_encode writes it (sample(): a1b2c3d4e5f6, 1750 mm, 3600 s). */
#define PAGE_LATEST                                                                   \
    "{\"received_at\":1760788800,\"received_iso\":\"2026-10-18T12:00:00+00:00\","     \
    "\"age_s\":2,\"stale\":false,\"status\":\"OK\",\"distance_cm\":175.0,"             \
    "\"level_pct\":41.7,\"rssi\":-61,\"uptime_s\":3600,\"fw\":\"0.3.0\","              \
    "\"device\":\"a1b2c3d4e5f6\"}"

static void test_parse_page(void)
{
    web_page_t g;
    CHECK(web_payload_parse_page("{\"ok\":true,\"latest\":" PAGE_LATEST
                                 ",\"history\":[[1760788770,41.7]],\"hours\":1,\"count\":57,"
                                 "\"empty_cm\":30,\"full_cm\":5}", &g) == 0);
    CHECK(g.ok && g.has_latest && g.count == 57);
    CHECK(strcmp(g.device, "a1b2c3d4e5f6") == 0);
    CHECK(g.distance_mm == 1750 && g.uptime_s == 3600 && g.error[0] == '\0');

    /* Decimals, null distance, body cut off in the history (no count). */
    CHECK(web_payload_parse_page("{\"ok\":true,\"latest\":{\"distance_cm\":12.3,\"uptime_s\":5,"
                                 "\"device\":\"x\"},\"history\":[[1,2],[3", &g) == 0);
    CHECK(g.distance_mm == 123 && g.uptime_s == 5 && g.count == -1);
    web_payload_parse_page("{\"ok\":true,\"latest\":{\"distance_cm\":12,\"uptime_s\":null}}", &g);
    CHECK(g.distance_mm == 120 && g.uptime_s == -1 && g.device[0] == '\0');
    web_payload_parse_page("{\"ok\":true,\"latest\":{\"distance_cm\":null}}", &g);
    CHECK(g.has_latest && g.distance_mm == -1);

    /* Keys outside "latest" are not taken for it. */
    web_payload_parse_page("{\"ok\":true,\"uptime_s\":3600,\"latest\":null,\"count\":0}", &g);
    CHECK(g.ok && !g.has_latest && g.uptime_s == -1 && g.count == 0);

    CHECK(web_payload_parse_page("{\"ok\":false,\"error\":\"database\"}", &g) == -1);
    CHECK(!g.ok && strcmp(g.error, "database") == 0);
    CHECK(web_payload_parse_page("<html>Not Found</html>", &g) == -1);
    CHECK(web_payload_parse_page(NULL, &g) == -1 && !g.has_latest && g.count == -1);
    CHECK(web_payload_parse_page("{\"ok\":true}", NULL) == -1);
}

static void test_check_page(void)
{
    web_post_t p = sample();
    web_page_t g;

    web_payload_parse_page("{\"ok\":true,\"latest\":" PAGE_LATEST ",\"count\":1}", &g);
    CHECK(web_payload_check_page(&g, &p) == WEB_PAGE_SHOWS_POST);

    /* Another reading: other uptime, distance or device. */
    p.uptime_s = 3601;
    CHECK(web_payload_check_page(&g, &p) == WEB_PAGE_OTHER);
    p = sample();
    p.distance_mm = 1751;
    CHECK(web_payload_check_page(&g, &p) == WEB_PAGE_OTHER);
    p = sample();
    p.device = "a1b2c3d4e5f7";
    CHECK(web_payload_check_page(&g, &p) == WEB_PAGE_OTHER);
    p = sample();

    /* NO_DATA: empty distance sent, null shown. */
    web_payload_parse_page("{\"ok\":true,\"latest\":{\"distance_cm\":null,\"uptime_s\":3600,"
                           "\"device\":\"a1b2c3d4e5f6\"}}", &g);
    CHECK(web_payload_check_page(&g, &p) == WEB_PAGE_OTHER);
    p.distance_mm = -1;
    p.status = "NO_DATA";
    CHECK(web_payload_check_page(&g, &p) == WEB_PAGE_SHOWS_POST);
    p = sample();

    web_payload_parse_page("{\"ok\":true,\"latest\":null,\"count\":0}", &g);
    CHECK(web_payload_check_page(&g, &p) == WEB_PAGE_EMPTY);
    web_payload_parse_page("{\"ok\":false,\"error\":\"database\"}", &g);
    CHECK(web_payload_check_page(&g, &p) == WEB_PAGE_FAILED);
    CHECK(web_payload_check_page(NULL, &p) == WEB_PAGE_FAILED);
    CHECK(web_payload_check_page(&g, NULL) == WEB_PAGE_FAILED);
}

int main(void)
{
    test_urlencode();
    test_device_id();
    test_form_ok();
    test_form_no_data();
    test_form_encoding_and_limits();
    test_parse_reply();
    test_check_stored();
    test_body_excerpt();
    test_page_dir();
    test_parse_page();
    test_check_page();

    if (s_failures != 0) {
        fprintf(stderr, "test_web_payload: %d of %d checks FAILED\n", s_failures, s_checks);
        return 1;
    }
    printf("test_web_payload: all %d checks passed\n", s_checks);
    return 0;
}
