/*
 * Running median filter over the last N distance samples.
 *
 * Pure C, no ESP-IDF dependencies (host-testable).
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define A02YYUW_FILTER_MAX_WINDOW 15

typedef struct {
    uint16_t ring[A02YYUW_FILTER_MAX_WINDOW];
    uint8_t  window;  /* 1 = pass-through */
    uint8_t  count;   /* samples held, <= window */
    uint8_t  head;    /* next write index */
} a02yyuw_filter_t;

/* window is clamped to 1..A02YYUW_FILTER_MAX_WINDOW. */
void a02yyuw_filter_init(a02yyuw_filter_t *f, uint8_t window);

/* Forget all samples (e.g. after the sensor was lost). */
void a02yyuw_filter_reset(a02yyuw_filter_t *f);

/*
 * Add a sample and return the median of the samples held. With an even
 * count (during warm-up) the lower of the two middle values is returned.
 */
uint16_t a02yyuw_filter_push(a02yyuw_filter_t *f, uint16_t value);

#ifdef __cplusplus
}
#endif
