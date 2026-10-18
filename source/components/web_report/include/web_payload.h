/*
 * Pure-C helpers for the web report (no ESP-IDF dependency, host-testable).
 *
 * Builds the application/x-www-form-urlencoded body POSTed to the PHP
 * endpoint (web/water/post.php). Field names are the server contract: change
 * both sides together.
 */
#pragma once

#include <stdbool.h>
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

#define WEB_REPLY_ERROR_LEN 32

/*
 * Fields of the post.php reply:
 *   200 {"ok":true,"stored":true,"id":N,"rows":N,"distance_mm":N|null,"uptime_s":N,..}
 *   4xx/5xx {"ok":false,"error":"..."}
 */
typedef struct {
    bool ok;              /* "ok":true present */
    bool stored;          /* "stored":true present (server read the row back) */
    int64_t id;           /* database row id of this reading, -1 if absent */
    int64_t rows;         /* rows in the table after the insert, -1 if absent */
    int64_t distance_mm;  /* stored distance, -1 if null or absent */
    int64_t uptime_s;     /* stored uptime, -1 if absent */
    /* "error" string, only [A-Za-z0-9 ._-] kept, "" if absent */
    char error[WEB_REPLY_ERROR_LEN];
} web_reply_t;

/*
 * Minimal scan of the server's JSON reply (flat object written by
 * json_encode; no nesting is needed). Returns 0 when "ok":true was found,
 * -1 otherwise (also for NULL arguments). out is always filled when non-NULL.
 */
int web_payload_parse_reply(const char *body, web_reply_t *out);

typedef enum {
    WEB_STORE_CONFIRMED,    /* row read back by the server, echo matches what was sent */
    WEB_STORE_UNCONFIRMED,  /* "ok":true but no readback echo (pre-run-20 post.php) */
    WEB_STORE_MISMATCH,     /* echo differs from what was sent: not counted as delivered */
    WEB_STORE_FAILED,       /* no "ok":true (or NULL arguments) */
} web_store_t;

/* Classify a parsed reply against the post that produced it. */
web_store_t web_payload_check_stored(const web_reply_t *reply, const web_post_t *sent);

/*
 * Log-safe summary of a reply body that is not post.php's JSON (hosting error
 * page, PHP error text): HTML tags, <style>/<script> content and &entities;
 * removed, characters outside [A-Za-z0-9 .,:;/()[]'_-] turned into blanks
 * (never '=', so no bench-checker key=value token), blank runs collapsed,
 * trimmed, cut to size - 1. Returns the length written (0 for an empty or
 * NULL body), or -1 if dst is NULL or size is 0.
 */
int web_payload_body_excerpt(char *dst, size_t size, const char *body);

#ifdef __cplusplus
}
#endif
