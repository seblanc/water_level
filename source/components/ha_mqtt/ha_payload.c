#include "ha_payload.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define DEVICE_FMT                                                              \
    "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Water tank\","             \
    "\"model\":\"ESP32 + A02YYUW\",\"manufacturer\":\"NOSTROMO\",\"sw_version\":\"%s\"}"

static const char *const TOPIC_SUFFIX[] = {
    [HA_TOPIC_AVAILABILITY] = "availability",
    [HA_TOPIC_SENSOR] = "sensor",
    [HA_TOPIC_STATE] = "state",
};

static const char *const ENTITY_NAME[] = {
    [HA_ENTITY_DISTANCE] = "distance",
    [HA_ENTITY_STATUS] = "status",
};

/* snprintf result -> length, or -1 on error/truncation. */
static int finish(int n, size_t len)
{
    return (n < 0 || (size_t)n >= len) ? -1 : n;
}

/* Strings embedded in JSON are not escaped, so reject anything needing it. */
static bool json_safe(const char *s)
{
    if (s == NULL) {
        return false;
    }
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == '"' || c == '\\') {
            return false;
        }
    }
    return true;
}

/* Topic levels must be non-empty and contain no MQTT wildcards. */
static bool topic_safe(const char *s)
{
    return s != NULL && s[0] != '\0' && strpbrk(s, "+#") == NULL;
}

int ha_payload_device_id(char *buf, size_t len, const uint8_t mac[6])
{
    if (buf == NULL || len == 0 || mac == NULL) {
        return -1;
    }
    return finish(snprintf(buf, len, "wt_%02x%02x%02x", mac[3], mac[4], mac[5]), len);
}

int ha_payload_topic(char *buf, size_t len, const char *id, ha_topic_t topic)
{
    if (buf == NULL || len == 0 || !topic_safe(id) || (unsigned)topic > HA_TOPIC_STATE) {
        return -1;
    }
    return finish(snprintf(buf, len, HA_TOPIC_BASE "/%s/%s", id, TOPIC_SUFFIX[topic]), len);
}

int ha_payload_discovery_topic(char *buf, size_t len, const char *prefix, const char *id,
                               ha_entity_t entity)
{
    if (buf == NULL || len == 0 || !topic_safe(prefix) || !topic_safe(id) ||
        (unsigned)entity > HA_ENTITY_STATUS) {
        return -1;
    }
    return finish(snprintf(buf, len, "%s/sensor/%s/%s/config", prefix, id, ENTITY_NAME[entity]),
                  len);
}

int ha_payload_state(char *buf, size_t len, const ha_state_t *s)
{
    if (buf == NULL || len == 0 || s == NULL || !json_safe(s->status)) {
        return -1;
    }
    return finish(snprintf(buf, len,
                           "{\"distance_mm\":%" PRId32 ",\"status\":\"%s\",\"age_ms\":%" PRId32
                           ",\"ok\":%" PRIu32 ",\"cks_err\":%" PRIu32 "}",
                           s->distance_mm, s->status, s->age_ms, s->frames_ok, s->checksum_err),
                  len);
}

int ha_payload_discovery(char *buf, size_t len, const char *id, ha_entity_t entity,
                         const char *sw_version)
{
    if (buf == NULL || len == 0 || !topic_safe(id) || !json_safe(id) || !json_safe(sw_version)) {
        return -1;
    }

    int n;
    switch (entity) {
    case HA_ENTITY_DISTANCE:
        n = snprintf(buf, len,
                     "{\"name\":\"Distance\",\"unique_id\":\"%s_distance\","
                     "\"state_topic\":\"" HA_TOPIC_BASE "/%s/state\","
                     "\"value_template\":\"{{ value_json.distance_mm }}\","
                     "\"device_class\":\"distance\",\"unit_of_measurement\":\"mm\","
                     "\"state_class\":\"measurement\","
                     "\"availability\":[{\"topic\":\"" HA_TOPIC_BASE "/%s/availability\"},"
                     "{\"topic\":\"" HA_TOPIC_BASE "/%s/sensor\"}],"
                     "\"availability_mode\":\"all\"," DEVICE_FMT "}",
                     id, id, id, id, id, sw_version);
        break;
    case HA_ENTITY_STATUS:
        n = snprintf(buf, len,
                     "{\"name\":\"Status\",\"unique_id\":\"%s_status\","
                     "\"state_topic\":\"" HA_TOPIC_BASE "/%s/state\","
                     "\"value_template\":\"{{ value_json.status }}\","
                     "\"entity_category\":\"diagnostic\","
                     "\"availability\":[{\"topic\":\"" HA_TOPIC_BASE "/%s/availability\"}],"
                     DEVICE_FMT "}",
                     id, id, id, id, sw_version);
        break;
    default:
        return -1;
    }
    return finish(n, len);
}

const char *ha_payload_sensor_availability(bool has_data)
{
    return has_data ? HA_AVAIL_ONLINE : HA_AVAIL_OFFLINE;
}

int ha_payload_uri_host(char *buf, size_t len, const char *uri)
{
    if (buf == NULL || len == 0 || uri == NULL) {
        return -1;
    }
    const char *p = strstr(uri, "://");
    p = (p != NULL) ? p + 3 : uri;
    const char *end = p + strcspn(p, "/?#");

    /* Drop "user:password@" if present (the last '@' before the path). */
    for (const char *q = p; q < end; q++) {
        if (*q == '@') {
            p = q + 1;
        }
    }

    size_t n = (size_t)(end - p);
    if (n == 0 || n >= len) {
        return -1;
    }
    memcpy(buf, p, n);
    buf[n] = '\0';
    return (int)n;
}
