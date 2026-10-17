# SYSTEM ARCHITECTURE — Water Tank Level (project 1a95ca931d72)

Last updated: run #6, ARCHITECTURE phase (attempt 1), 2026-10-17.

**Evidence state.** The Stage 1 firmware **has been compiled in CI** (GitHub Actions, `espressif/idf:v5.4.2`).
Build #5 compiled and linked it, and Build #8 (`43e0382`) completed and uploaded artifact
`nostromo-firmware-8`. **It has never been flashed, run, or measured on hardware**, and the
host tests have never been executed. The partial Stage 2 sources added by commit `7bac3ea`
(run #4 implementation, recorded as *failed*) **have never been compiled by anything**:
no builder result exists for any commit after `43e0382`, and `components/ha_mqtt/` has no
`CMakeLists.txt` yet (§5.10). No agent phase has had a shell, a toolchain, or bench access. Hardware facts below
either come from the project specs or are marked **[ASSUMPTION]** or **[DATASHEET-RECALL]**.
The second marker means general knowledge of the part, not checked against a supplied
document (no reference documents were provided).

### Status history (condensed)

| Run / phase | Outcome |
|-------------|---------|
| #1 arch + impl | Architecture and Stage 1 source written |
| #2 arch / impl / review | Docs aligned with the code (§7, §8). Fixes F-03/F-04/F-05/F-12, F-13 documented. Offline log checker added (§7.4) |
| #3 arch → impl (attempts 1–8) | CI integration. Build #1 blocked by a dirty worktree (F-15). #2/#3 blocked by a push 403 (F-17). #4 blocked by the missing `workflow` token scope (F-18). #5–#7 compiled and linked, then failed in packaging on git "dubious ownership" (F-19). `level_report` sink fan-out added (F-08) |
| #3 review (attempt 9) | Operator workflow fix `a45bcc0`. **Build #8 completed**, artifact uploaded. No source defects found |
| #4 arch | No new builder result (Build #8 is still the latest). Read-only check of `../.github/workflows/nostromo-build.yml` settles the CI-artifact layout and the flashing procedure (§4.1, G13 closed). **Stage 2 (Home Assistant over MQTT) design worked out to implementable detail (§5)**, with runtime gating so the Stage 1 behaviour is unchanged when HA is not configured. Plan for the next implementation phase: DEVELOPMENT_PLAN.md M6a |
| #4 impl (attempt 2, commit `7bac3ea`, recorded **failed**) | M6a steps 1–4 written: `ha_payload.{h,c}`, `test_ha_payload.c` + Makefile target, `ha_config.c`, `ha_mqtt.c`, `ha_mqtt.h`, `ha_mqtt_priv.h`. Steps 5–10 (CMake, `main` hook, Kconfig, `sdkconfig.defaults`, provisioning template, README, checker case) **not written**. The reason for the failure is not recorded in the tree (G20) |
| #5 arch (commit `bba5d98`, recorded **failed**) | No new builder result. Static review of the partial Stage 2 code (§5.10, F-22…F-30). Build-gating design for the remaining steps settled (§5.2, §5.10.2). M6a re-planned as the *remaining* steps 5–10 plus fixes. The doc updates are in the tree even though the phase was recorded as failed; the reason is not recorded (G22) |
| **#6 arch (this phase)** | No new builder result (Build #8 is still the latest); the source tree is unchanged since `7bac3ea`. Second read-only pass over the never-compiled Stage 2 code, this time against the IDF v5.4.2 API and the `-Werror=all` flags (§5.10.4): **no new compile blocker found**. One new runtime finding (F-31, console noise from esp-mqtt during broker outages). The M6a plan is confirmed unchanged |

Earlier per-attempt detail is preserved in git history (commits `65c6ad9` … `bacd2c8`).

---

## 1. Purpose and scope

| Stage | Goal | Source | State |
|-------|------|--------|-------|
| Stage 1 | Read an A02YYUW ultrasonic sensor over UART and print the distance on the UART console **once per second** | PURPOSE rev 1, TELEMETRY rev 1 | Written; **CI-compiled** (Builds #5–#8); not run on hardware |
| Stage 2 | Send the level data to the user's Home Assistant | PURPOSE rev 1 ("Then we will implement…") | **Design complete (§5)**. Component code partly written (`7bac3ea`, §5.10) but **not wired into the build and never compiled**. Remaining work: M6a steps 5–10 |

## 2. Hardware context

### 2.1 Target
- ESP32 dev board, ESP-IDF framework, chip target `esp32`.
- Bench "Simple UART": a WCH CH340 USB bridge (VID `1a86`,
  `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0`) is used for both flashing and logs
  at 115200 baud. That matches the usual ESP32-DevKitC / "DevKit V1" clone, which has
  DTR/RTS auto-reset/boot circuitry.
- **[ASSUMPTION]** The module is an ESP32-WROOM-32 (no PSRAM). On ESP32-WROVER modules,
  GPIO16/GPIO17 are wired to the PSRAM and **cannot** be used for the sensor (gap G2).
- **[ASSUMPTION]** 4 MB flash and a 40 MHz crystal (IDF defaults for the `esp32` target).

### 2.2 Sensor: DFRobot A02YYUW (SEN0311)
**[DATASHEET-RECALL]**: check against the vendor wiki before trusting these values.

| Parameter | Value |
|-----------|-------|
| Supply | 3.3 V – 5 V (project spec: board 3V3 rail) |
| Interface | UART TTL, **9600 baud, 8N1**, sensor-driven (streams without being asked) |
| Wires | Red = VCC, Black = GND, Yellow = RX (mode control in), White = TX (data out). Check against the actual cable |
| Frame | 4 bytes: `0xFF`, `DATA_H`, `DATA_L`, `SUM` |
| Distance | `(DATA_H << 8) \| DATA_L`, **millimetres** |
| Checksum | `SUM = (0xFF + DATA_H + DATA_L) & 0xFF` |
| Range | ~30 mm (blind zone) to ~4500 mm |
| Output mode (sensor RX) | Floating/HIGH → "processed" value, ~100–300 ms period; LOW → "real-time" value, ~100 ms |
| Current | ≤ ~8 mA |

With a 3.3 V supply the sensor TX is at 3.3 V logic, so it connects straight to an ESP32
GPIO with no level shifting. Range or stability may be somewhat lower at 3.3 V than at 5 V
**[ASSUMPTION]** (gap G7).

### 2.3 Pin map

| ESP32 pin | Direction | Connected to | Function |
|-----------|-----------|--------------|----------|
| GPIO16 | IN (UART2 RXD, pull-up) | Sensor TX (white) | Sensor data frames |
| GPIO17 | OUT (UART2 TXD, idles HIGH, never written) | Sensor RX (yellow) | Holds sensor in processed mode |
| 3V3 | PWR | Sensor VCC (red) | Sensor supply |
| GND | PWR | Sensor GND (black) | Common ground |
| GPIO1 / GPIO3 | UART0 TX/RX | CH340 bridge | Console logs @115200 + flashing |
| EN / GPIO0 | — | CH340 DTR/RTS (board circuit) | Auto-reset / download mode |

**[ASSUMPTION — gap G1]** The spec says only "GPIO17 and GPIO16". The map above follows the
ESP32 UART2 convention (RX2 = GPIO16, TX2 = GPIO17). Both pins are Kconfig options, so a
reversed wiring can be fixed in `menuconfig` without code changes. Correct operation does
not depend on GPIO17. If the wiring is swapped, GPIO17 drives against the sensor output
(F-13). Inspect the wiring before power-up.

GPIO16/17 are not strapping pins and are not flash pins on WROOM-32, so they are safe at
boot. Stage 2 adds no pins (Wi-Fi is on-chip).

## 3. Software architecture

### 3.1 Platform
- ESP-IDF **v5.4.2** in CI (G4 resolved for CI). The firmware needs v5.0 or later (§7.3).
  The < v5.3 branch of the F-12 driver selection has never been built, and v6.x is untested.
- FreeRTOS (bundled), single app image, no OTA.
- Partition table: currently the IDF default "single factory app" (1 MB app). **Stage 2
  changes it to "single factory app, large" (1.5 MB app)** because of the Wi-Fi/MQTT image
  size (§5.8).

### 3.2 Project layout (as implemented; Stage 2 additions marked `+` = written in `7bac3ea`, `++` = still to write)

```
source/
├── CMakeLists.txt                 # project(water_tank), PROJECT_VER 0.1.0
├── sdkconfig.defaults             # esp32, 4 MB flash, console UART0 @115200, INFO logs, WT_* defaults
├── main/
│   ├── CMakeLists.txt             # REQUIRES a02yyuw level_report nvs_flash esp_app_format spi_flash (++ ha_mqtt)
│   ├── Kconfig.projbuild          # menu "Water Tank Sensor" (WT_* options) (++ "Home Assistant" submenu)
│   └── main.c                     # app_main, banner, NVS init, reporter task (fixed rate) (++ ha_mqtt_start)
├── components/
│   ├── a02yyuw/                   # REQUIRES driver (< v5.3) | esp_driver_uart (+ PRIV esp_driver_gpio) (≥ v5.3); PRIV esp_timer
│   │   ├── include/a02yyuw.h          # driver API (config, sample, stats)
│   │   ├── include/a02yyuw_parser.h   # pure-C frame parser API + protocol constants
│   │   ├── include/a02yyuw_filter.h   # pure-C running median API
│   │   ├── a02yyuw.c                  # UART install, event-driven RX task, latest sample
│   │   ├── a02yyuw_parser.c
│   │   └── a02yyuw_filter.c
│   ├── level_report/              # sink dispatcher; Stage 1 = console sink
│   │   ├── include/level_report.h     # reading type, sink type, dispatcher API
│   │   ├── level_report.c             # sink registry + fan-out, status strings
│   │   ├── level_report_priv.h        # component-internal: console sink prototype
│   │   └── level_report_uart.c        # console sink (§8 report line)
│   └── ha_mqtt/                   # Stage 2 (§5)
│       ├── CMakeLists.txt             # ++ MISSING: without it IDF does not build the component (§5.10.2)
│       ├── include/ha_mqtt.h          # + ha_mqtt_start()
│       ├── include/ha_payload.h       # + pure-C builder API, buffer-size constants
│       ├── ha_mqtt_priv.h             # + ha_config_t, ha_config_load()
│       ├── ha_config.c                # + NVS → Kconfig fallback settings load
│       ├── ha_payload.c               # + pure-C JSON/topic builders (host-testable)
│       └── ha_mqtt.c                  # + Wi-Fi STA, esp-mqtt client, sink, publisher task
├── tools/
│   └── nvs_config.example.csv     # ++ NVS provisioning template, placeholders only (§5.3)
└── test/
    ├── host/                      # host unit tests (cc, no IDF): test_parser.c, + test_ha_payload.c, + Makefile target
    └── bench/                     # offline log checker (Python 3 stdlib): check_log.py, test_check_log.py (++ `ha:` case)
```

### 3.3 Module responsibilities (Stage 1, as implemented)

**`a02yyuw_parser`** (pure C, no IDF headers, host-testable)
- Fills a 4-byte window that always starts with `0xFF`. Non-`0xFF` bytes seen while the
  window is empty are counted in `sync_skips`.
- Full window: if the checksum is valid, emit `distance_mm` and count it in `frames_ok`.
  If it is invalid, count `checksum_err` and **drop only the failed header byte**, then
  realign on the next `0xFF` already in the window. A `0xFF` appearing as DATA_H, DATA_L
  or SUM therefore cannot make the parser lose the next valid frame.
- `a02yyuw_parser_reset()` drops a partial frame but keeps the counters (used after a UART
  overflow).
- `a02yyuw_distance_in_range()`: 30–4500 mm.

**`a02yyuw_filter`** (pure C)
- Running median over the last N samples (N clamped to 1..15). N = 1 passes samples
  through unchanged. During warm-up with an even count it returns the lower of the two
  middle values.

**`a02yyuw` driver**
- `a02yyuw_init()` validates the configuration before touching hardware. It rejects UART0
  and out-of-range ports, an RX pin that fails `GPIO_IS_VALID_GPIO`, a TX pin (unless -1)
  that fails `GPIO_IS_VALID_OUTPUT_GPIO`, ESP32 flash pins GPIO6–11, and RX == TX. It then
  installs the UART driver (512 B RX ring buffer, no TX buffer, 16-entry event queue) at
  9600 8N1 with no flow control, sets the pins (TX = -1 leaves the pin unconfigured),
  enables the RX pull-up, and starts the RX task. On any failure it deletes the driver and
  returns an error.
- RX task (prio 5, 3 KB stack) blocks on the UART **event queue**:
  - `UART_DATA`: read in chunks of up to 64 B and feed the parser byte by byte. Each valid
    frame updates the filter and the latest sample.
  - `UART_FIFO_OVF` / `UART_BUFFER_FULL`: warning, flush input, reset the queue and the
    parser, increment `uart_overflows`.
  - `UART_FRAME_ERR` / `UART_PARITY_ERR`: increment `line_errors`.
- The latest sample and the stats are shared through a `portMUX` spinlock critical
  section. The sample is timestamped with `esp_timer_get_time()`.
- The median filter is cleared if the gap since the previous frame exceeds `stale_ms`.
- `a02yyuw_get_latest()` returns `true` only if a sample exists and is ≤ `stale_ms` old. It
  always fills `age_ms` (−1 if no frame was ever received) and sets `in_range`.

**`level_report`** (sink dispatcher)
- `level_report_init()` registers the console sink. Calling it twice returns
  `ESP_ERR_INVALID_STATE`.
- `level_report_add_sink(level_sink_fn, ctx)` registers up to `LEVEL_REPORT_MAX_SINKS` (4)
  sinks. It returns `ESP_ERR_INVALID_ARG` for a NULL function and `ESP_ERR_NO_MEM` when
  full. It is not thread-safe and must be called during start-up, before the reporter task
  starts.
- `level_report_publish(const level_reading_t *)` calls every sink in registration order
  from the reporter task. Sinks must not block.
- Console sink (`level_report_uart.c`): one `key=value` line through `ESP_LOGI` (tag
  `wt`). See §8.

**`main.c`**
- `app_main`: NVS init (erases and retries on a version or no-free-pages error), boot
  banner, `a02yyuw_init`, `level_report_init`, reporter task (prio 4, 4 KB stack).
  `app_main` then returns, which IDF allows.
- Reporter task: `xTaskDelayUntil`, fixed period `CONFIG_WT_REPORT_PERIOD_MS` with no drift.
  Each tick it builds a `level_reading_t`:
  - not fresh → `NO_DATA`, `distance_mm = -1`
  - fresh and in range → `OK`
  - fresh but out of range → `OUT_OF_RANGE` (value still reported)

  A status *transition* logs exactly one warning (into NO_DATA) or info message (out of
  NO_DATA). `prev_status` starts as `OK`, so a boot without a sensor logs the warning once
  (F-06).

### 3.4 Data flow

```
A02YYUW --9600 8N1--> GPIO16 / UART2 --ISR, 512 B ring--> event queue --> RX task (prio 5)
                                                                           │ parser → median filter
                                                                           ▼
                                                    latest sample + stats (spinlock)
                                                                           │
                                                 reporter task (prio 4, 1 Hz, xTaskDelayUntil)
                                                                           │
                                                  level_report_publish ─┬─> console sink → UART0 (Stage 1)
                                                                        └─> ha sink → depth-1 queue (Stage 2)
                                                                                  │ xQueueOverwrite, never blocks
                                                                                  ▼
                                                                  ha publisher task (prio 3) → esp-mqtt → Wi-Fi → broker → HA
```

### 3.5 Timing budget
- Sensor traffic is ~3–10 frames/s × 4 B ≈ 12–40 B/s, against ~960 B/s of capacity at
  9600 baud. The load is negligible.
- Report latency: the reported value is at most `age_ms` old (typically < 300 ms in
  processed mode **[ASSUMPTION]**).
- Worst-case NO_DATA detection after the sensor is lost is ≈ `stale_ms + report_period`,
  or ≈ 2 s with the defaults.
- Console load is a few log lines/s at most, well within 115200 baud. Stage 2 adds Wi-Fi
  and MQTT event lines, mainly at connect/reconnect (§5.6 limits the Wi-Fi driver's
  verbosity).
- Stage 2: the HA sink's cost inside the reporter task is one `xQueueOverwrite` (µs).
  Network latency never reaches the reporter task, so the 1 Hz console schedule (V-05)
  must hold with Wi-Fi active (V-13g).

### 3.6 Error handling

| Condition | Behaviour |
|-----------|-----------|
| No valid frame for > `stale_ms` | `status=NO_DATA`, `distance_mm=-1`; one warning per transition |
| Checksum failure | Counted (`cks_err`); the frame is never published; parser resyncs |
| Distance < 30 or > 4500 mm | Reported with `status=OUT_OF_RANGE` |
| UART FIFO / ring overflow | Warning, flush, parser reset, `uart_ovf` incremented |
| UART framing / parity error | `line_errors` incremented, printed as `line_err=` |
| Invalid sensor UART/GPIO configuration | `a02yyuw_init` logs the reason and returns `ESP_ERR_INVALID_ARG` → `ESP_ERROR_CHECK` abort → reboot loop with the error visible on the console |
| Driver/task init failure | `ESP_ERROR_CHECK` / `abort()` → panic → reboot (logged) |
| Task watchdog | IDF default TWDT; all tasks block in OS calls |
| Stage 2: HA not configured | One `ha: disabled (...)` line at boot; Wi-Fi is never started; Stage 1 is unaffected (§5.2) |
| Stage 2: Wi-Fi / broker down | Retried with backoff in the background; readings are dropped (no backlog); console output unaffected; **never** a reboot (§5.6) |
| Stage 2: HA init failure (e.g. out of memory) | Logged as an error, HA stays off, Stage 1 keeps running. HA is optional, so it must not abort the firmware |

## 4. Mapping: programming, telemetry, interfaces, test constraints

| Aspect | Spec / bench | Architectural decision |
|--------|--------------|------------------------|
| Build | Adapter `esp_idf`, target `esp32`, backend GitHub Actions (automatic) | Native IDF project at `source/`. CI runs `idf.py -C source set-target esp32 build` in `espressif/idf:v5.4.2`. It builds **committed files only** and uses `sdkconfig.defaults`, because `sdkconfig` is git-ignored. As a result, **CI builds never contain Wi-Fi/MQTT credentials** (§5.3) |
| Programming (CI artifact) | CI uploads `nostromo-firmware-<id>` | See §4.1. Build #8's artifact is the **Stage 1 baseline**. Retention is 30 days, so it should expire around 2026-11-16 **[ASSUMPTION: counted from the 2026-10-17 upload]** |
| Programming (local) | FLASHING rev 1: "Flash over USB". Bench: USB-Serial (CH340) at `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0` | `idf.py -p <port> flash`. esptool enters download mode automatically via DTR/RTS. Fall back to `-b 115200` if the CH340 link is unreliable. Hold BOOT on clones without auto-reset circuitry |
| Runtime telemetry | TELEMETRY rev 1: "UART logs". Bench: UART @115200 on the same port | UART0 console at 115200, `ESP_LOG` (INFO). Machine-parseable `key=value` report line at 1 Hz, tag `wt`. Contract in §8. Stage 2 keeps this line unchanged; HA is an additional sink, not a replacement |
| Port sharing | Flash and log use one port | Flashing and monitoring cannot run at the same time. Close the monitor before flashing (`idf.py flash monitor` sequences them). Opening the port may reset the board through DTR/RTS |
| Sensor interface | PURPOSE: GPIO16/17, 3V3 | UART2 at 9600 8N1. Pins and port set in Kconfig `WT_SENSOR_*` |
| Upstream interface | PURPOSE Stage 2: Home Assistant | MQTT 3.1.1 to a broker (e.g. the HA Mosquitto add-on) with HA MQTT Discovery (§5) |
| Provisioning | None specified | Runtime settings in NVS (namespace `wt_cfg`), flashed from a local CSV. Kconfig strings are the fallback for local builds (§5.3) |
| Test constraints | TEST_SETUP and ACCEPTANCE empty; bench lists no stimuli, instruments, or voltage | Verification is log observation plus manual physical checks. Parser, filter (and Stage 2 payload builders) are covered by host unit tests. Stage 2 checks also need a Wi-Fi AP, a broker and HA, none of which are on the bench (VERIFICATION_MATRIX.md) |
| Guardrails | GUARDRAILS rev 1 empty | Self-imposed: no use of strapping or flash pins; UART0 reserved for the console; sensor on 3V3 only; **no secrets in the repo** (credentials only in NVS images or a local `sdkconfig`, both git-ignored); HA failures must never reset the device or stop the console stream |

### 4.1 Flashing a CI artifact (from a read-only check of the workflow)
The workflow copies the whole `source/build` directory to `artifacts/build`. It adds
`source-commit.txt` (`git rev-parse HEAD`), `toolchain-version.txt` (`idf.py --version`) and
`SHA256SUMS`, and then uploads `artifacts/`. Build #8 uploaded 1360 files, 32.7 MB. IDF
writes `flash_args` into `build/` with paths relative to that directory, so:

```
unzip nostromo-firmware-8.zip -d fw8 && cd fw8
cat source-commit.txt                    # expect 43e03828b63949c8c75de83f00ffe9b5f43b76b1
grep -v ' \./SHA256SUMS$' SHA256SUMS | sha256sum -c --quiet
cd build
python -m esptool --chip esp32 -p /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 \
    -b 460800 --before default_reset --after hard_reset write_flash @flash_args
```

- The `grep -v` is a precaution. The shell creates `SHA256SUMS` before `find` runs, so the
  list may contain an entry for `SHA256SUMS` itself that can never match **[ASSUMPTION
  from reading the workflow; not observed]**.
- Expected layout (IDF defaults for esp32): bootloader @0x1000, partition table @0x8000,
  app @0x10000. Stage 2 changes the partition table (§5.8), so a Stage 2 image must be
  flashed **with** its own partition table. `write_flash @flash_args` does this.
- `esptool` is not inside the artifact. Use any esptool ≥ 4 (`pip install esptool`) or the
  one in a local IDF install.

## 5. Stage 2 — Home Assistant via MQTT (design, run #4)

### 5.1 Choice
**MQTT with Home Assistant MQTT Discovery**, using IDF's bundled `mqtt` (esp-mqtt) and
`esp_wifi` components.
- Discovery creates the entities in HA automatically, and the LWT gives availability
  tracking. Both run on the HA Mosquitto add-on, the most common setup.
- Rejected alternatives: ESPHome (it would replace this IDF codebase and its verified
  build pipeline) and the HA REST/webhook API (no discovery, no availability, and a
  long-lived HA token stored on the device).
- **Gap G5 is still open.** The operator has not said whether a broker exists. The design
  is runtime-gated (§5.2), so shipping it costs nothing if the operator chooses differently.

### 5.2 Build and runtime gating
- Kconfig `WT_HA_ENABLE` (bool, **default y**) controls whether the `ha_mqtt` code is
  compiled in. It defaults to `y` because CI builds only the defaults: with `n`, the Stage 2
  code would never be compiled by the only toolchain available.
- Runtime gate: `ha_mqtt_start()` loads the settings (§5.3). If the Wi-Fi SSID **or** the
  broker URI is empty, it logs `I (t) ha: disabled (no wifi_ssid/mqtt_uri configured)` and
  returns `ESP_OK`. It does not initialise `esp_netif`, start Wi-Fi or create a task. A CI
  artifact without provisioning therefore behaves like Stage 1 plus that one line.
- With `WT_HA_ENABLE=n`, `main.c` compiles the call out (`#if CONFIG_WT_HA_ENABLE`), so the
  binary contains Stage 1 code only. The Kconfig strings of §5.3 `depends on WT_HA_ENABLE`,
  so their `CONFIG_*` macros do not exist when it is `n`. The component's `CMakeLists.txt`
  therefore compiles `ha_config.c` and `ha_mqtt.c` only when `CONFIG_WT_HA_ENABLE` is set
  (§5.10.2). That `n` path is never built by CI (sdkconfig.defaults sets `y`), so it is
  covered by review only.

### 5.3 Configuration and provisioning (no secrets in the repo)

| Key (NVS `wt_cfg`) | Type | Kconfig fallback | Default | Meaning |
|--------------------|------|------------------|---------|---------|
| `wifi_ssid` | string ≤ 32 | `WT_WIFI_SSID` | "" | Wi-Fi STA SSID |
| `wifi_pass` | string ≤ 64 | `WT_WIFI_PASSWORD` | "" | WPA2/WPA3 PSK ("" = open network) |
| `mqtt_uri` | string ≤ 128 | `WT_MQTT_BROKER_URI` | "" | e.g. `mqtt://192.168.1.10:1883` |
| `mqtt_user` | string ≤ 64 | `WT_MQTT_USERNAME` | "" | Broker user (HA add-on: a HA user) |
| `mqtt_pass` | string ≤ 64 | `WT_MQTT_PASSWORD` | "" | Broker password |
| — | int | `WT_HA_PUBLISH_PERIOD_MS` | 10000 (range 1000–600000) | Minimum state publish interval |
| — | string | `WT_HA_DISCOVERY_PREFIX` | `homeassistant` | HA discovery prefix |

- Precedence: a key present in NVS wins, otherwise the Kconfig value is used. Each key is
  resolved on its own.
- CI artifacts: the operator provisions NVS once, then reflashes app images as often as
  needed. NVS survives `write_flash @flash_args`, since that only writes the bootloader,
  partition table and app.
  1. Copy `tools/nvs_config.example.csv` to a git-ignored `nvs_config.csv` and fill it in.
  2. `python -m esp_idf_nvs_partition_gen generate nvs_config.csv nvs.bin 0x6000`
     (pip package `esp-idf-nvs-partition-gen`, or `$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py`).
  3. `python -m esptool --chip esp32 -p <port> write_flash 0x9000 nvs.bin`.

  The offset and size match the IDF default `nvs` partition (0x9000, 0x6000). Both
  single-app tables keep that location **[ASSUMPTION from IDF defaults; confirm in
  `build/partition_table` output or `idf.py partition-table`]**.
- Local builds: `idf.py menuconfig` → "Water Tank Sensor → Home Assistant". The values live
  in `sdkconfig`, which is git-ignored.
- `.gitignore` additions: `nvs_config.csv`, `nvs*.bin`.
- Security limitation: without flash encryption, credentials sit in flash in plain text.
  This is acceptable for a home device on a trusted LAN, and it is documented, not
  mitigated. The log never prints passwords: the banner prints the SSID, URI host and user
  only.
- Note: `init_nvs()` erases NVS on `ESP_ERR_NVS_NEW_VERSION_FOUND` / `NO_FREE_PAGES`, which
  would wipe provisioned credentials. That is the IDF-recommended behaviour, and it only
  happens after an incompatible IDF upgrade or a corrupted partition. In that case the
  device falls back to "HA disabled". Documented in the README.

### 5.4 MQTT topics and payloads

`<id>` = `wt_` + the last 3 bytes of the Wi-Fi STA MAC in lowercase hex (e.g. `wt_a1b2c3`),
from `esp_read_mac(..., ESP_MAC_WIFI_STA)`.

| Topic | Retained | QoS | Payload |
|-------|----------|-----|---------|
| `watertank/<id>/availability` | yes | 1 | `online` (birth, published on connect) / `offline` (LWT, and on clean stop) |
| `watertank/<id>/sensor` | yes | 1 | `online` when the last reading was `OK` or `OUT_OF_RANGE`, `offline` when `NO_DATA` |
| `watertank/<id>/state` | no | 0 | `{"distance_mm":1234,"status":"OK","age_ms":120,"ok":5310,"cks_err":2}` |
| `<prefix>/sensor/<id>/distance/config` | yes | 1 | Discovery: distance entity |
| `<prefix>/sensor/<id>/status/config` | yes | 1 | Discovery: status entity (diagnostic) |

Distance discovery payload (built by `ha_payload.c`; abbreviated keys are avoided for
readability; `object_id` is deliberately omitted because recent HA releases are
deprecating it **[ASSUMPTION]**, and HA derives the entity ID from the device and entity
names):
```json
{"name":"Distance","unique_id":"<id>_distance",
 "state_topic":"watertank/<id>/state","value_template":"{{ value_json.distance_mm }}",
 "device_class":"distance","unit_of_measurement":"mm","state_class":"measurement",
 "availability":[{"topic":"watertank/<id>/availability"},{"topic":"watertank/<id>/sensor"}],
 "availability_mode":"all",
 "device":{"identifiers":["<id>"],"name":"Water tank","model":"ESP32 + A02YYUW",
           "manufacturer":"NOSTROMO","sw_version":"<app version>"}}
```
- The status entity uses the same `device`, `value_template: "{{ value_json.status }}"`,
  `entity_category: "diagnostic"`, and availability = the device topic only.
- With `availability_mode: all`, the distance entity shows **unavailable** whenever the
  device is offline **or** the sensor reports `NO_DATA`. HA never sees `-1` as a distance.
  The state payload still carries `distance_mm:-1` on `NO_DATA`, but the template is not
  shown while the entity is unavailable.
- Discovery and availability are (re)published on **every** `MQTT_EVENT_CONNECTED`, which
  covers broker restarts and HA restarts with retained topics.
- Later (G8): `level_percent` / `volume_l` from Kconfig tank geometry, as extra fields in
  the state JSON plus extra discovery entities. This is backward compatible because fields
  are only appended.

### 5.5 Tasks and data path
- **HA sink** (registered with `level_report_add_sink()` in `ha_mqtt_start()`, before the
  reporter task is created): `xQueueOverwrite(q, r)` on a depth-1 queue of
  `level_reading_t`, and nothing else.
- **Publisher task `wt_ha`** (prio 3, i.e. below reporter (4) and RX (5); 4096 B stack
  **[ASSUMPTION; check the high-water mark in V-13]**) loops on `xQueueReceive(q,
  portMAX_DELAY)`, so it wakes once per report period. It publishes when MQTT is connected
  **and** either (a) `now - last_pub ≥ WT_HA_PUBLISH_PERIOD_MS`, or (b) the
  availability-relevant status changed (`OK/OUT_OF_RANGE` ↔ `NO_DATA`). In case (b) it
  publishes the `sensor` topic first, then the state. While disconnected, readings are
  dropped. There is no backlog, because only the newest value matters.
- esp-mqtt runs its own task (default prio 5, `CONFIG_MQTT_TASK_STACK_SIZE` default 6144)
  and has outbox handling. `esp_mqtt_client_publish()` is called only from `wt_ha`.
  Connection state is a `volatile bool`/event-group bit set in the MQTT event handler.
- Start-up order in `app_main`: `init_nvs` → banner → `a02yyuw_init` → `level_report_init`
  → `ha_mqtt_start` (registers the sink; if configured, it starts netif, the event loop,
  Wi-Fi, the MQTT client and `wt_ha`) → reporter task. This order respects the
  `level_report_add_sink` rule (no publishing before registration completes).
- `esp_netif_init()` and `esp_event_loop_create_default()` are called inside
  `ha_mqtt_start()`, and only when configured. Nothing else in the firmware uses them.

### 5.6 Connectivity behaviour
- Wi-Fi STA, `WIFI_AUTH_WPA2_PSK` minimum threshold (`WIFI_AUTH_OPEN` if the password is
  empty), power save left at the IDF default (`WIFI_PS_MIN_MODEM`).
- On `WIFI_EVENT_STA_DISCONNECTED`: reconnect after a delay of 1 s, 2 s, 4 s … capped at
  60 s, reset on `IP_EVENT_STA_GOT_IP`. The delay runs in an `esp_timer` one-shot, not in
  the event handler. The MQTT client is started once after the first IP and relies on its
  own reconnect (`reconnect_timeout_ms` 10 s) afterwards.
- `esp_log_level_set("wifi", ESP_LOG_WARN)` keeps the console mostly free of Wi-Fi driver
  chatter. The `ha` tag logs one line per state change: `wifi connected ip=…`,
  `wifi disconnected reason=<n>, retry in <s> s`, `mqtt connected`, `mqtt disconnected`.
- No failure in this path calls `abort()` or `esp_restart()`. The device keeps measuring and
  printing on UART regardless of network state (R-07 spirit, §3.6).

### 5.7 Banner addition (appended line, §8 compatible)
```
I (t) ha: enabled id=<id> ssid=<ssid> broker=<host:port> user=<user|-> period_ms=<n>
I (t) ha: disabled (no wifi_ssid/mqtt_uri configured)
```
The `ha:` tag cannot match the report-line rule `\bwt: distance_mm=`, so `check_log.py`
is unaffected (to be confirmed by V-09b with an added synthetic case).

### 5.8 Resource budget (estimates, to be replaced by CI numbers)

| Item | Stage 1 (measured, Build #5) | Stage 2 (estimate) |
|------|------------------------------|--------------------|
| App image | 0x35210 = 217 KB | ~800–950 KB **[ASSUMPTION: typical IDF 5.x Wi-Fi + esp-mqtt app]** |
| App partition | 1 MB (single app) | **1.5 MB** (`CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`), leaving ≥ 35 % headroom for TLS later |
| Heap | small | +~60–90 KB for Wi-Fi/lwIP/MQTT **[ASSUMPTION]**; ESP32 has ~300 KB of free DRAM at boot |
| Power on 3V3 | ESP32 idle + sensor ≤ 8 mA | Wi-Fi TX peaks ~250–350 mA. These may sag the board LDO and the shared sensor supply, so watch for brownout resets (V-12) and higher `cks_err` (V-10) with Wi-Fi active |

The partition-table change also applies to Stage 1 functionality. It costs nothing, but
changes the flash layout, so Stage 2 images must be flashed with their partition table
(§4.1).

### 5.9 Testability
- `ha_payload.c` (topic/JSON builders: `snprintf` into caller buffers, returns length or
  -1 on truncation) has no IDF dependencies, so `test/host/test_ha_payload.c` runs with the
  existing `Makefile`.
- The rest can only be verified with a Wi-Fi AP, a broker and HA. The bench lists none of
  these, so the operator must perform V-13a…g.

### 5.10 Implementation state of Stage 2 (run #5 static review of `7bac3ea`)

Read-only review; nothing compiled or executed. The written files follow §5.1–§5.9 closely.

#### 5.10.1 What exists and matches the design
- `ha_payload.c`: topic, device-ID, state and discovery builders exactly as §5.4 (field
  order, `availability_mode: all`, status entity `diagnostic`). Rejects JSON-unsafe strings
  (`"`, `\`, control chars) and MQTT wildcards rather than escaping them. Extra helper
  `ha_payload_uri_host()` strips `user:pw@` for the banner (§5.3 "never log passwords").
- `test_ha_payload.c`: exact-string checks for every builder, truncation boundaries,
  invalid input. The Makefile builds and runs it with `-Werror`. Hand-checked boundary
  lengths (25-char state topic, 69-char OK payload) are consistent with the strings.
- `ha_config.c`: per-key NVS → Kconfig fallback; a missing namespace is silent, other NVS
  errors are a warning.
- `ha_mqtt.c`: runtime gate, depth-1 overwrite sink, `wt_ha` task (prio 3, 4096 B),
  1→2→4…→32→60 s esp_timer backoff, MQTT client started once after the first IP, LWT
  `offline` retained, discovery + birth on every connect, sensor-availability topic
  published before the state on a transition, no `abort()`/`esp_restart()`. It also calls
  `esp_wifi_set_storage(WIFI_STORAGE_RAM)` so the driver never copies credentials into
  its own NVS namespace (good, not in the original design).

#### 5.10.2 Decisions for the remaining steps
1. **`components/ha_mqtt/CMakeLists.txt`** (the missing piece that keeps the component out
   of the build today):
   ```cmake
   set(srcs "ha_payload.c")
   if(CONFIG_WT_HA_ENABLE)
       list(APPEND srcs "ha_config.c" "ha_mqtt.c")
   endif()
   idf_component_register(
       SRCS ${srcs}
       INCLUDE_DIRS "include"
       REQUIRES level_report
       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash mqtt esp_timer esp_app_format
   )
   ```
   `esp_app_format` is needed for `esp_app_desc.h` (F-23). `esp_hw_support` (`esp_mac.h`)
   and `log` are IDF common requirements. `CONFIG_*` variables are available in component
   CMake files during the build pass **[DATASHEET-RECALL: IDF build system]**.
2. **`main/CMakeLists.txt`**: add `ha_mqtt` to `REQUIRES` unconditionally (it always has
   at least `ha_payload.c`, so the include path exists in both configurations).
3. **`main.c`**: `#if CONFIG_WT_HA_ENABLE` → `#include "ha_mqtt.h"` and, between
   `level_report_init()` and the reporter `xTaskCreate`, `(void)ha_mqtt_start();` with a
   comment that errors are already logged and HA is optional. Update the file comment
   ("Stage 1" → Stage 1 + optional Stage 2). Remove the "Not used in Stage 1" NVS comment.
4. **Kconfig**: submenu "Home Assistant" inside "Water Tank Sensor":
   `WT_HA_ENABLE` (bool, default y); the remaining options `depends on WT_HA_ENABLE`:
   `WT_WIFI_SSID`, `WT_WIFI_PASSWORD`, `WT_MQTT_BROKER_URI`, `WT_MQTT_USERNAME`,
   `WT_MQTT_PASSWORD` (string, default ""), `WT_HA_PUBLISH_PERIOD_MS` (int, 10000, range
   1000 600000), `WT_HA_DISCOVERY_PREFIX` (string, "homeassistant"). These are exactly the
   macros `ha_config.c`/`ha_mqtt.c` already reference.
5. **Steps 5 + 6 (CMake and Kconfig) must land in the same commit.** With only the CMake
   file, `ha_mqtt.c` references undefined `CONFIG_WT_*` macros and the build breaks.

#### 5.10.3 Review findings on the written code (to fix in M6a)
See §7.2 F-22…F-29. None is known to break compilation once §5.10.2 is in place, but this
cannot be confirmed without a CI build.

**Expected component-tree compile behaviour today [ASSUMPTION]:** the IDF build skips a
`components/` sub-directory that has no `CMakeLists.txt` (possibly with a notice), so a
build of `7bac3ea` should still produce the Stage 1 image. Unverified; no build was run.

#### 5.10.4 Run #6 compile-surface pass (read-only; API facts are [DATASHEET-RECALL] for IDF v5.4)
This pass checks each external symbol and flag the Stage 2 sources rely on, so that the
first CI build of M6a steps 5–7 has as few surprises as possible. **This is not a
substitute for that build.**

| Item | Where | Result |
|------|-------|--------|
| `esp_mqtt_client_config_t` v5 nested fields: `broker.address.uri`, `credentials.username`, `credentials.client_id`, `credentials.authentication.password`, `session.keepalive`, `session.last_will.{topic,msg,qos,retain}`, `network.reconnect_timeout_ms` | `ha_mqtt.c` `start_network()` | Match the v5 layout. A `msg_len` of 0 means `strlen(msg)` |
| `esp_mqtt_error_codes_t.error_type` / `.connect_return_code`, `MQTT_ERROR_TYPE_CONNECTION_REFUSED` | `on_mqtt_event()` | Present in v5 |
| `esp_mqtt_client_register_event(client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, …)` | `start_network()` | The documented idiom |
| `IPSTR` / `IP2STR`, `ip_event_got_ip_t` | `on_net_event()` | Come in through `esp_netif.h` |
| `wifi_event_sta_disconnected_t.reason` | `on_net_event()` | Cast to `int` for `%d`, so the field width doesn't matter |
| `esp_read_mac` (`esp_mac.h`, `esp_hw_support`), `esp_check.h` macros, `log` | `ha_mqtt.c` | Common requirements, so no `REQUIRES` entry is needed |
| `nvs.h` | `ha_config.c` | `nvs_flash` is in `PRIV_REQUIRES` (§5.10.2) |
| `esp_app_desc.h` | `ha_mqtt.c` | Needs `esp_app_format` (F-23, already in the §5.10.2 text) |
| `uxTaskGetStackHighWaterMark` | `publisher_task()` | Enabled by default in IDF FreeRTOS |
| `strlcpy` | `ha_config.c`, `ha_mqtt.c` | Declared by IDF newlib `<string.h>` (used throughout IDF) |
| `ESP_GOTO_ON_ERROR` / `ESP_GOTO_ON_FALSE` need a local `esp_err_t ret` and a `fail:` label | `start_network()` | Both present |
| `-Wall` format checks | all `ESP_LOG*` calls | `%u` + `(unsigned)`, `%d` + `(int)`, `PRIu32`/`PRId32` for the fixed-width types, `%d` for Kconfig ints (plain literals) |
| Unused statics / variables (`-Werror=unused-*`) | `ha_mqtt.c` | Every `HA_*` macro and static is referenced. With `WT_HA_ENABLE=n` the file is not compiled at all |
| Host build `-std=c99 -Wall -Wextra -Werror` | `ha_payload.c` | Only C89/C99 library calls (`snprintf`, `strpbrk`, `strcspn`, `strstr`). `(unsigned)enum > CONST` compares against a non-negative constant, which does not trigger `-Wsign-compare` |
| Kconfig names used in C | `ha_config.c`, `ha_mqtt.c` | `WT_WIFI_SSID`, `WT_WIFI_PASSWORD`, `WT_MQTT_BROKER_URI`, `WT_MQTT_USERNAME`, `WT_MQTT_PASSWORD`, `WT_HA_PUBLISH_PERIOD_MS`, `WT_HA_DISCOVERY_PREFIX`. Exactly the set planned in §5.10.2 item 4, and none exists yet |

Residual compile risk: the `-Werror=all` behaviour of the GCC 13 toolchain bundled with
IDF 5.4 on the network code (for example `-Wmissing-field-initializers` is not part of `-Wall`, so
partial designated initialisers are safe). If the first build fails, the CI log decides.

## 6. Assumptions and gaps

| ID | Gap / uncertainty | Assumption taken | Impact if wrong | Resolution path |
|----|-------------------|------------------|-----------------|-----------------|
| G1 | Which of GPIO16/17 is sensor TX vs RX | Sensor TX → GPIO16 | `ok=0`, NO_DATA; output contention on GPIO17 (F-13) | Check the wiring first; diagnose with TX = -1; swap in menuconfig or rewire; V-03 detects it |
| G2 | Module type (WROOM vs WROVER) | WROOM-32, no PSRAM | GPIO16/17 unusable | Read the module can marking; move pins to e.g. GPIO25/26 |
| G3 | No sensor datasheet supplied | SEN0311 protocol from recall (§2.2) | Parser constants wrong | Operator supplies the vendor doc; V-03/V-06 confirm on hardware |
| G4 | IDF version | **Resolved for CI: v5.4.2.** A local bench build may differ; the banner prints it | < v5.3 branch of F-12 unbuilt | — |
| G5 | HA setup: broker present? HA version? ESPHome preference? | Mosquitto add-on (or any MQTT 3.1.1 broker) reachable on the LAN, plain `mqtt://` on 1883, user/password auth | Stage 2 code unused until configured; if no broker can be installed, a different sink is needed | Operator confirms before or during M6 verification. The design is runtime-gated, so no harm until then |
| G6 | Acceptance criteria empty | Derived criteria (VERIFICATION_MATRIX.md) | Wrong pass/fail bar | Operator confirms AC-1…AC-8 |
| G7 | Sensor performance at 3.3 V | Adequate | Reduced range | V-08 at real tank depth |
| G8 | Tank geometry unknown | Raw distance only | No `level_percent` in HA | Operator supplies empty/full distances |
| G9 | Whether the sensor RX wire is connected | Works either way | None | — |
| G10 | Board auto-reset circuit present? | Present (typical CH340 DevKit clone) | Manual BOOT/EN needed to flash | V-02 observation |
| G11 | `CONFIG_LOG_COLORS` with an old local `sdkconfig` | Disabled in `sdkconfig.defaults` | Only for a stale local `sdkconfig`; `check_log.py` strips ANSI anyway | `idf.py fullclean` / delete `sdkconfig` |
| G12 | Who commits the worktree | The platform commits at phase end (observed every phase since `65c6ad9`) | Build blocked ("worktree is dirty") | If it recurs, commit `source/` and re-trigger |
| G13 | CI workflow contents | **Resolved** (read-only): `espressif/idf:v5.4.2`; whole `source/build` uploaded with `source-commit.txt`, `toolchain-version.txt`, `SHA256SUMS`; 30-day retention | — | §4.1 |
| G14–G16 | CI push 403, `workflow` scope, git "dubious ownership" | **All resolved** by operator actions; Build #8 passed end to end | — | — |
| G17 | Wi-Fi + sensor on one 3V3 rail (new, Stage 2) | Board LDO copes with Wi-Fi TX peaks | Brownout resets, noisy sensor | V-12/V-10 with HA enabled; add bulk capacitance (e.g. 100 µF near sensor VCC) or power the sensor from 5 V **only after** checking its TX logic level |
| G18 | 2.4 GHz coverage at the tank (new) | Adequate RSSI | Frequent reconnects, gaps in HA | `ha` log lines show disconnect reasons. Optional RSSI field later |
| G20 | Why run #4 implementation attempt 2 was recorded as failed (run #5) | Phase ended before steps 5–10; no build was triggered (no builder result after Build #8) | If the failure was a build error on a pushed commit, its log is missing here | Operator/platform supplies the failure reason or a CI log for `7bac3ea` if one exists |
| G21 | Will IDF ignore `components/ha_mqtt/` without `CMakeLists.txt`? (run #5) | Yes, the directory is skipped and the Stage 1 image builds | If wrong, any build of `7bac3ea` fails at configure time | Moot once M6a step 5 lands |
| G22 | Why run #5 architecture (`bba5d98`) was recorded as failed (run #6) | Platform-side outcome; the deliverables it wrote are present and consistent with the tree | If a doc defect caused it, it is not identifiable from the tree | Operator/platform supplies the reason |
| G19 | Bundled `mqtt` component in future IDF versions | Present in v5.4.2 | Build break on IDF ≥ 6 if it is moved to the component registry **[ASSUMPTION]** | CI pins v5.4.2; revisit on an IDF upgrade |

## 7. Implementation status and static review

### 7.1 Differences from the run #1 plan
| Planned | Implemented | Assessment |
|---------|-------------|------------|
| RX task polls `uart_read_bytes` with a 100 ms timeout | Event-queue RX task, chunked reads | Better: handles overflow and line errors too |
| Latest sample behind a mutex | `portMUX` spinlock | Fine: short struct copies |
| Report line `distance_mm status age_ms ok cks_err` | Adds `sync_skip uart_ovf rx_bytes line_err` | Superset, OK |
| Median filter "over last ~10 frames" | Kconfig window 3–15, default 5, off by default | OK |
| — | Median reset after a data gap; filter split into its own pure-C module | Added |

### 7.2 Findings

| ID | Finding | State |
|----|---------|-------|
| F-01 | Code never compiled | **Closed**: compiled and linked on IDF v5.4.2 (Build #5), full CI pass in Build #8 |
| F-02 | Host test vector checksums | Checked by hand; V-09 not executed |
| F-03 | `bytes_rx` / `line_errors` not printed | **Done**: `rx_bytes=` / `line_err=` appended |
| F-04 | `CONFIG_LOG_COLORS` not set | **Done** |
| F-05 | Kconfig allowed flash/non-existent GPIOs | **Done**: validated in `a02yyuw_init` |
| F-06 | First tick logs "data lost" when no sensor | Intended (V-03b) |
| F-07 | Redundant `gpio_pullup_en` | Harmless, kept |
| F-08 | Single sink | **Done**: dispatcher + `level_report_add_sink`, compiled in CI |
| F-09 | Misaligned window passes checksum ~1/256 while resyncing | Mitigated by median option; watch `cks_err` (V-10) |
| F-10 | `GPIO_IS_VALID_*` macros | **Closed**: compiled on v5.4.2 |
| F-11 | Host tests / checker self-tests hand-traced, never run | V-09, V-09b |
| F-12 | Driver component split in IDF v5.3 | **Done**; ≥ v5.3 branch build-confirmed, < v5.3 unbuilt |
| F-13 | GPIO17 contention on swapped wiring | Documented; diagnose with TX = -1. Operator decision on default pending |
| F-14 | `-Werror=all` exposure | Read-checked. The compile succeeded, but the warning scan is unconfirmed (V-01) |
| F-15…F-19 | CI process blockers (dirty tree, 403, `workflow` scope, dubious ownership) | **All resolved**; Build #8 completed |
| F-20 (new) | The workflow's `SHA256SUMS` probably lists itself, so a plain `sha256sum -c` would report one false failure | Workaround in §4.1. Platform file, not edited by agents |
| F-21 | `init_nvs()` erase-on-version-change would wipe Stage 2 credentials | Accepted, documented (§5.3) |
| F-22 (run #5) | `components/ha_mqtt/CMakeLists.txt`, the Kconfig "Home Assistant" submenu, the `main` hook and `sdkconfig.defaults` changes are missing, so Stage 2 is **not part of any build** | Open, **blocker for R-08**. M6a step 5–7, one commit (§5.10.2) |
| F-23 (run #5) | `ha_mqtt.c` includes `esp_app_desc.h` → the component needs `esp_app_format` in `PRIV_REQUIRES` | Open; included in the §5.10.2 CMake text |
| F-24 (run #5) | `.gitignore` lacks `test/host/test_ha_payload` (host binary), `nvs_config.csv`, `nvs*.bin`. The last two matter for R-08e (no secrets in git) | Open, M6a step 8 |
| F-25 (run #5) | `schedule_reconnect()` advances the backoff even when the timer is already armed (`ESP_ERR_INVALID_STATE`), so a second disconnect event during a pending retry doubles the delay and logs a wrong "retry in N s" | Open, minor. Advance the delay only when `esp_timer_start_once` returns `ESP_OK`; otherwise log without a delay value |
| F-26 (run #5) | `start_network()` fail path leaves the two event-handler instances and the default STA netif registered after `esp_wifi_deinit()` | Accepted (no events fire after deinit; failure path is one-shot). Add a comment |
| F-27 (run #5) | `esp_mqtt_client_publish()` performs socket I/O in `wt_ha` and can block it for the network timeout | Accepted by design: the reporter only does `xQueueOverwrite` (§5.5). V-13e checks the UART stream during an outage |
| F-28 (run #5) | `ha_mqtt.c`/`ha_config.c` emit log lines not in §8 (`discovery published, wt_ha stack free=`, `wifi connect failed`, `mqtt connection refused/error`, `mqtt start failed`, `nvs key … unreadable`, `reconnect timer:`) | **Done in this phase**: §8 updated. No code change needed |
| F-29 (run #5) | Only the `wifi` tag is lowered to WARN. Other network tags (e.g. `phy_init`, `esp_netif_handlers`, `net80211`, `pp`) may still print INFO lines at connect **[ASSUMPTION]** | Harmless for `check_log.py` (it reads only `wt:` report lines and crash patterns). Observe in V-13b; lower more tags only if the console becomes noisy |
| F-31 (run #6) | During a broker outage, esp-mqtt and its transport print their own `E`/`W` lines (tags such as `mqtt_client`, `transport_base`, `esp-tls`) on **every** reconnect attempt, i.e. every `HA_MQTT_RECONNECT_MS` = 10 s **[DATASHEET-RECALL]**. That defeats the "once per outage" intent of the `ha` lines | Accepted for M6a, no code change. These lines contain none of the V-12 failure patterns, so `check_log.py` verdicts are unaffected. Observe the noise in V-13e/V-13f. If it is a problem, lower those tags with `esp_log_level_set(..., ESP_LOG_NONE)` or `ESP_LOG_ERROR`, together with F-29 |
| F-30 (run #5) | Once Stage 2 links, the app is expected to exceed the 1 MB default partition (§5.8), so the CMake/Kconfig commit without `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` would likely fail at the size check **[ASSUMPTION]** | Open: M6a step 7 goes into the same commit as steps 5–6 |

### 7.3 ESP-IDF APIs used (build-compatibility surface)
Stage 1 (compiled on v5.4.2): `uart_driver_install`, `uart_param_config`
(`UART_SCLK_DEFAULT`), `uart_set_pin`, `uart_read_bytes`, `uart_flush_input`,
`uart_driver_delete`, `uart_event_t`; `gpio_pullup_en`, `GPIO_IS_VALID_GPIO`,
`GPIO_IS_VALID_OUTPUT_GPIO`; `esp_timer_get_time`; `ESP_RETURN_ON_FALSE` /
`ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR`; `xTaskCreate`, `xTaskDelayUntil`,
`xQueueReceive`, `xQueueReset`, `taskENTER/EXIT_CRITICAL`; `nvs_flash_init` /
`nvs_flash_erase`; `esp_app_get_description`, `esp_chip_info`, `esp_flash_get_size`,
`esp_get_idf_version`. Needs IDF ≥ v5.0.

Stage 2 (written in `7bac3ea`, **not yet compiled**; also uses `esp_wifi_set_storage`,
`esp_mqtt_client_destroy`, `esp_wifi_deinit`, `uxTaskGetStackHighWaterMark`, `strlcpy`): `esp_netif_init`, `esp_event_loop_create_default`,
`esp_netif_create_default_wifi_sta`, `esp_wifi_init` (`WIFI_INIT_CONFIG_DEFAULT`),
`esp_wifi_set_mode`, `esp_wifi_set_config`, `esp_wifi_start`, `esp_wifi_connect`,
`esp_event_handler_instance_register`; `esp_read_mac`; `esp_timer_create` /
`esp_timer_start_once`; `nvs_open` / `nvs_get_str` / `nvs_close`; `esp_mqtt_client_init`
(`esp_mqtt_client_config_t` v5 layout: `.broker.address.uri`,
`.credentials.username`, `.credentials.authentication.password`, `.session.last_will.*`),
`esp_mqtt_client_register_event`, `esp_mqtt_client_start`, `esp_mqtt_client_publish`;
`xQueueCreate` / `xQueueOverwrite`. Component `REQUIRES`: `esp_wifi esp_netif esp_event
nvs_flash mqtt esp_timer level_report` (PRIV as appropriate).

### 7.4 Offline bench log checker
`test/bench/check_log.py` reads a captured console log (file or stdin) and never opens a
port. It splits the log into boot segments at each firmware banner, parses report lines
with the §8 rule, and evaluates:

| Check | What the script can decide from the log | What still needs the operator |
|-------|-----------------------------------------|-------------------------------|
| V-03 | Banner pins/baud; first `OK` ≤ 2 s after the banner; `ok` strictly increasing over `OK` lines | Sensor actually connected |
| V-03b | Exactly one `sensor data lost`; every report `NO_DATA distance_mm=-1 age_ms=-1 ok=0`; one banner | Sensor actually disconnected |
| V-05 | Every interval within period ± 50 ms; cumulative drift ≤ 50 ms; span ≥ 59 s | — |
| V-10 | Final `cks_err/ok` < 1 %, `uart_ovf` = 0; span ≥ 10 min | — |
| V-11 | `age_ms` of the first `NO_DATA` line ≤ 2000; warning/recovery messages; recovery without reboot | Replug → `OK` latency |
| V-12 | One boot segment; no crash/reset text after the banner; monotonic timestamps; run ≥ 1 h | — |

Verdicts: PASS / FAIL / INCONCLUSIVE (exit 0 / 1 / 3). The script assumes the default log
timestamp format `(<ms>)` (`CONFIG_LOG_TIMESTAMP_SOURCE_RTOS`). It has not been run.

## 8. Telemetry contract (UART0 @115200, 8N1)

Format: standard ESP-IDF log prefix `<L> (<ms since boot>) <tag>: <message>`. The host
uses the `<ms since boot>` field for timing checks (V-05).

**Boot banner** (once per boot, tag `wt`, then `a02yyuw`; Stage 2 appends one `ha` line):
```
I (t) wt: === water_tank fw <ver> (built <date> <time>) ===
I (t) wt: idf=<idf ver> chip_rev=v<maj>.<min> cores=<n> flash=<MB>MB
I (t) wt: sensor=A02YYUW uart=<n> rx_gpio=<n> tx_gpio=<n> baud=9600
I (t) wt: report_period_ms=<n> stale_ms=<n> median_window=<n>
I (t) a02yyuw: UART<n> @9600 8N1, rx=GPIO<n> tx=GPIO<n>, median_window=<n>
I (t) ha: enabled id=<id> ssid=<ssid> broker=<host:port> user=<user|-> period_ms=<n>   # or: ha: disabled (...)
```

**Report line** (every `report_period_ms`, tag `wt`, level I). **Unchanged by Stage 2**:
```
distance_mm=<int|-1> status=<OK|NO_DATA|OUT_OF_RANGE> age_ms=<int|-1> ok=<u32> cks_err=<u32> sync_skip=<u32> uart_ovf=<u32> rx_bytes=<u32> line_err=<u32>
```
Future fields must only be appended.

**Event lines:**
- `W wt: sensor data lost: no valid frame for > <stale_ms> ms (check wiring, RX/TX pins)`
- `I wt: sensor data available`
- `W a02yyuw: UART FIFO|ring buffer overflow, input flushed`
- `E a02yyuw: ... not usable ...` / `RX and TX GPIO must differ` (invalid configuration, followed by an abort)
- Stage 2 (tag `ha`), as written in `ha_mqtt.c` / `ha_config.c`:
  - `I ha: wifi connected ip=<ip>`
  - `W ha: wifi disconnected reason=<n>, retry in <s> s` / `W ha: wifi connect failed (<err>), retry in <s> s`
  - `I ha: mqtt connected`, `W ha: mqtt disconnected` (transition only)
  - `W ha: mqtt connection refused (code <n>), check user/password` / `W ha: mqtt connection error (type <n>), retrying` (once per outage)
  - `I ha: discovery published, wt_ha stack free=<n> B` (each connect; source of the V-13g stack figure)
  - `E ha: mqtt start failed: <err>`, `E ha: reconnect timer: <err>`
  - `W ha: nvs key <key> unreadable (<err>), using Kconfig value` / `W ha: nvs namespace wt_cfg not readable (<err>), …`
  - `E ha: <step>: <err>` from the `ESP_*_ON_*` macros, then `E ha: init failed: <err>; HA off`
  - Other network tags (`phy_init`, `esp_netif_handlers`, …) may add INFO lines (F-29).
  - esp-mqtt tags (`mqtt_client`, `transport_base`, `esp-tls`) may print `E` lines every
    ~10 s while the broker is unreachable (F-31). They are not failures under V-12.

**Host parsing rule:** a line matching `\bwt: distance_mm=` is a report line. Parse the
fields as whitespace-separated `key=value` pairs. Any `Guru Meditation`, `abort()`,
`rst:`, `Task watchdog` or `Brownout detector` text after the boot banner counts as a
failure in V-12. Reference implementation: `test/bench/check_log.py`.
