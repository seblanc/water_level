#include "a02yyuw_filter.h"

#include <string.h>

void a02yyuw_filter_init(a02yyuw_filter_t *f, uint8_t window)
{
    memset(f, 0, sizeof(*f));
    if (window == 0) {
        window = 1;
    }
    if (window > A02YYUW_FILTER_MAX_WINDOW) {
        window = A02YYUW_FILTER_MAX_WINDOW;
    }
    f->window = window;
}

void a02yyuw_filter_reset(a02yyuw_filter_t *f)
{
    f->count = 0;
    f->head = 0;
}

uint16_t a02yyuw_filter_push(a02yyuw_filter_t *f, uint16_t value)
{
    f->ring[f->head] = value;
    f->head = (uint8_t)((f->head + 1) % f->window);
    if (f->count < f->window) {
        f->count++;
    }

    /* Ring slots 0..count-1 are always the filled ones (writes start at 0). */
    uint16_t sorted[A02YYUW_FILTER_MAX_WINDOW];
    memcpy(sorted, f->ring, (size_t)f->count * sizeof(sorted[0]));
    for (uint8_t i = 1; i < f->count; i++) {
        uint16_t v = sorted[i];
        uint8_t j = i;
        while (j > 0 && sorted[j - 1] > v) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = v;
    }
    return sorted[(f->count - 1) / 2];
}
