#include "a02yyuw_parser.h"

#include <string.h>

uint8_t a02yyuw_checksum(uint8_t data_h, uint8_t data_l)
{
    return (uint8_t)((A02YYUW_HEADER + data_h + data_l) & 0xFF);
}

bool a02yyuw_distance_in_range(uint16_t distance_mm)
{
    return distance_mm >= A02YYUW_MIN_MM && distance_mm <= A02YYUW_MAX_MM;
}

void a02yyuw_parser_init(a02yyuw_parser_t *p)
{
    memset(p, 0, sizeof(*p));
}

void a02yyuw_parser_reset(a02yyuw_parser_t *p)
{
    p->len = 0;
}

/* Drop buf[0] (a failed header) and realign on the next header byte in the window. */
static void resync(a02yyuw_parser_t *p)
{
    uint8_t i = 1;
    while (i < p->len && p->buf[i] != A02YYUW_HEADER) {
        i++;
        p->sync_skips++;
    }
    uint8_t remain = (uint8_t)(p->len - i);
    memmove(p->buf, &p->buf[i], remain);
    p->len = remain;
}

bool a02yyuw_parser_feed(a02yyuw_parser_t *p, uint8_t byte, uint16_t *distance_mm)
{
    if (p->len == 0 && byte != A02YYUW_HEADER) {
        p->sync_skips++;
        return false;
    }

    p->buf[p->len++] = byte;
    if (p->len < A02YYUW_FRAME_LEN) {
        return false;
    }

    if (a02yyuw_checksum(p->buf[1], p->buf[2]) == p->buf[3]) {
        if (distance_mm) {
            *distance_mm = (uint16_t)(((uint16_t)p->buf[1] << 8) | p->buf[2]);
        }
        p->frames_ok++;
        p->len = 0;
        return true;
    }

    p->checksum_err++;
    resync(p);
    return false;
}
