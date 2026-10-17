#include "web_payload.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_unreserved(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

int web_payload_urlencode(char *dst, size_t size, const char *src)
{
    static const char HEX[] = "0123456789ABCDEF";
    if (dst == NULL || src == NULL || size == 0) {
        return -1;
    }
    size_t n = 0;
    for (const char *s = src; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (is_unreserved((char)c)) {
            if (n + 1 >= size) {
                return -1;
            }
            dst[n++] = (char)c;
        } else {
            if (n + 3 >= size) {
                return -1;
            }
            dst[n++] = '%';
            dst[n++] = HEX[c >> 4];
            dst[n++] = HEX[c & 0x0F];
        }
    }
    dst[n] = '\0';
    return (int)n;
}

int web_payload_device_id(char *dst, size_t size, const uint8_t mac[6])
{
    if (dst == NULL || mac == NULL || size < WEB_PAYLOAD_DEVICE_LEN) {
        return -1;
    }
    return snprintf(dst, size, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
                    mac[4], mac[5]);
}

/* ---- bounded appender ---- */

typedef struct {
    char *buf;
    size_t size;
    size_t len;
    bool overflow;
} appender_t;

static void append_fmt(appender_t *a, const char *fmt, ...)
{
    if (a->overflow) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(a->buf + a->len, a->size - a->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= a->size - a->len) {
        a->overflow = true;
        return;
    }
    a->len += (size_t)n;
}

static void append_encoded(appender_t *a, const char *name, const char *value)
{
    append_fmt(a, "%s%s=", a->len > 0 ? "&" : "", name);
    if (a->overflow) {
        return;
    }
    int n = web_payload_urlencode(a->buf + a->len, a->size - a->len, value);
    if (n < 0) {
        a->overflow = true;
        return;
    }
    a->len += (size_t)n;
}

int web_payload_form(char *dst, size_t size, const web_post_t *p)
{
    if (dst == NULL || size == 0 || p == NULL || p->key == NULL || p->device == NULL ||
        p->status == NULL || p->fw == NULL) {
        return -1;
    }
    appender_t a = {.buf = dst, .size = size};
    dst[0] = '\0';

    append_encoded(&a, "key", p->key);
    append_encoded(&a, "device", p->device);
    if (p->distance_mm >= 0) {
        append_fmt(&a, "&distance_mm=%" PRId32, p->distance_mm);
    } else {
        append_fmt(&a, "&distance_mm=");
    }
    append_encoded(&a, "status", p->status);
    append_fmt(&a, "&age_ms=%" PRId32 "&frames_ok=%" PRIu32 "&checksum_err=%" PRIu32
                   "&rssi=%d&uptime_s=%" PRIu32,
               p->age_ms, p->frames_ok, p->checksum_err, p->rssi, p->uptime_s);
    append_encoded(&a, "fw", p->fw);

    if (a.overflow) {
        dst[0] = '\0';
        return -1;
    }
    return (int)a.len;
}

/* ---- server reply ---- */

/* Pointer to the value of "name" (after the colon and blanks), or NULL. */
static const char *json_value(const char *body, const char *name)
{
    size_t n = strlen(name);
    for (const char *p = strchr(body, '"'); p != NULL; p = strchr(p + 1, '"')) {
        if (strncmp(p + 1, name, n) != 0 || p[1 + n] != '"') {
            continue;
        }
        const char *v = p + 2 + n;
        while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') {
            v++;
        }
        if (*v != ':') {
            continue;  /* the name appeared as a string value, not a key */
        }
        v++;
        while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') {
            v++;
        }
        return v;
    }
    return NULL;
}

static int64_t json_int(const char *body, const char *name)
{
    const char *v = json_value(body, name);
    if (v == NULL || *v < '0' || *v > '9') {
        return -1;
    }
    long long i = strtoll(v, NULL, 10);
    return (i < 0) ? -1 : (int64_t)i;
}

int web_payload_parse_reply(const char *body, web_reply_t *out)
{
    if (out == NULL) {
        return -1;
    }
    out->ok = false;
    out->id = -1;
    out->rows = -1;
    if (body == NULL) {
        return -1;
    }
    const char *ok = json_value(body, "ok");
    out->ok = (ok != NULL && strncmp(ok, "true", 4) == 0);
    out->id = json_int(body, "id");
    out->rows = json_int(body, "rows");
    return out->ok ? 0 : -1;
}
