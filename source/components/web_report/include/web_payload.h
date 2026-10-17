/*
 * Pure-C helpers for the web report (no ESP-IDF dependency, host-testable).
 *
 * Builds the application/x-www-form-urlencoded body POSTed to the PHP
 * endpoint (web/water/post.php). Field names are the server contract: change
 * both sides together.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "aabbccddeeff" + NUL */
#define WEB_PAYLOAD_DEVICE_LEN 13
/* Worst case: 64-char key and 32-char version fully %-encoded, plus numbers. */
#define WEB_PAYLOAD_FORM_MAX   640

typedef struct {
    const char *key;          /* API key */
    const char *device;       /* web_payload_device_id() output */
    int32_t  distance_mm;     /* -1 when status is NO_DATA (sent as empty) */
    const char *status;       /* "OK", "NO_DATA", "OUT_OF_RANGE" */
    int32_t  age_ms;
    uint32_t frames_ok;
    uint32_t checksum_err;
    int      rssi;            /* dBm, 0 = unknown */
    uint32_t uptime_s;
    const char *fw;           /* app version string */
} web_post_t;

/*
 * Percent-encode src (RFC 3986 unreserved characters kept, others as %XX)
 * into dst. Returns the length written (without NUL), or -1 if dst is too
 * small or an argument is NULL.
 */
int web_payload_urlencode(char *dst, size_t size, const char *src);

/* Lower-case hex MAC, no separators. Returns 12, or -1 if size is too small. */
int web_payload_device_id(char *dst, size_t size, const uint8_t mac[6]);

/*
 * Build the form body:
 *   key=..&device=..&distance_mm=..&status=..&age_ms=..&frames_ok=..
 *   &checksum_err=..&rssi=..&uptime_s=..&fw=..
 * distance_mm is left empty when negative. Returns the length (without NUL),
 * or -1 on truncation or NULL arguments.
 */
int web_payload_form(char *dst, size_t size, const web_post_t *p);

#ifdef __cplusplus
}
#endif
