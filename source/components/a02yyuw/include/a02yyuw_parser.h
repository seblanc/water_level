/*
 * A02YYUW (DFRobot SEN0311) UART frame parser.
 *
 * Pure C, no ESP-IDF dependencies, so it can be unit-tested on a host
 * (see test/host/).
 *
 * Frame (9600 8N1, sensor-driven):  0xFF  DATA_H  DATA_L  SUM
 *   distance_mm = (DATA_H << 8) | DATA_L
 *   SUM         = (0xFF + DATA_H + DATA_L) & 0xFF
 *
 * Protocol constants come from general knowledge of the part, not from a
 * supplied datasheet (gap G3 in SYSTEM_ARCHITECTURE.md).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define A02YYUW_HEADER     0xFF
#define A02YYUW_FRAME_LEN  4

/* Nominal measuring range; values outside it are reported as OUT_OF_RANGE. */
#define A02YYUW_MIN_MM     30
#define A02YYUW_MAX_MM     4500

typedef struct {
    uint8_t  buf[A02YYUW_FRAME_LEN];
    uint8_t  len;           /* bytes currently held; buf[0] is a header when len > 0 */
    uint32_t frames_ok;     /* checksum-valid frames decoded */
    uint32_t checksum_err;  /* 4-byte windows starting with 0xFF that failed the checksum */
    uint32_t sync_skips;    /* bytes discarded while searching for a header */
} a02yyuw_parser_t;

/* Clear the parser state and all counters. */
void a02yyuw_parser_init(a02yyuw_parser_t *p);

/* Drop any partial frame but keep the counters (e.g. after a UART overflow). */
void a02yyuw_parser_reset(a02yyuw_parser_t *p);

/*
 * Feed one received byte. Returns true when it completes a checksum-valid
 * frame; the distance is then written to *distance_mm (may be NULL).
 *
 * On a checksum failure the parser drops only the failed header byte and
 * re-synchronises on the next 0xFF already in its window, so a 0xFF inside a
 * data/checksum byte cannot make it lose a following valid frame.
 */
bool a02yyuw_parser_feed(a02yyuw_parser_t *p, uint8_t byte, uint16_t *distance_mm);

uint8_t a02yyuw_checksum(uint8_t data_h, uint8_t data_l);

/* True if distance_mm lies within the sensor's nominal range. */
bool a02yyuw_distance_in_range(uint16_t distance_mm);

#ifdef __cplusplus
}
#endif
