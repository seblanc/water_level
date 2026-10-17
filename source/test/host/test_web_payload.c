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
}

int main(void)
{
    test_urlencode();
    test_device_id();
    test_form_ok();
    test_form_no_data();
    test_form_encoding_and_limits();
    test_parse_reply();

    if (s_failures != 0) {
        fprintf(stderr, "test_web_payload: %d of %d checks FAILED\n", s_failures, s_checks);
        return 1;
    }
    printf("test_web_payload: all %d checks passed\n", s_checks);
    return 0;
}
