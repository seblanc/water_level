# Water tank level — ESP32 + A02YYUW (ESP-IDF)

Stage 1 firmware: reads a DFRobot A02YYUW waterproof ultrasonic sensor over UART and
prints the distance on the USB console once per second. Stage 2 (Home Assistant) is
planned but not implemented yet. See `SYSTEM_ARCHITECTURE.md` §5.

> **Status:** source written, **not compiled, flashed or tested yet**. The authoring
> environment had no toolchain or hardware. See `VERIFICATION_MATRIX.md`.

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

## Host unit tests (parser and filter, no hardware)

```sh
make -C test/host run
```

This needs a host C compiler. The tests have not been run yet.

## Bench log checker (offline, reads a capture file)

`test/bench/check_log.py` (Python 3.7+, standard library only) checks a captured console
log against the verification-matrix criteria V-03, V-03b, V-05, V-10, V-11 and V-12.

```sh
# Capture: in `idf.py monitor`, press Ctrl+T then Ctrl+L to toggle logging to a file,
# or use any serial terminal that saves the raw text at 115200 baud.
python3 test/bench/check_log.py capture.log                    # V-03 V-05 V-10 V-12
python3 test/bench/check_log.py unplug.log --check v11
python3 test/bench/check_log.py nosensor.log --check v03b --check v12
python3 test/bench/test_check_log.py                           # self-tests (synthetic logs)
```

Exit status: `0` all PASS, `1` any FAIL, `3` INCONCLUSIVE (for example a capture that is
too short for the check). The checker and its self-tests have not been run yet.

## Layout

```
main/                      app_main, banner, 1 Hz reporter task, Kconfig
components/a02yyuw/        sensor driver (UART2 + RX task) and pure-C parser/filter
components/level_report/   report dispatcher (level_report_add_sink); Stage 1 = console sink
test/host/                 host unit tests for parser/filter
test/bench/                offline log checker for bench captures + its self-tests
```
