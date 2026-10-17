/*
 * Home Assistant MQTT topic and JSON payload builders (SYSTEM_ARCHITECTURE.md §5.4).
 *
 * Pure C99, no ESP-IDF headers, so it can be unit-tested on the host
 * (test/host/test_ha_payload.c). Every builder writes a NUL-terminated
 * string into the caller's buffer and returns its length (without the NUL),
 * or -1 if the buffer is too small or an argument is invalid. On -1 the
 * buffer contents are unspecified and must not be published.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HA_TOPIC_BASE      "watertank"
#define HA_AVAIL_ONLINE    "online"
#define HA_AVAIL_OFFLINE   "offline"

#define HA_PAYLOAD_ID_LEN  10   /* "wt_" + 6 hex digits + NUL */
#define HA_TOPIC_MAX       128
#define HA_STATE_MAX       160
#define HA_DISCOVERY_MAX   768

typedef enum {
    HA_TOPIC_AVAILABILITY = 0,  /* watertank/<id>/availability (birth/LWT) */
    HA_TOPIC_SENSOR,            /* watertank/<id>/sensor (sensor has data) */
    HA_TOPIC_STATE,             /* watertank/<id>/state (JSON reading) */
} ha_topic_t;

typedef enum {
    HA_ENTITY_DISTANCE = 0,
    HA_ENTITY_STATUS,
} ha_entity_t;

/* Fields of the state JSON. Kept independent of level_reading_t (IDF-free). */
typedef struct {
    int32_t     distance_mm;   /* -1 when status is NO_DATA */
    const char *status;        /* "OK" / "NO_DATA" / "OUT_OF_RANGE" */
    int32_t     age_ms;
    uint32_t    frames_ok;
    uint32_t    checksum_err;
} ha_state_t;

/* "wt_" + the last 3 bytes of mac[6] in lowercase hex, e.g. "wt_a1b2c3". */
int ha_payload_device_id(char *buf, size_t len, const uint8_t mac[6]);

/* watertank/<id>/availability | sensor | state */
int ha_payload_topic(char *buf, size_t len, const char *id, ha_topic_t topic);

/* <prefix>/sensor/<id>/distance/config | status/config */
int ha_payload_discovery_topic(char *buf, size_t len, const char *prefix, const char *id,
                               ha_entity_t entity);

/* {"distance_mm":1234,"status":"OK","age_ms":120,"ok":5310,"cks_err":2} */
int ha_payload_state(char *buf, size_t len, const ha_state_t *s);

/* HA MQTT discovery config for the distance or status entity. */
int ha_payload_discovery(char *buf, size_t len, const char *id, ha_entity_t entity,
                         const char *sw_version);

/* Payload of the watertank/<id>/sensor topic. */
const char *ha_payload_sensor_availability(bool has_data);

/*
 * Host[:port] part of a broker URI, for logging without credentials:
 * "mqtt://user:pw@192.168.1.10:1883/x" -> "192.168.1.10:1883".
 */
int ha_payload_uri_host(char *buf, size_t len, const char *uri);

#ifdef __cplusplus
}
#endif
