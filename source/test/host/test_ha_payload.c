/*
 * Host unit tests for the Home Assistant topic/payload builders.
 * Plain C99, no ESP-IDF. Build and run with `make -C test/host run`.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ha_payload.h"

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

/* Builder returned n and produced exactly `expected`. */
#define CHECK_STR(n, buf, expected)                                              \
    do {                                                                         \
        CHECK((n) == (int)strlen(expected));                                     \
        CHECK(strcmp((buf), (expected)) == 0);                                   \
    } while (0)

static const char ID[] = "wt_a1b2c3";

#define DEVICE_JSON                                                              \
    "\"device\":{\"identifiers\":[\"wt_a1b2c3\"],\"name\":\"Water tank\","      \
    "\"model\":\"ESP32 + A02YYUW\",\"manufacturer\":\"NOSTROMO\",\"sw_version\":\"0.1.0\"}"

static const char DISTANCE_JSON[] =
    "{\"name\":\"Distance\",\"unique_id\":\"wt_a1b2c3_distance\","
    "\"state_topic\":\"watertank/wt_a1b2c3/state\","
    "\"value_template\":\"{{ value_json.distance_mm }}\","
    "\"device_class\":\"distance\",\"unit_of_measurement\":\"mm\","
    "\"state_class\":\"measurement\","
    "\"availability\":[{\"topic\":\"watertank/wt_a1b2c3/availability\"},"
    "{\"topic\":\"watertank/wt_a1b2c3/sensor\"}],"
    "\"availability_mode\":\"all\"," DEVICE_JSON "}";

static const char STATUS_JSON[] =
    "{\"name\":\"Status\",\"unique_id\":\"wt_a1b2c3_status\","
    "\"state_topic\":\"watertank/wt_a1b2c3/state\","
    "\"value_template\":\"{{ value_json.status }}\","
    "\"entity_category\":\"diagnostic\","
    "\"availability\":[{\"topic\":\"watertank/wt_a1b2c3/availability\"}],"
    DEVICE_JSON "}";

static void test_device_id(void)
{
    const uint8_t mac[6] = {0x24, 0x6F, 0x28, 0xA1, 0xB2, 0xC3};
    const uint8_t mac0[6] = {0, 0, 0, 0x00, 0x0A, 0xFF};
    char buf[HA_PAYLOAD_ID_LEN];
    char small[HA_PAYLOAD_ID_LEN - 1];

    int n = ha_payload_device_id(buf, sizeof(buf), mac);
    CHECK_STR(n, buf, "wt_a1b2c3");
    n = ha_payload_device_id(buf, sizeof(buf), mac0);
    CHECK_STR(n, buf, "wt_000aff");
    CHECK(ha_payload_device_id(small, sizeof(small), mac) == -1);
    CHECK(ha_payload_device_id(NULL, 10, mac) == -1);
    CHECK(ha_payload_device_id(buf, sizeof(buf), NULL) == -1);
}

static void test_topics(void)
{
    char buf[HA_TOPIC_MAX];
    int n = ha_payload_topic(buf, sizeof(buf), ID, HA_TOPIC_AVAILABILITY);
    CHECK_STR(n, buf, "watertank/wt_a1b2c3/availability");
    n = ha_payload_topic(buf, sizeof(buf), ID, HA_TOPIC_SENSOR);
    CHECK_STR(n, buf, "watertank/wt_a1b2c3/sensor");
    n = ha_payload_topic(buf, sizeof(buf), ID, HA_TOPIC_STATE);
    CHECK_STR(n, buf, "watertank/wt_a1b2c3/state");

    /* "watertank/wt_a1b2c3/state" is 25 characters: needs 26 bytes. */
    CHECK(ha_payload_topic(buf, 25, ID, HA_TOPIC_STATE) == -1);
    CHECK(ha_payload_topic(buf, 26, ID, HA_TOPIC_STATE) == 25);
    CHECK(ha_payload_topic(buf, sizeof(buf), "", HA_TOPIC_STATE) == -1);
    CHECK(ha_payload_topic(buf, sizeof(buf), "wt_#", HA_TOPIC_STATE) == -1);
    CHECK(ha_payload_topic(buf, sizeof(buf), ID, (ha_topic_t)7) == -1);
}

static void test_discovery_topics(void)
{
    char buf[HA_TOPIC_MAX];
    int n = ha_payload_discovery_topic(buf, sizeof(buf), "homeassistant", ID, HA_ENTITY_DISTANCE);
    CHECK_STR(n, buf, "homeassistant/sensor/wt_a1b2c3/distance/config");
    n = ha_payload_discovery_topic(buf, sizeof(buf), "homeassistant", ID, HA_ENTITY_STATUS);
    CHECK_STR(n, buf, "homeassistant/sensor/wt_a1b2c3/status/config");
    n = ha_payload_discovery_topic(buf, sizeof(buf), "ha/test", ID, HA_ENTITY_DISTANCE);
    CHECK_STR(n, buf, "ha/test/sensor/wt_a1b2c3/distance/config");

    CHECK(ha_payload_discovery_topic(buf, 10, "homeassistant", ID, HA_ENTITY_STATUS) == -1);
    CHECK(ha_payload_discovery_topic(buf, sizeof(buf), "", ID, HA_ENTITY_STATUS) == -1);
    CHECK(ha_payload_discovery_topic(buf, sizeof(buf), "ha+", ID, HA_ENTITY_STATUS) == -1);
    CHECK(ha_payload_discovery_topic(buf, sizeof(buf), NULL, ID, HA_ENTITY_STATUS) == -1);
}

static void test_state(void)
{
    char buf[HA_STATE_MAX];
    ha_state_t ok = {1234, "OK", 120, 5310, 2};
    ha_state_t none = {-1, "NO_DATA", -1, 0, 0};
    ha_state_t oor = {12, "OUT_OF_RANGE", 85, 4294967295u, 7};

    int n = ha_payload_state(buf, sizeof(buf), &ok);
    CHECK_STR(n, buf, "{\"distance_mm\":1234,\"status\":\"OK\",\"age_ms\":120,\"ok\":5310,\"cks_err\":2}");
    n = ha_payload_state(buf, sizeof(buf), &none);
    CHECK_STR(n, buf, "{\"distance_mm\":-1,\"status\":\"NO_DATA\",\"age_ms\":-1,\"ok\":0,\"cks_err\":0}");
    n = ha_payload_state(buf, sizeof(buf), &oor);
    CHECK_STR(n, buf,
              "{\"distance_mm\":12,\"status\":\"OUT_OF_RANGE\",\"age_ms\":85,"
              "\"ok\":4294967295,\"cks_err\":7}");

    /* The OK payload is 69 characters. */
    CHECK(ha_payload_state(buf, 69, &ok) == -1);
    CHECK(ha_payload_state(buf, 70, &ok) == 69);
    CHECK(ha_payload_state(buf, sizeof(buf), NULL) == -1);

    ha_state_t bad = {1, "O\"K", 0, 0, 0};
    CHECK(ha_payload_state(buf, sizeof(buf), &bad) == -1);
    bad.status = NULL;
    CHECK(ha_payload_state(buf, sizeof(buf), &bad) == -1);
}

static void test_discovery_payloads(void)
{
    char buf[HA_DISCOVERY_MAX];
    int n = ha_payload_discovery(buf, sizeof(buf), ID, HA_ENTITY_DISTANCE, "0.1.0");
    CHECK_STR(n, buf, DISTANCE_JSON);
    n = ha_payload_discovery(buf, sizeof(buf), ID, HA_ENTITY_STATUS, "0.1.0");
    CHECK_STR(n, buf, STATUS_JSON);

    /* Exact-fit boundary: length + 1 bytes are required. */
    size_t dlen = strlen(DISTANCE_JSON);
    CHECK(ha_payload_discovery(buf, dlen, ID, HA_ENTITY_DISTANCE, "0.1.0") == -1);
    CHECK(ha_payload_discovery(buf, dlen + 1, ID, HA_ENTITY_DISTANCE, "0.1.0") == (int)dlen);

    /* The longest app version (esp_app_desc_t.version is char[32]) must still fit. */
    CHECK(ha_payload_discovery(buf, sizeof(buf), ID, HA_ENTITY_DISTANCE,
                               "v0.1.0-1234-gabcdef0123-dirty12") > 0);

    CHECK(ha_payload_discovery(buf, sizeof(buf), ID, HA_ENTITY_DISTANCE, "1\"2") == -1);
    CHECK(ha_payload_discovery(buf, sizeof(buf), ID, HA_ENTITY_DISTANCE, "1\\2") == -1);
    CHECK(ha_payload_discovery(buf, sizeof(buf), ID, HA_ENTITY_DISTANCE, NULL) == -1);
    CHECK(ha_payload_discovery(buf, sizeof(buf), ID, (ha_entity_t)5, "0.1.0") == -1);
}

static void test_sensor_availability(void)
{
    CHECK(strcmp(ha_payload_sensor_availability(true), "online") == 0);
    CHECK(strcmp(ha_payload_sensor_availability(false), "offline") == 0);
}

static void test_uri_host(void)
{
    char buf[64];
    int n = ha_payload_uri_host(buf, sizeof(buf), "mqtt://192.168.1.10:1883");
    CHECK_STR(n, buf, "192.168.1.10:1883");
    n = ha_payload_uri_host(buf, sizeof(buf), "mqtt://user:secret@broker.lan:1883/path");
    CHECK_STR(n, buf, "broker.lan:1883");
    n = ha_payload_uri_host(buf, sizeof(buf), "mqtts://a@b@host");
    CHECK_STR(n, buf, "host");
    n = ha_payload_uri_host(buf, sizeof(buf), "broker.lan");
    CHECK_STR(n, buf, "broker.lan");
    CHECK(ha_payload_uri_host(buf, sizeof(buf), "mqtt://") == -1);
    CHECK(ha_payload_uri_host(buf, 5, "mqtt://broker.lan") == -1);
    CHECK(ha_payload_uri_host(buf, sizeof(buf), NULL) == -1);
}

int main(void)
{
    test_device_id();
    test_topics();
    test_discovery_topics();
    test_state();
    test_discovery_payloads();
    test_sensor_availability();
    test_uri_host();

    printf("%d checks, %d failures\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
