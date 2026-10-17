# Water tank level — ESP32 + A02YYUW (ESP-IDF)

Stage 1 firmware: reads a DFRobot A02YYUW waterproof ultrasonic sensor over UART and
prints the distance on the USB console once per second. Stage 2 publishes the reading to
Home Assistant over MQTT (discovery), once Wi-Fi and broker settings are provisioned.
See `SYSTEM_ARCHITECTURE.md` §5 and the "Home Assistant" section below.

> **Status:** Stage 1 and Stage 2 are **compiled in CI only**. The latest is GitHub Actions
> Build #9 for commit `c804544`, artifact `nostromo-firmware-9`, with HA compiled in and
> off until provisioned. Build #8 (`nostromo-firmware-8`) is the Stage 1-only baseline.
> Nothing has been flashed or tested on hardware, and the host tests have not been run.
> See `VERIFICATION_MATRIX.md`.

## Wiring (default, configurable)

| A02YYUW wire | ESP32 pin | Note |
|--------------|-----------|------|
| Red (VCC)    | 3V3       | per project spec |
| Black (GND)  | GND       | |
| White (TX)   | GPIO16    | ESP32 UART2 RX |
| Yellow (RX)  | GPIO17    | ESP32 UART2 TX (idles HIGH → sensor "processed" output mode) |

Wire colours come from general knowledge of the part. Check them against your cable.
If you get `status=NO_DATA` and `ok=0` while the sensor is powered, the TX/RX pins are
probably swapped. Fix this in `idf.py menuconfig` → *Water Tank Sensor* (or swap the
wires). On an **ESP32-WROVER** module, GPIO16/17 are used by PSRAM, so pick other
pins (e.g. GPIO25/26).

> **Caution (swapped wiring):** GPIO17 is a push-pull UART TX that idles HIGH. If the
> sensor's TX (white) wire ends up on GPIO17, both outputs drive the same line. Check the
> wiring before power-up. While diagnosing pin direction, set `WT_SENSOR_TX_GPIO=-1`:
> the firmware never transmits, and a floating sensor RX also selects the "processed"
> mode. A series resistor (~1 kΩ) in the GPIO17 → sensor RX line also limits the current
> **[recommendation, not bench-tested]**.

## Build, flash, monitor

Requires ESP-IDF v5.0 or later. The code targets the v5.x APIs and has never been built
with any version. The sensor component picks `driver` (< v5.3) or `esp_driver_uart`/
`esp_driver_gpio` (≥ v5.3) automatically. v6.x is untested.

```sh
. $IDF_PATH/export.sh
idf.py set-target esp32
idf.py build
idf.py -p /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 flash monitor
```

If flashing is unreliable over the CH340, add `-b 115200`. Some clone boards need BOOT
held down while esptool connects. Exit the monitor with `Ctrl+]`.

## Expected console output

```
I (xxx) wt: === water_tank fw 0.1.0 (built ...) ===
I (xxx) wt: idf=v5.x chip_rev=v3.0 cores=2 flash=4MB
I (xxx) wt: sensor=A02YYUW uart=2 rx_gpio=16 tx_gpio=17 baud=9600
I (xxx) wt: report_period_ms=1000 stale_ms=1000 median_window=1
I (xxx) a02yyuw: UART2 @9600 8N1, rx=GPIO16 tx=GPIO17, median_window=1
I (xxx) a02yyuw: sensor locked after 9 bytes; discarded during acquisition: 0 checksum errors, 1 sync skips, 0 line errors
I (xxx) wt: distance_mm=1234 status=OK age_ms=42 ok=10 cks_err=0 sync_skip=0 uart_ovf=0 rx_bytes=40 line_err=0
...
W (xxx) wt: sensor data lost: no valid frame for > 1000 ms (check wiring, RX/TX pins)
I (xxx) wt: distance_mm=-1 status=NO_DATA age_ms=1530 ok=118 cks_err=0 sync_skip=0 uart_ovf=0 rx_bytes=472 line_err=0
```

This sample shows the intended format. It was not captured from hardware.

| Field | Meaning |
|-------|---------|
| `distance_mm` | Newest (or median-filtered) valid distance; `-1` when `NO_DATA` |
| `status` | `OK`, `NO_DATA` (no valid frame within `stale_ms`), `OUT_OF_RANGE` (< 30 mm or > 4500 mm) |
| `age_ms` | Age of the newest valid frame; `-1` if none was ever received |
| `ok` / `cks_err` / `sync_skip` / `uart_ovf` | Cumulative counters: valid frames, checksum failures, bytes skipped while resyncing, UART overflows |
| `rx_bytes` / `line_err` | Cumulative raw bytes received on the sensor UART, and UART framing/parity errors |

Counter origin: `ok`, `rx_bytes` and `uart_ovf` count from boot. `cks_err`, `sync_skip` and
`line_err` count from the first valid frame (the `sensor locked` line). Joining the
sensor's stream mid-frame at boot is not a link fault, so those counts are reset there.
They are logged once in the `sensor locked` line, so nothing is hidden. Before the first
valid frame, these three counters show the raw counts.

### Troubleshooting with the counters

| Symptom | Likely cause |
|---------|--------------|
| `ok=0 rx_bytes=0` | Nothing arrives: sensor not powered, TX/RX swapped (gap G1, see the caution above), or wrong pin/module (G2) |
| `ok=0 rx_bytes` increasing | Bytes arrive but no valid frames: wrong baud rate or protocol, or noise |
| `line_err` increasing | Electrical problem (noise, missing common GND) or baud mismatch |
| `cks_err` high compared to `ok` | Noisy line, or the parser is resyncing often |
| Boot loop with `RX GPIO.. is not a usable input` / `TX GPIO..` | Invalid pin in menuconfig (flash pins 6–11, or non-existent GPIO) |

## Configuration (`idf.py menuconfig` → Water Tank Sensor)

| Option | Default |
|--------|---------|
| `WT_SENSOR_UART_NUM` | 2 |
| `WT_SENSOR_RX_GPIO` / `WT_SENSOR_TX_GPIO` | 16 / 17 (`-1` = TX unused) |
| `WT_REPORT_PERIOD_MS` | 1000 |
| `WT_SENSOR_STALE_MS` | 1000 |
| `WT_MEDIAN_FILTER` / `WT_MEDIAN_WINDOW` | off / 5 |
| *Wi-Fi* → `WT_WIFI_SSID` / `WT_WIFI_PASSWORD` | `weefee` / (set) in `sdkconfig.defaults` (NVS values take precedence) |
| *Web report* → `WT_WEB_ENABLE` | on |
| `WT_WEB_POST_URL` | `https://randoresto.ch/water/post.php` |
| `WT_WEB_API_KEY` | set in `sdkconfig.defaults`, must equal `api_key` in `web/water/config.php` |
| `WT_WEB_POST_PERIOD_MS` | 60000 |
| *Home Assistant* → `WT_HA_ENABLE` | on (runtime-gated, see below) |
| `WT_MQTT_BROKER_URI` / `WT_MQTT_USERNAME` / `WT_MQTT_PASSWORD` | empty (NVS values take precedence) |

## Web report (PHP server at https://randoresto.ch/water)

Once Wi-Fi is up, the firmware POSTs the newest reading to `WT_WEB_POST_URL` every
`WT_WEB_POST_PERIOD_MS` (60 s). It also posts at once when the sensor goes to or from
`NO_DATA`. After a failed post it waits 15 s before trying again. Nothing is queued while
offline. The 1 Hz console lines are not affected.

Server side: copy `web/water/` to the web host. Setup, database and page details are in
`web/water/README.md`. The page shows the level in % using `empty_cm` = 30 and
`full_cm` = 5 from `config.php`. These values are provisional. Change them on the server,
no reflash needed.

Console lines: `web: enabled device=<mac> url=… key=set period_ms=60000`, then
`web: post ok (http 200)`. On errors you see `web: post failed: <err>` (network/TLS) or
`web: post rejected: http <code>`: 401 = API key mismatch, 404 = wrong URL,
5xx = server/database. The key and the Wi-Fi password are never logged.

**Credentials in `sdkconfig.defaults`:** at the operator's request, the Wi-Fi SSID and
password and the web API key are in `sdkconfig.defaults` so that CI artifacts work
without provisioning. Anyone who can read the repository can read them. If the repository
is public, move them to NVS (`tools/nvs_config.example.csv`, keys `wifi_ssid`, `wifi_pass`,
`web_url`, `web_key`) and clear them in `sdkconfig.defaults`. Also change the key in
`config.php`.
| `WT_HA_PUBLISH_PERIOD_MS` | 10000 |
| `WT_HA_DISCOVERY_PREFIX` | `homeassistant` |

## Home Assistant (Stage 2, MQTT discovery)

Needs an MQTT broker reachable from the ESP32 (e.g. the HA Mosquitto add-on) with the HA
MQTT integration enabled. This setup is assumed, not confirmed (gap G5).

**Default behaviour:** without settings the firmware prints one extra line,
`I (xxx) ha: disabled (no wifi_ssid/mqtt_uri configured)`, and otherwise behaves exactly
like Stage 1. Wi-Fi is not started.

### Provisioning (works with CI artifacts; settings survive app reflashes)

1. `cp tools/nvs_config.example.csv nvs_config.csv` and fill in your values
   (`nvs_config.csv` is git-ignored). Any key can be left out; the menuconfig value is used
   instead.
2. `python -m esp_idf_nvs_partition_gen generate nvs_config.csv nvs.bin 0x6000`
   (pip package `esp-idf-nvs-partition-gen`, or
   `$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py`).
3. `python -m esptool --chip esp32 -p /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 write_flash 0x9000 nvs.bin`

0x9000/0x6000 is the IDF default `nvs` partition; confirm it in the build's partition
table output (`idf.py partition-table`). Local builds can use `idf.py menuconfig` →
*Water Tank Sensor → Home Assistant* instead; the values are then in the git-ignored
`sdkconfig`. Do not put MQTT credentials into `sdkconfig.defaults`.

Stage 2 uses the larger single-app partition table (1.5 MB app). Flash a Stage 2 image
together with its partition table (`idf.py flash` or `write_flash @flash_args`).

### What Home Assistant shows

The device appears as **Water tank** with two entities: *Distance* (mm) and a diagnostic
*Status* (`OK` / `NO_DATA` / `OUT_OF_RANGE`). Distance becomes *unavailable* when the
device is offline or the sensor has no data. Topics (`<id>` = `wt_` + last 3 MAC bytes):

| Topic | Content |
|-------|---------|
| `watertank/<id>/state` | `{"distance_mm":1234,"status":"OK","age_ms":120,"ok":5310,"cks_err":2}`, at most every `WT_HA_PUBLISH_PERIOD_MS`, immediately on a NO_DATA transition |
| `watertank/<id>/availability` | `online` / `offline` (retained, LWT) |
| `watertank/<id>/sensor` | `online` / `offline` (retained, sensor has data) |
| `homeassistant/sensor/<id>/{distance,status}/config` | Discovery (retained) |

Console lines with HA configured: `ha: enabled id=… ssid=… broker=host:port user=…
period_ms=…`, then `ha: wifi connected ip=…`, `ha: mqtt connected`, `ha: discovery
published …`. Passwords are never logged. The 1 Hz `wt: distance_mm=…` lines are unchanged.

### Notes
- **Security:** credentials are stored in plain text in flash (no flash encryption) and
  MQTT is unencrypted (`mqtt://`). Fine for a trusted home LAN only.
- **NVS erase:** after an incompatible IDF upgrade or a corrupted NVS partition the
  firmware erases NVS at boot. The settings are then gone, HA falls back to *disabled*,
  and you must repeat the provisioning steps.
- Wi-Fi TX peaks can sag the shared 3V3 rail. Watch for brownout resets or rising
  `cks_err` after enabling HA.

## Host unit tests (parser and filter, no hardware)

```sh
make -C test/host run
```

This needs a host C compiler. The tests have not been run yet.

## Bench log checker (offline, reads a capture file)

`test/bench/check_log.py` (Python 3.7+, standard library only) checks a captured console
log against the verification-matrix criteria V-03, V-03b, V-05, V-10, V-11, V-12 and V-15.

```sh
# Capture: in `idf.py monitor`, press Ctrl+T then Ctrl+L to toggle logging to a file,
# or use any serial terminal that saves the raw text at 115200 baud.
python3 test/bench/check_log.py capture.log                    # V-03 V-05 V-10 V-12
python3 test/bench/check_log.py unplug.log --check v11
python3 test/bench/check_log.py nosensor.log --check v03b --check v12
python3 test/bench/check_log.py target30cm.log --check v15     # verification spec rev 3
python3 test/bench/test_check_log.py                           # self-tests (synthetic logs)
```

**V-15 (verification spec rev 3):** point the sensor at a flat, rigid target about 30 cm
away, square to the beam, with nothing else in the beam cone. A water surface 10–60 cm
away also works. HA must not be provisioned. Open the monitor, which resets the board,
and capture at least 20 s from the reset. Then run `--check v15`. It passes when all of
these hold:
- exactly one banner, with the expected pins;
- no crash text and no `E` line from any tag;
- the first `OK` comes within 3 s of the banner;
- at least 10 fresh `OK` readings;
- every line from the first `OK` onward is `OK` at 100–600 mm;
- `cks_err`, `line_err` and `uart_ovf` are 0 on every line.

One `NO_DATA` line and one `sensor data lost` warning before the first `OK` are tolerated.

Exit status: `0` all PASS, `1` any FAIL, `3` INCONCLUSIVE (for example a capture that is
too short for the check). The checker and its self-tests have not been run yet.

## Layout

```
main/                      app_main, banner, 1 Hz reporter task, Kconfig
components/a02yyuw/        sensor driver (UART2 + RX task) and pure-C parser/filter
components/level_report/   report dispatcher (level_report_add_sink); Stage 1 = console sink
components/wt_wifi/        shared Wi-Fi station (reconnect with backoff)
components/ha_mqtt/        Stage 2: MQTT + HA discovery sink; pure-C payload builders
components/web_report/     HTTPS POST sink for web/water/post.php; pure-C form builder
web/water/                 PHP endpoint, SQLite/MySQL storage and level web page
tools/                     nvs_config.example.csv (provisioning template)
test/host/                 host unit tests for parser/filter, HA payloads and web form
test/bench/                offline log checker for bench captures + its self-tests
```
