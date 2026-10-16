/*
 * Host unit tests for the A02YYUW frame parser and median filter.
 * Plain C99, no ESP-IDF. Build and run with `make -C test/host run`.
 */
#include <stdint.h>
#include <stdio.h>

#include "a02yyuw_filter.h"
#include "a02yyuw_parser.h"

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

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

/* Feed bytes; store decoded distances in out[] (up to max). Returns the number decoded. */
static size_t feed(a02yyuw_parser_t *p, const uint8_t *data, size_t len, uint16_t *out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        uint16_t mm;
        if (a02yyuw_parser_feed(p, data[i], &mm)) {
            if (n < max) {
                out[n] = mm;
            }
            n++;
        }
    }
    return n;
}

/* Reference frames: SUM = (0xFF + H + L) & 0xFF */
static const uint8_t F_1953[] = {0xFF, 0x07, 0xA1, 0xA7}; /* 0x07A1 = 1953 mm */
static const uint8_t F_1023[] = {0xFF, 0x03, 0xFF, 0x01}; /* 0xFF as DATA_L */
static const uint8_t F_65280[] = {0xFF, 0xFF, 0x00, 0xFE}; /* 0xFF as DATA_H */
static const uint8_t F_766[] = {0xFF, 0x02, 0xFE, 0xFF};  /* 0xFF as SUM */

static void test_checksum(void)
{
    CHECK(a02yyuw_checksum(0x07, 0xA1) == 0xA7);
    CHECK(a02yyuw_checksum(0x03, 0xFF) == 0x01);
    CHECK(a02yyuw_checksum(0xFF, 0x00) == 0xFE);
    CHECK(a02yyuw_checksum(0x02, 0xFE) == 0xFF);
}

static void test_valid_frame(void)
{
    a02yyuw_parser_t p;
    uint16_t out[4];
    a02yyuw_parser_init(&p);
    CHECK(feed(&p, F_1953, sizeof(F_1953), out, COUNT_OF(out)) == 1);
    CHECK(out[0] == 1953);
    CHECK(p.frames_ok == 1);
    CHECK(p.checksum_err == 0);
    CHECK(p.sync_skips == 0);
}

static void test_bad_checksum_then_valid(void)
{
    const uint8_t data[] = {0xFF, 0x07, 0xA1, 0x00, 0xFF, 0x07, 0xA1, 0xA7};
    a02yyuw_parser_t p;
    uint16_t out[4];
    a02yyuw_parser_init(&p);
    CHECK(feed(&p, data, sizeof(data), out, COUNT_OF(out)) == 1);
    CHECK(out[0] == 1953);
    CHECK(p.checksum_err == 1);
    CHECK(p.sync_skips == 3);
}

static void test_garbage_before_header(void)
{
    const uint8_t data[] = {0x00, 0x12, 0x34, 0xFF, 0x07, 0xA1, 0xA7};
    a02yyuw_parser_t p;
    uint16_t out[4];
    a02yyuw_parser_init(&p);
    CHECK(feed(&p, data, sizeof(data), out, COUNT_OF(out)) == 1);
    CHECK(out[0] == 1953);
    CHECK(p.sync_skips == 3);
    CHECK(p.checksum_err == 0);
}

static void test_ff_inside_frames_back_to_back(void)
{
    a02yyuw_parser_t p;
    uint16_t out[8];
    size_t n = 0;
    a02yyuw_parser_init(&p);
    n += feed(&p, F_766, sizeof(F_766), &out[n], COUNT_OF(out) - n);
    n += feed(&p, F_65280, sizeof(F_65280), &out[n], COUNT_OF(out) - n);
    n += feed(&p, F_1023, sizeof(F_1023), &out[n], COUNT_OF(out) - n);
    n += feed(&p, F_1953, sizeof(F_1953), &out[n], COUNT_OF(out) - n);
    CHECK(n == 4);
    CHECK(out[0] == 766);
    CHECK(out[1] == 65280);
    CHECK(out[2] == 1023);
    CHECK(out[3] == 1953);
    CHECK(p.checksum_err == 0);
    CHECK(p.sync_skips == 0);
}

static void test_start_mid_frame(void)
{
    /* Stream starts on the tail (DATA_L=0xFF, SUM=0x01) of a previous frame. */
    const uint8_t data[] = {0xFF, 0x01, 0xFF, 0x03, 0xFF, 0x01};
    a02yyuw_parser_t p;
    uint16_t out[4];
    a02yyuw_parser_init(&p);
    CHECK(feed(&p, data, sizeof(data), out, COUNT_OF(out)) == 1);
    CHECK(out[0] == 1023);
    CHECK(p.checksum_err == 1);
    CHECK(p.sync_skips == 1);
}

static void test_split_across_reads(void)
{
    a02yyuw_parser_t p;
    uint16_t out[4];
    a02yyuw_parser_init(&p);
    CHECK(feed(&p, F_1953, 2, out, COUNT_OF(out)) == 0);
    CHECK(feed(&p, F_1953 + 2, 2, out, COUNT_OF(out)) == 1);
    CHECK(out[0] == 1953);
}

static void test_many_frames(void)
{
    a02yyuw_parser_t p;
    uint16_t out[1];
    size_t n = 0;
    a02yyuw_parser_init(&p);
    for (int i = 0; i < 100; i++) {
        n += feed(&p, F_1023, sizeof(F_1023), out, COUNT_OF(out));
    }
    CHECK(n == 100);
    CHECK(p.frames_ok == 100);
    CHECK(p.checksum_err == 0);
}

static void test_all_ff_never_decodes(void)
{
    uint8_t data[100];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = 0xFF;
    }
    a02yyuw_parser_t p;
    uint16_t out[1];
    a02yyuw_parser_init(&p);
    CHECK(feed(&p, data, sizeof(data), out, COUNT_OF(out)) == 0);
    CHECK(p.frames_ok == 0);
    CHECK(p.checksum_err == 97);
    /* A valid frame afterwards must still be found, without a reset. */
    CHECK(feed(&p, F_1953, sizeof(F_1953), out, COUNT_OF(out)) == 1);
    CHECK(out[0] == 1953);
}

static void test_null_output_pointer(void)
{
    a02yyuw_parser_t p;
    bool got = false;
    a02yyuw_parser_init(&p);
    for (size_t i = 0; i < sizeof(F_1953); i++) {
        got = a02yyuw_parser_feed(&p, F_1953[i], NULL);
    }
    CHECK(got);
    CHECK(p.frames_ok == 1);
}

static void test_reset_drops_partial_keeps_counters(void)
{
    a02yyuw_parser_t p;
    uint16_t out[2];
    a02yyuw_parser_init(&p);
    CHECK(feed(&p, F_1953, sizeof(F_1953), out, COUNT_OF(out)) == 1);
    CHECK(feed(&p, F_1953, 2, out, COUNT_OF(out)) == 0);
    a02yyuw_parser_reset(&p);
    CHECK(feed(&p, F_1953 + 2, 2, out, COUNT_OF(out)) == 0);
    CHECK(p.frames_ok == 1);
    CHECK(p.sync_skips == 2);
}

static void test_range(void)
{
    CHECK(!a02yyuw_distance_in_range(0));
    CHECK(!a02yyuw_distance_in_range(29));
    CHECK(a02yyuw_distance_in_range(30));
    CHECK(a02yyuw_distance_in_range(4500));
    CHECK(!a02yyuw_distance_in_range(4501));
    CHECK(!a02yyuw_distance_in_range(65280));
}

static void test_filter_passthrough(void)
{
    a02yyuw_filter_t f;
    a02yyuw_filter_init(&f, 1);
    CHECK(a02yyuw_filter_push(&f, 100) == 100);
    CHECK(a02yyuw_filter_push(&f, 5000) == 5000);
    CHECK(a02yyuw_filter_push(&f, 7) == 7);
}

static void test_filter_median(void)
{
    a02yyuw_filter_t f;
    a02yyuw_filter_init(&f, 5);
    CHECK(a02yyuw_filter_push(&f, 100) == 100);
    CHECK(a02yyuw_filter_push(&f, 5000) == 100);  /* lower middle of {100, 5000} */
    CHECK(a02yyuw_filter_push(&f, 102) == 102);
    CHECK(a02yyuw_filter_push(&f, 101) == 101);
    CHECK(a02yyuw_filter_push(&f, 99) == 101);
    CHECK(a02yyuw_filter_push(&f, 103) == 102);   /* 100 evicted */
}

static void test_filter_clamp_and_reset(void)
{
    a02yyuw_filter_t f;
    a02yyuw_filter_init(&f, 0);
    CHECK(f.window == 1);
    a02yyuw_filter_init(&f, 99);
    CHECK(f.window == A02YYUW_FILTER_MAX_WINDOW);

    a02yyuw_filter_init(&f, 3);
    a02yyuw_filter_push(&f, 1000);
    a02yyuw_filter_push(&f, 1000);
    a02yyuw_filter_reset(&f);
    CHECK(a02yyuw_filter_push(&f, 7) == 7);
}

int main(void)
{
    test_checksum();
    test_valid_frame();
    test_bad_checksum_then_valid();
    test_garbage_before_header();
    test_ff_inside_frames_back_to_back();
    test_start_mid_frame();
    test_split_across_reads();
    test_many_frames();
    test_all_ff_never_decodes();
    test_null_output_pointer();
    test_reset_drops_partial_keeps_counters();
    test_range();
    test_filter_passthrough();
    test_filter_median();
    test_filter_clamp_and_reset();

    printf("%d checks, %d failures\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
