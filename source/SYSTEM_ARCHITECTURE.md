# SYSTEM ARCHITECTURE — Water Tank Level (project 1a95ca931d72)

Status history:
- Run #1: ARCHITECTURE (attempt 1), then IMPLEMENTATION (attempt 2). Stage 1 source was written.
- Run #2, ARCHITECTURE phase (attempt 2, 2026-10-16): this document was updated to match
  the code. §7 (implementation status + static review) and §8 (telemetry contract) were added.
- Run #2, IMPLEMENTATION phase (attempt 3, 2026-10-16): static-review fixes F-03, F-04 and F-05
  were implemented. The offline bench log checker `test/bench/check_log.py` was added (§7.4).
- Run #2, REVIEW phase (attempt 4, 2026-10-16): static re-review of all sources and tests.
  F-12 (IDF-version-dependent driver requirement) was fixed. F-13 (output contention on
  swapped wiring) is documented with a mitigation. Verification results are now
  `NOT VERIFIED`.
- Run #3, ARCHITECTURE phase (attempt 1, 2026-10-16): static re-read of the firmware sources,
  CMake files, Kconfig and `sdkconfig.defaults`. No code defects found. Added the first
  builder result: GitHub Actions **Build #1 was blocked** because the Git worktree is dirty.
  All project files are untracked, so nothing was compiled (F-15, G12). Added the CI build
  path and CI-artifact flashing to §4 (G13). F-16 covers bench evidence files hidden by
  `.gitignore`.
- Run #3, IMPLEMENTATION phase (attempt 2, 2026-10-16): Git status at phase start was
  **clean**. Commit `65c6ad9` ("MOTHER run 3 architecture attempt 1: phase_complete")
  contains the whole tree, so the platform commits at the end of each phase. F-15 should
  therefore not block the next CI build, but no new builder result exists yet. Static
  re-read found no defects. Implemented the `level_report` sink fan-out (F-08, M6
  groundwork). The console output is unchanged (§3.3).
- Run #3, IMPLEMENTATION phase (attempt 6, 2026-10-17): CI **Build #5 compiled and linked**
  the firmware on `espressif/idf:v5.4.2` (`water_tank.bin`, 0x35210 bytes). The job then
  failed in the workflow's packaging step on a git "dubious ownership" error (F-19, G16),
  not on a compile error. G4/G13 resolved, and F-01/F-10/F-12 (≥ v5.3 branch) build-confirmed.

**Compiled in CI only (Build #5). Nothing has been flashed or tested on hardware.** No agent
phase so far had a shell, a toolchain, or bench access. Hardware facts below either come from the project
specs or are marked **[ASSUMPTION]** / **[DATASHEET-RECALL]**. The second marker means
general knowledge of the part that has not been checked against a supplied document (no
reference documents were provided).

---

## 1. Purpose and scope

| Stage | Goal | Source | State |
|-------|------|--------|-------|
| Stage 1 | Read an A02YYUW ultrasonic sensor over UART and print the distance on the UART console **once per second** | PURPOSE rev 1, TELEMETRY rev 1 | Source written, unverified |
| Stage 2 | Send the level data to the user's Home Assistant | PURPOSE rev 1 ("Then we will implement…") | Design outline only (§5) |

## 2. Hardware context

### 2.1 Target
- "ESP32 dev board from AliExpress", ESP-IDF framework.
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
not depend on GPIO17.

GPIO16/17 are not strapping pins and are not flash pins on WROOM-32, so they are safe at
boot.

## 3. Software architecture

### 3.1 Platform
- ESP-IDF **v5.x**, target `esp32` **[ASSUMPTION — TOOLCHAIN spec is empty]**. The APIs
  used are listed in §7.3 and are expected to be stable across v5.x (not verified by a
  build). The driver-component split in v5.3 is handled in `components/a02yyuw/CMakeLists.txt`
  (F-12). v6.x is untested.
- FreeRTOS (bundled), single app image, default partition table, no OTA in Stage 1.

### 3.2 Project layout (as implemented)

```
source/
├── CMakeLists.txt                 # project(water_tank), PROJECT_VER 0.1.0
├── sdkconfig.defaults             # esp32, 4 MB flash, console UART0 @115200, INFO logs, WT_* defaults
├── main/
│   ├── CMakeLists.txt             # REQUIRES a02yyuw level_report nvs_flash esp_app_format spi_flash
│   ├── Kconfig.projbuild          # menu "Water Tank Sensor" (WT_* options)
│   └── main.c                     # app_main, banner, NVS init, reporter task (fixed rate)
├── components/
│   ├── a02yyuw/                   # REQUIRES driver (< v5.3) | esp_driver_uart (+ PRIV esp_driver_gpio) (≥ v5.3); PRIV esp_timer
│   │   ├── include/a02yyuw.h          # driver API (config, sample, stats)
│   │   ├── include/a02yyuw_parser.h   # pure-C frame parser API + protocol constants
│   │   ├── include/a02yyuw_filter.h   # pure-C running median API
│   │   ├── a02yyuw.c                  # UART install, event-driven RX task, latest sample
│   │   ├── a02yyuw_parser.c
│   │   └── a02yyuw_filter.c
│   └── level_report/              # sink dispatcher; Stage 1 = console sink
│       ├── include/level_report.h     # reading type, sink type, dispatcher API
│       ├── level_report.c             # sink registry + fan-out, status strings
│       ├── level_report_priv.h        # component-internal: console sink prototype
│       └── level_report_uart.c        # console sink (§8 report line)
└── test/
    ├── host/                      # host unit tests (gcc/cc, no IDF)
    │   ├── Makefile               # make -C test/host run
    │   └── test_parser.c
    └── bench/                     # offline log checker (Python 3 stdlib), reads capture files only
        ├── check_log.py           # §8 parser + V-03/V-03b/V-05/V-10/V-11/V-12 criteria
        └── test_check_log.py      # self-tests on synthetic logs
```

### 3.3 Module responsibilities

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
  installs the UART driver (512 B RX ring buffer, no TX
  buffer, 16-entry event queue) at 9600 8N1 with no flow control, sets the pins (TX = -1
  leaves the pin unconfigured), enables the RX pull-up, and starts the RX task. On any
  failure it deletes the driver and returns an error.
- RX task (prio 5, 3 KB stack) blocks on the UART **event queue**:
  - `UART_DATA`: read in chunks of up to 64 B and feed the parser byte by byte. Each valid
    frame updates the filter and the latest sample.
  - `UART_FIFO_OVF` / `UART_BUFFER_FULL`: warning, flush input, reset the queue and the
    parser, increment `uart_overflows`.
  - `UART_FRAME_ERR` / `UART_PARITY_ERR`: increment `line_errors`.
- The latest sample and the stats are shared through a `portMUX` spinlock critical
  section. The sample is timestamped with `esp_timer_get_time()`.
- The median filter is cleared if the gap since the previous frame exceeds `stale_ms`, so
  values from before a dropout are not mixed with new ones.
- `a02yyuw_get_latest()` returns `true` only if a sample exists and is ≤ `stale_ms` old. It
  always fills `age_ms` (−1 if no frame was ever received) and sets `in_range`.

**`level_report`** (sink dispatcher, run #3)
- `level_report_init()` registers the console sink. Calling it twice returns
  `ESP_ERR_INVALID_STATE`.
- `level_report_add_sink(level_sink_fn, ctx)` registers up to `LEVEL_REPORT_MAX_SINKS` (4)
  sinks. It returns `ESP_ERR_INVALID_ARG` for a NULL function and `ESP_ERR_NO_MEM` when
  full. It is not thread-safe and must be called during start-up, before the reporter task
  publishes.
- `level_report_publish(const level_reading_t *)` calls every sink in registration order
  from the reporter task. Sinks must not block; a network sink queues the reading.
- Console sink (`level_report_uart.c`): one `key=value` line through `ESP_LOGI` (tag
  `wt`), byte-identical to the run #2 output. See §8.

**`main.c`**
- `app_main`: NVS init (erases and retries on a version or no-free-pages error; prepared
  for Stage 2), boot banner, `a02yyuw_init`, `level_report_init`, reporter task
  (prio 4, 4 KB stack). `app_main` then returns, which IDF allows.
- Reporter task: `xTaskDelayUntil`, fixed period `CONFIG_WT_REPORT_PERIOD_MS` with no drift.
  Each tick it builds a `level_reading_t`:
  - not fresh → `NO_DATA`, `distance_mm = -1`
  - fresh and in range → `OK`
  - fresh but out of range → `OUT_OF_RANGE` (value still reported)

  A status *transition* logs exactly one warning (into NO_DATA) or info message (out of
  NO_DATA).

### 3.4 Data flow

```
A02YYUW --9600 8N1--> GPIO16 / UART2 --ISR, 512 B ring--> event queue --> RX task
                                                                           │ parser → median filter
                                                                           ▼
                                                    latest sample + stats (spinlock)
                                                                           │
                                                 reporter task (1 Hz, xTaskDelayUntil)
                                                                           │
                                                  level_report_publish ─┬─> UART0 console (Stage 1)
                                                                        └─> HA / MQTT sink (Stage 2)
```

### 3.5 Timing budget
- Sensor traffic is ~3–10 frames/s × 4 B ≈ 12–40 B/s, against ~960 B/s of capacity at
  9600 baud. The load is negligible.
- Report latency: the reported value is at most `age_ms` old (typically < 300 ms in
  processed mode **[ASSUMPTION]**).
- Worst-case NO_DATA detection after the sensor is lost is ≈ `stale_ms + report_period`,
  or ≈ 2 s with the defaults.
- Console load is ~4–5 log lines/s at most (1 report line plus occasional warnings), well
  within 115200 baud.

### 3.6 Error handling

| Condition | Behaviour |
|-----------|-----------|
| No valid frame for > `stale_ms` | `status=NO_DATA`, `distance_mm=-1`; one warning per transition |
| Checksum failure | Counted (`cks_err`); the frame is never published; parser resyncs |
| Distance < 30 or > 4500 mm | Reported with `status=OUT_OF_RANGE` |
| UART FIFO / ring overflow | Warning, flush, parser reset, `uart_ovf` incremented |
| UART framing / parity error | `line_errors` incremented, printed as `line_err=` (F-03) |
| Invalid sensor UART/GPIO configuration | `a02yyuw_init` logs the reason and returns `ESP_ERR_INVALID_ARG` → `ESP_ERROR_CHECK` abort → reboot loop with the error visible on the console (F-05) |
| Driver/task init failure | `ESP_ERROR_CHECK` / `abort()` → panic → reboot (logged) |
| Task watchdog | IDF default TWDT; both tasks block in OS calls |

## 4. Mapping: programming, telemetry, interfaces, test constraints

| Aspect | Spec / bench | Architectural decision |
|--------|--------------|------------------------|
| Build | Adapter `esp_idf`, target `esp32`, backend GitHub Actions (automatic). Workflow added in commit `c4a0c6d`. The workflow file cannot be seen from the agent's file tools (G13) | Native IDF project at `source/` (`CMakeLists.txt` + `main/` + `components/`). The CI build uses only committed files. `sdkconfig` is git-ignored, so CI builds from `sdkconfig.defaults`. **Precondition:** the worktree must be committed and clean (F-15/G12). Record the IDF version from the CI log (G4) |
| Programming (CI artifact) | CI produces the binaries; flashing is done on the bench | Needs a full set of images: `bootloader/bootloader.bin` @0x1000, `partition_table/partition-table.bin` @0x8000, `water_tank.bin` @0x10000 (IDF defaults for esp32). The simplest path is `python -m esptool --chip esp32 -p <port> -b 460800 write_flash @flash_args`, run from the downloaded `build/` directory **[ASSUMPTION: the workflow uploads `build/` including `flash_args`]**. Otherwise `idf.py -p <port> flash` from a local build |
| Programming | FLASHING rev 1: "Flash over USB". Bench: USB-Serial (CH340) at `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0` | `idf.py -p <port> flash`. esptool enters download mode automatically via DTR/RTS. Fall back to `-b 115200` if the CH340 link is unreliable; hold BOOT on clones without auto-reset circuitry. Flash layout: IDF default single-app partition table, 4 MB |
| Runtime telemetry | TELEMETRY rev 1: "UART logs". Bench: UART @115200 on the same port | UART0 console at 115200, `ESP_LOG` (INFO). Machine-parseable `key=value` report line at 1 Hz, tag `wt`. Contract in §8 |
| Port sharing | Flash and log use one port | Flashing and monitoring cannot run at the same time. Close the monitor before flashing (`idf.py flash monitor` sequences them). Opening the port may reset the board through DTR/RTS, so expect a boot banner on connect |
| Sensor interface | PURPOSE: GPIO16/17, 3V3 | UART2 at 9600 8N1. Pins and port set in Kconfig `WT_SENSOR_*` |
| Upstream interface | PURPOSE Stage 2: Home Assistant | `level_report` sink abstraction; MQTT + HA discovery recommended (§5) |
| Test constraints | TEST_SETUP and ACCEPTANCE empty; bench lists no stimuli, instruments, or voltage | Verification is log observation plus manual physical checks (tape measure, target, unplugging a wire). Parser and filter are covered by host unit tests. See VERIFICATION_MATRIX.md |
| Guardrails | GUARDRAILS rev 1 empty | Self-imposed: no use of strapping or flash pins; UART0 reserved for the console; sensor on 3V3 only (no 5 V logic into a GPIO); no secrets in the repo (Stage 2) |

## 5. Stage 2 outlook — Home Assistant (design only)

Recommended approach: **MQTT with Home Assistant MQTT Discovery** (`esp-mqtt` + `esp_wifi`).
- Wi-Fi STA credentials and the broker URI/user/password come from Kconfig (local
  `sdkconfig`, git-ignored) or NVS. They are never committed.
- Publish a retained discovery config to `homeassistant/sensor/<device_id>/distance/config`
  (`device_class: distance`, `unit_of_measurement: mm`, `state_class: measurement`), plus
  an availability topic with LWT `offline` and birth `online`.
- Publish state JSON (`distance_mm`, `status`, `age_ms`) to `watertank/<device_id>/state`
  at a configurable interval (default suggestion: 10 s; the UART stream stays at 1 Hz).
- Sink fan-out: `level_report_publish()` is already a dispatcher (run #3). The MQTT sink
  registers with `level_report_add_sink()` from `app_main` before the reporter task starts.
  It must never block the reporter task: it copies the reading into a queue of depth 1
  (`xQueueOverwrite`) that a separate MQTT task consumes.
- Later: tank geometry (`empty_mm`, `full_mm`) → `level_percent`, optional `volume_l`.

Alternatives:
- ESPHome would replace this IDF codebase.
- An HA REST/webhook via `esp_http_client` has no discovery and no availability tracking.

**Prerequisite (G5):** an MQTT broker reachable from the ESP32 (e.g. the HA Mosquitto add-on).

## 6. Assumptions and gaps

| ID | Gap / uncertainty | Assumption taken | Impact if wrong | Resolution path |
|----|-------------------|------------------|-----------------|-----------------|
| G1 | Which of GPIO16/17 is sensor TX vs RX | Sensor TX → GPIO16 | `ok=0`, NO_DATA; output contention on GPIO17 (F-13) | Check the wiring first; diagnose with TX = -1; swap in menuconfig or rewire; V-03 detects it |
| G2 | Module type (WROOM vs WROVER) | WROOM-32, no PSRAM | GPIO16/17 unusable | Read the module can marking; move pins to e.g. GPIO25/26 |
| G3 | No sensor datasheet supplied | SEN0311 protocol from recall (§2.2) | Parser constants wrong | Operator supplies the vendor doc; V-03/V-06 confirm on hardware |
| G4 | IDF version unspecified | v5.x (v5.1–v5.3) | API/component differences | **Resolved for CI (Build #5): IDF v5.4.2** (`espressif/idf:v5.4.2`, `idf5.4_py3.12_env`). A local bench build may use another version; the banner prints it |
| G5 | HA setup (broker? ESPHome?) | MQTT + discovery | Stage 2 design only | Ask the operator before Stage 2 |
| G6 | Acceptance criteria empty | Derived criteria (VERIFICATION_MATRIX.md) | Wrong pass/fail bar | Operator confirms R-/V- items |
| G7 | Sensor performance at 3.3 V | Adequate | Reduced range | V-08 at real tank depth. If 5 V is needed, first check the sensor TX logic level |
| G8 | Tank geometry unknown | Raw distance only | None in Stage 1 | Needed for Stage 2 `level_percent` |
| G9 | Whether the sensor RX wire is connected | Works either way | None | — |
| G10 | Board's USB bridge reset behaviour (auto-reset circuit present?) | Present (typical CH340 DevKit clone) | Manual BOOT/EN needed to flash | V-02 observation |
| G11 | `CONFIG_LOG_COLORS` default depends on IDF version/config | Disabled in `sdkconfig.defaults` (F-04, done) | Only if an existing `sdkconfig` predates the change. `check_log.py` strips ANSI codes anyway | Delete `sdkconfig` or run `idf.py fullclean` to re-apply defaults |
| G12 | Who commits the worktree. Agent phases have file tools only, with no git | The platform commits at phase end (observed: `65c6ad9`, clean status at the start of run #3 impl.) | V-01 stays blocked ("worktree is dirty") | If a build is blocked again, commit all files under `source/`, then re-trigger the CI build |
| G13 | CI workflow contents (IDF image/version, artifacts kept) | **Resolved (read-only inspection of `../.github/workflows/nostromo-build.yml`)**: container `espressif/idf:v5.4.2`; `idf.py -C source set-target "$TARGET" build`; packaging copies all of `source/build` to `artifacts/build`, adds `source-commit.txt`, `toolchain-version.txt`, `SHA256SUMS`, and uploads `nostromo-firmware-<build id>` (30-day retention) | — | Flashing from artifacts (§4) stays valid once packaging works (G16) |
| G16 | CI packaging fails after a successful build (Build #5): `git rev-parse HEAD` → "detected dubious ownership in repository at '/__w/water_level/water_level'" | The workspace is checked out by the runner user, and the `espressif/idf` container runs as root, so git ≥ 2.35.2 refuses the repo. `actions/checkout` adds `safe.directory` only to a temporary HOME used during checkout, not to the later `run:` steps | V-01 cannot finish, and no firmware artifact is uploaded, however correct the source is | Platform/operator workflow fix, for example `git config --global --add safe.directory "$GITHUB_WORKSPACE"` (or `"$PWD"`) before `git rev-parse HEAD` in "Package binaries and build evidence", or use `${{ github.sha }}` instead of `git rev-parse`. Agents must not edit the workflow file. No change under `source/` can affect this step |
| G14 | CI push credentials and repository access. Build #2: "Permission to seblanc/water_level.git denied to seblanc", HTTP 403 | The configured token is valid (it authenticates as `seblanc`) but lacks write scope, e.g. a read-only or fine-grained PAT without "Contents: write", or one restricted to other repositories | V-01 cannot run, however correct the source is | Operator: grant the CI token write access to `seblanc/water_level` (or fix the remote URL), then re-trigger the build. Agents cannot fix this from `source/`. **Build #4: the 403 is gone** (the remote now evaluated the ref update), so write access appears fixed. See G15 |
| G15 | CI token scope for workflow files. Build #4: "refusing to allow a Personal Access Token to create or update workflow `.github/workflows/nostromo-build.yml` without `workflow` scope" | The pushed history contains the platform commit `c4a0c6d`, which adds the workflow file (project root, outside `source/`), and the remote does not have it yet | V-01 cannot run until the remote accepts that commit | Operator, one of: (a) classic PAT: add the `workflow` scope; (b) fine-grained PAT: add the "Workflows: Read and write" permission; (c) push the history once with a credential that has workflow rights (e.g. SSH key or `gh auth` with `workflow` scope). After that, pushes that don't modify the workflow file no longer need the scope. Then re-trigger the build. Agents must not edit or delete the workflow file (platform infrastructure) |

## 7. Implementation status and static review

### 7.1 Differences from the run #1 plan
| Planned | Implemented | Assessment |
|---------|-------------|------------|
| RX task polls `uart_read_bytes` with a 100 ms timeout | RX task driven by the UART event queue, chunked reads | Better: it also handles overflow and line-error events |
| Latest sample behind a mutex | `portMUX` spinlock critical section | Fine: the critical sections are short struct copies |
| Report line `distance_mm status age_ms ok cks_err` | Adds `sync_skip` and `uart_ovf` | Superset, OK |
| Median filter "over last ~10 frames" | Kconfig window 3–15, default 5, off by default | OK |
| — | Median filter reset after a data gap > `stale_ms` | Added |
| — | `a02yyuw_filter` split into its own pure-C module with host tests | Added |

### 7.2 Static review findings (from reading the code only, no compiler)
Nothing below has been confirmed by a build. These are risks or improvements for the next
implementation phase.

| ID | Finding | Severity | Proposed action |
|----|---------|----------|-----------------|
| F-01 | Code has never been compiled. Include/`REQUIRES` sets look consistent with IDF v5.x (`esp_app_desc.h`→`esp_app_format`, `esp_flash.h`→`spi_flash`, `driver/uart.h`→`driver` or `esp_driver_uart` (F-12), `esp_timer`) | High (unknown) | **Build-confirmed on IDF v5.4.2 (Build #5)**: all project objects compiled and linked |
| F-02 | Host test vectors checked by hand: checksums of `F_1953`, `F_1023`, `F_65280`, `F_766` are arithmetically correct | Info | V-09 still has to be executed |
| F-03 | `bytes_rx` and `line_errors` are collected but never printed. They are the quickest way to tell "nothing arrives" (wiring/G1) from "bytes arrive but no frames" (baud, wrong pin, noise) | Medium (diagnosability) | **Done (run #2 impl.)**: `rx_bytes=` and `line_err=` appended to the report line |
| F-04 | `CONFIG_LOG_COLORS` is not set in `sdkconfig.defaults` | Low | **Done**: `# CONFIG_LOG_COLORS is not set` |
| F-05 | Kconfig `WT_SENSOR_RX_GPIO` range 0–39 allows flash pins 6–11 and non-existent GPIOs (20, 24, 28–31). Invalid pins fail in `uart_set_pin` and abort; flash pins would hang the chip | Low | **Done**: validated in `a02yyuw_init` (§3.3), Kconfig help updated |
| F-06 | `prev_status` starts as `OK`, so a board booting without a sensor logs the "data lost" warning at the first tick (~1 s) | Info (intended) | None. Required by V-03b |
| F-07 | `uart_set_pin` already enables the RX pull-up in IDF v5; the extra `gpio_pullup_en` is redundant | Info | Keep (harmless) |
| F-08 | `level_report` takes a single sink; Stage 2 needs a fan-out | Info | **Done (run #3 impl.)**: sink registry + `level_report_add_sink` (§3.3). Not build-verified |
| F-09 | Misaligned windows pass the 8-bit checksum with probability ~1/256, only while resyncing | Low | The median filter mitigates it; check `cks_err` in V-10 |
| F-10 | `GPIO_IS_VALID_GPIO` / `GPIO_IS_VALID_OUTPUT_GPIO` are expected from `driver/gpio.h` (already included) in IDF v5.x. Not build-verified | Low | V-01. If missing, test `SOC_GPIO_VALID_GPIO_MASK` / `SOC_GPIO_VALID_OUTPUT_GPIO_MASK` from `soc/soc_caps.h` directly. **Build #5: `a02yyuw.c` compiled on v5.4.2, so the macros resolve** |
| F-11 | Hand traces of every case in `test_parser.c` and `test_check_log.py` found no wrong expectations, but neither file has been executed. Re-traced in the run #2 review with the same result | Info | V-09 and the checker self-test (V-09b) |
| F-12 | `a02yyuw` required the monolithic `driver` component, which IDF v5.3 split into `esp_driver_*`. Newer releases may remove the alias, and the bench IDF version is unknown (G4) | Medium (build portability) | **Done (run #2 review)**: requirement chosen from `IDF_VERSION_MAJOR/MINOR`. **≥ v5.3 branch (`esp_driver_uart`/`esp_driver_gpio`) build-confirmed on v5.4.2 (Build #5)**. The < v5.3 branch is still unbuilt |
| F-13 | GPIO17 is a push-pull UART TX idling HIGH. If the wiring is swapped (G1), it drives against the sensor TX output. The previous docs presented the swap as harmless to diagnose | Medium (hardware safety) | **Documented (run #2 review)**: README caution + Kconfig help. Diagnose with `WT_SENSOR_TX_GPIO=-1`; optional ~1 kΩ series resistor. Default left at 17 to match PURPOSE. The operator may choose -1 as the default (functionally equivalent per §2.2, but check_log `--tx-gpio` must then be passed) |
| F-14 | Review re-checked IDF `-Wall` exposure (format specifiers, `ESP_GOTO_ON_ERROR` needs a local `ret`, unused symbols) in all project sources and found no issue by reading | Info | V-01 (IDF builds with `-Werror=all`) |
| F-15 | GitHub Actions Build #1 was **blocked before compiling**: "Project Git worktree is dirty; commit changes before building". Every project file is untracked (`?? CMakeLists.txt`, `components/`, `main/`, …). The commits so far (`b1c6a18`, `37f0f80`, `c4a0c6d`) do not contain the firmware. This is a process blocker, not a code defect | High (blocks V-01) | Commit all of `source/` (G12) and re-run the build. Commit `.gitignore` too, so `build/` and `sdkconfig` never make the tree dirty. **Run #3 impl.:** the tree was committed (`65c6ad9`) and the worktree was clean at phase start. Probably resolved for the next build; confirm with a real builder result |
| F-16 | `.gitignore` ignores `*.log`, so bench captures named `*.log` (README examples) will not be committed as evidence | Low (evidence handling) | Store evidence captures as `test/bench/evidence/<check>-<date>.txt`, or `git add -f`. No code change needed |
| F-17 | Builds #2 (2026-10-16) and #3 (2026-10-17) were both blocked at `git push` (HTTP 403) before any compilation. The identical error shows the credentials were not changed between attempts | High (blocks V-01) | Platform/operator credential fix (G14). No source change can resolve it. Re-triggering without changing the credentials will give the same result. **Superseded by F-18 (Build #4)** |
| F-18 | Build #4 (2026-10-17) got past the 403 but was rejected by GitHub: the PAT has no `workflow` scope, and the push includes `.github/workflows/nostromo-build.yml` (commit `c4a0c6d`). Still no compilation | High (blocks V-01) | Operator token-scope fix (G15). No source change can resolve it. The workflow file lives outside `source/` and must not be removed by an agent. **Resolved by Build #5** (push accepted, CI checked out `d587225`) |
| F-19 | Build #5 (2026-10-17, `d587225`): the compile and link of all project components succeeded on IDF v5.4.2. The job then failed in "Package binaries and build evidence": `git rev-parse HEAD` exit 128, "dubious ownership" | High (blocks V-01 completion and artifact upload) | Operator workflow fix (G16). No source change. The truncated log excerpt (from step 960/1004) shows no warnings or errors for project objects. Earlier steps were not supplied, so "no warnings" is unconfirmed |

### 7.3 ESP-IDF APIs used (build-compatibility surface)
`uart_driver_install`, `uart_param_config` (`UART_SCLK_DEFAULT`), `uart_set_pin`,
`uart_read_bytes`, `uart_flush_input`, `uart_driver_delete`, `uart_event_t`
(`UART_DATA`, `UART_FIFO_OVF`, `UART_BUFFER_FULL`, `UART_FRAME_ERR`, `UART_PARITY_ERR`);
`gpio_pullup_en`, `GPIO_IS_VALID_GPIO`, `GPIO_IS_VALID_OUTPUT_GPIO`; `esp_timer_get_time`; `ESP_RETURN_ON_FALSE` / `ESP_RETURN_ON_ERROR` /
`ESP_GOTO_ON_ERROR` (`esp_check.h`); `xTaskCreate`, `xTaskDelayUntil`, `xQueueReceive`,
`xQueueReset`, `taskENTER/EXIT_CRITICAL(portMUX)`; `nvs_flash_init` / `nvs_flash_erase`;
`esp_app_get_description`, `esp_chip_info`, `esp_flash_get_size`, `esp_get_idf_version`.
All of these exist in IDF v5.0+ **[ASSUMPTION, not build-verified]**. `UART_SCLK_DEFAULT`
and `esp_app_desc.h` need v5.0 or later, so the firmware **will not build on v4.x**.

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
| V-11 | `age_ms` of the first `NO_DATA` line ≤ 2000 (upper bound on unplug → detection); warning/recovery messages; recovery without reboot | Replug → `OK` latency (the replug time is not in the log) |
| V-12 | One boot segment; no crash/reset text after the banner; monotonic timestamps; run ≥ 1 h | — |

Verdicts: PASS / FAIL / INCONCLUSIVE (exit 0 / 1 / 3). The script assumes the default log
timestamp format `(<ms>)` (`CONFIG_LOG_TIMESTAMP_SOURCE_RTOS`). It has not been run.

## 8. Telemetry contract (UART0 @115200, 8N1)

Format: standard ESP-IDF log prefix `<L> (<ms since boot>) <tag>: <message>`. The host
uses the `<ms since boot>` field for timing checks (V-05).

**Boot banner** (once per boot, tag `wt`, then `a02yyuw`):
```
I (t) wt: === water_tank fw <ver> (built <date> <time>) ===
I (t) wt: idf=<idf ver> chip_rev=v<maj>.<min> cores=<n> flash=<MB>MB
I (t) wt: sensor=A02YYUW uart=<n> rx_gpio=<n> tx_gpio=<n> baud=9600
I (t) wt: report_period_ms=<n> stale_ms=<n> median_window=<n>
I (t) a02yyuw: UART<n> @9600 8N1, rx=GPIO<n> tx=GPIO<n>, median_window=<n>
```

**Report line** (every `report_period_ms`, tag `wt`, level I):
```
distance_mm=<int|-1> status=<OK|NO_DATA|OUT_OF_RANGE> age_ms=<int|-1> ok=<u32> cks_err=<u32> sync_skip=<u32> uart_ovf=<u32> rx_bytes=<u32> line_err=<u32>
```
`rx_bytes` and `line_err` were added by F-03 (run #2 implementation), at the end of the line
so existing parsers keep working. Future fields must also be appended.

**Event lines:**
- `W wt: sensor data lost: no valid frame for > <stale_ms> ms (check wiring, RX/TX pins)`
- `I wt: sensor data available`
- `W a02yyuw: UART FIFO|ring buffer overflow, input flushed`
- `E a02yyuw: ... not usable ...` / `RX and TX GPIO must differ` (invalid configuration, followed by an abort)

**Host parsing rule:** a line matching `\bwt: distance_mm=` is a report line. Parse the
fields as whitespace-separated `key=value` pairs. Any `Guru Meditation`, `abort()`,
`rst:`, `Task watchdog` or `Brownout detector` text after the boot banner counts as a
failure in V-12. The brownout pattern was added in run #2 because the sensor shares the 3V3
rail. Reference implementation: `test/bench/check_log.py`.
