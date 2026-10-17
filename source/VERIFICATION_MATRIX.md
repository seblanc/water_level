# VERIFICATION MATRIX — Water Tank Level (project 1a95ca931d72)

The ACCEPTANCE spec is empty (revision 1) and there are no structured requirements yet.
The requirements and acceptance criteria below are **derived** from PURPOSE, FLASHING and
TELEMETRY revision 1. They are **proposals that need operator confirmation** (gap G6).

**Only one check has evidence:** V-01, the CI build. Build #8 completed and its artifact
was uploaded, but the "no warnings" sub-criterion is still unconfirmed. Every other result
is `NOT VERIFIED` or `DEFERRED`. Nothing has been flashed, run, or measured, and the host
tests have not been executed. **CI build evidence is compilation evidence only, not
hardware verification.**

Run #4, ARCHITECTURE phase (attempt 1), 2026-10-17: there is no new builder result
(Build #8 is still the latest), and no source files changed. Stage 2 requirements R-08a…R-08f
and checks V-13a…V-13g were derived from the new Stage 2 design (SYSTEM_ARCHITECTURE.md §5).
Proposed criteria AC-7/AC-8 were added. The V-02 procedure now refers to the CI-artifact
flashing steps (§4.1).

## Derived requirements

| ID | Requirement | Source |
|----|-------------|--------|
| R-01 | Firmware builds with ESP-IDF v5.x for target `esp32` | Framework spec |
| R-02 | Firmware can be flashed over USB | FLASHING rev 1 |
| R-03 | Read the A02YYUW over UART on GPIO16/GPIO17 | PURPOSE rev 1 |
| R-04 | Sensor powered from the board's 3V3 | PURPOSE rev 1 (wiring, not firmware) |
| R-05 | Stream the distance on the UART console every second | PURPOSE rev 1, TELEMETRY rev 1 |
| R-06 | Only checksum-valid frames are reported | Derived (data integrity) |
| R-07 | Sensor loss is reported, not silently masked; recovery needs no reboot | Derived (robustness) |
| R-08 | Send data to Home Assistant | PURPOSE rev 1 (Stage 2) |
| R-08a | Without HA configuration, behaviour equals Stage 1 (plus one `ha: disabled` line) | Derived (§5.2) |
| R-08b | The distance entity appears in HA automatically (MQTT discovery) | Derived (§5.4) |
| R-08c | HA shows the distance as unavailable when the sensor has no data or the device is offline | Derived (§5.4, R-07) |
| R-08d | Wi-Fi/broker loss recovers automatically, never reboots the device, and never interrupts the UART stream | Derived (§5.6) |
| R-08e | No credentials in the repository or in CI artifacts | Derived (guardrail, §5.3) |
| R-08f | The 1 Hz UART stream (R-05) stays within tolerance while HA is active | Derived (§3.5) |

## Proposed acceptance criteria, pending operator confirmation

| AC | Criterion | Checks |
|----|-----------|--------|
| AC-1 | Builds without errors; host unit tests pass | V-01, V-09 |
| AC-2 | Flashes over the bench USB port and boots to the banner | V-02 |
| AC-3 | With the sensor connected, a `status=OK` line with a plausible distance appears every 1 s ± 50 ms | V-03, V-05 |
| AC-4 | Reported distance matches a tape measurement within ±(10 mm + 1 %) between 300 and 2000 mm | V-06 |
| AC-5 | Unplugging the sensor shows `NO_DATA` within ≤ 2 s; reconnecting recovers without a reboot | V-11, V-03b |
| AC-6 | Runs ≥ 1 h with no reset, panic, or WDT | V-12 |
| AC-7 (Stage 2) | A provisioned device appears in HA as "Water tank" with a distance entity in mm that updates at least every `WT_HA_PUBLISH_PERIOD_MS` + 1 s | V-13b, V-13c |
| AC-8 (Stage 2) | Network faults (AP off, broker restart) recover without a reboot; AC-3 and AC-6 still hold with HA active | V-13e, V-13f, V-13g |

## Implementation traceability

| Req | Implemented in | State |
|-----|----------------|-------|
| R-01 | `CMakeLists.txt`, `main/CMakeLists.txt`, `components/*/CMakeLists.txt`, `sdkconfig.defaults` | CI-compiled |
| R-02 | Procedure only: `README.md`, SYSTEM_ARCHITECTURE.md §4 / §4.1 | — |
| R-03 | `components/a02yyuw/a02yyuw.c`, `main/Kconfig.projbuild` | CI-compiled |
| R-04 | Wiring only (README.md wiring table) | — |
| R-05 | `main/main.c` `reporter_task`, `components/level_report/*` | CI-compiled |
| R-06 | `components/a02yyuw/a02yyuw_parser.c`; `test/host/test_parser.c` | CI-compiled (tests not run) |
| R-07 | `a02yyuw_get_latest` staleness; `main/main.c` transitions; `a02yyuw.c` filter reset | CI-compiled |
| R-08, R-08a…f | Planned: `components/ha_mqtt/*`, `main/main.c`, Kconfig, `sdkconfig.defaults`, `tools/nvs_config.example.csv`, `test/host/test_ha_payload.c` (DEVELOPMENT_PLAN.md M6a). Groundwork: `level_report_add_sink` | **Not written** |

## Verification checks

Report-line fields and parsing rules are defined in SYSTEM_ARCHITECTURE.md §8.

| Check | Req | Method | Procedure | Pass criterion | Needs | Result |
|-------|-----|--------|-----------|----------------|-------|--------|
| V-01 | R-01 | Build (GitHub Actions) | Committed tree → CI `idf.py -C source set-target esp32 build` | CI job succeeds, `water_tank.bin` produced, no warnings from project sources. Record the IDF version and the image size | CI | **PASS except warning scan** (Build #8, `43e0382`). IDF v5.4.2, image 0x35210 B (Build #5). Warning scan NOT VERIFIED. **Must be re-run once Stage 2 code is added** |
| V-02 | R-02 | Bench | SYSTEM_ARCHITECTURE.md §4.1 (CI artifact, `write_flash @flash_args`) or `idf.py -p /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 flash` | esptool prints "Hash of data verified" for each segment and the board resets into the app. `source-commit.txt` matches the intended commit | Board + USB | NOT VERIFIED |
| V-03 | R-03 | Bench / log (auto: `check_log.py --check v03`) | Sensor connected; capture the monitor output to a file | Banner shows `rx_gpio=16 tx_gpio=17 baud=9600`; within 2 s, `status=OK` lines with `ok=` increasing. On failure, record `rx_bytes`/`line_err` | Board + sensor | NOT VERIFIED |
| V-03b | R-07 | Bench / log (auto: `--check v03b`) | Boot with the sensor **disconnected** | Banner; one `sensor data lost` warning; `status=NO_DATA distance_mm=-1 age_ms=-1 ok=0` lines at 1 Hz; no reboot | Board | NOT VERIFIED |
| V-04 | R-04 | Inspection | Visual wiring check **before first power-up**; multimeter on sensor VCC (optional) | VCC on 3V3, common GND, ~3.3 V; sensor TX (white) on GPIO16, not on GPIO17 (F-13) | Multimeter (not on bench) | NOT VERIFIED |
| V-05 | R-05 | Log timing (auto: `--check v05`) | Capture ≥ 60 s of log | 60 ± 1 report lines; consecutive timestamps 1000 ± 50 ms apart; cumulative drift ≤ 50 ms | Board + log capture | NOT VERIFIED |
| V-06 | R-05 | Bench accuracy | Flat rigid target at 300 / 1000 / 2000 mm (tape-measured), 10 readings each | Mean `distance_mm` within ±(10 mm + 1 %) **[ASSUMPTION: tolerance]** | Tape measure, target | NOT VERIFIED |
| V-07 | R-05 | Bench | Target < 30 mm from the face | `status=OUT_OF_RANGE` or a stable blind-zone value (record which); no crash | Target | NOT VERIFIED |
| V-08 | R-05 | Field | Sensor over the real tank at max depth (G8), 30 s | `status=OK` throughout; spread ≤ ±20 mm **[ASSUMPTION]** | Tank | NOT VERIFIED |
| V-09 | R-06, R-08b | Host unit test | `make -C test/host run` | `<N> checks, 0 failures`, exit 0. After M6a it also covers `test_ha_payload.c` | Host C compiler | NOT VERIFIED |
| V-09b | (tooling) | Host self-test | `python3 test/bench/test_check_log.py` | All cases OK, exit 0. Must pass before any `check_log.py` verdict counts as evidence | Python 3.7+ | NOT VERIFIED |
| V-10 | R-06 | Log (auto: `--check v10`) | Run ≥ 10 min with the sensor connected | `cks_err` < 1 % of `ok` **[ASSUMPTION threshold]**; `uart_ovf` = 0 | Board + sensor | NOT VERIFIED |
| V-11 | R-07 | Fault injection (partly auto: `--check v11`) | Disconnect the sensor TX wire for ~5 s, then reconnect | One `sensor data lost`; `NO_DATA` within ≤ 2 s; `sensor data available` + `OK` within ≤ 2 s of reconnect (manual timing); no reboot | Board + sensor | NOT VERIFIED |
| V-12 | R-01..R-07 | Stability (auto: `--check v12`) | Run ≥ 1 h | No `Guru Meditation`, `abort()`, `Task watchdog`, `Brownout detector`, or new `rst:` after the banner; monotonic timestamps | Board + sensor | NOT VERIFIED |
| V-14 | R-03 | Bench / config | `WT_SENSOR_RX_GPIO=6`, build, flash, observe; then restore 16 | `E a02yyuw: RX GPIO6 is not a usable input ...` then abort/reboot; no silent hang. Optional | Board | NOT VERIFIED |
| V-13a | R-08a, R-08e | Log + repo inspection | Flash a CI image with HA compiled in, NVS not provisioned; run V-03/V-05 | Exactly one `ha: disabled (...)` line; V-03/V-05 verdicts as with Build #8; no `wifi` lines. Repo/artifact grep finds no SSID or password | Board + sensor | DEFERRED (code not written) |
| V-13b | R-08 | Log | Provision NVS (§5.3), reboot | `ha: enabled id=wt_xxxxxx ...`, `wifi connected ip=...`, `mqtt connected` within 30 s | Board, Wi-Fi AP, broker | DEFERRED |
| V-13c | R-08b | HA UI + `mosquitto_sub -v -t 'watertank/#' -t 'homeassistant/#'` | After V-13b | Retained discovery configs present; device "Water tank" with a "Distance" entity (mm) in HA; state updates every ~10 s and matches the UART `distance_mm` | Broker, HA | DEFERRED |
| V-13d | R-08c | Fault injection | Disconnect the sensor TX for ≥ 15 s, then reconnect | `watertank/<id>/sensor` → `offline` within ≤ 3 s (one report period after NO_DATA); the HA entity becomes unavailable; it returns after reconnect | Broker, HA, sensor | DEFERRED |
| V-13e | R-08d | Fault injection | Switch off the AP (or block the device) for 2 min, then restore | `wifi disconnected` + backoff lines; UART report lines continue at 1 Hz throughout; reconnect without reboot; HA marks the device unavailable via the LWT (broker keepalive timeout) and recovers | AP control, broker, HA | DEFERRED |
| V-13f | R-08d | Fault injection | Restart the broker / HA | `mqtt disconnected` → `mqtt connected`; discovery and availability republished; entity recovers without a device reboot | Broker | DEFERRED |
| V-13g | R-08f | Log (auto: `--check v05`, `--check v12`) | Provisioned and connected: capture ≥ 60 s (V-05) and ≥ 1 h (V-12) | Same pass criteria as V-05 / V-12; no `Brownout detector`; `cks_err` rate as in V-10 (G17). Record the `wt_ha` stack high-water mark if logged | Board, sensor, Wi-Fi, broker | DEFERRED |

## Bench constraints affecting verification
- Bench "Simple UART" has one CH340 port for both flashing and logging (`UART @115200`), and
  lists no stimuli, instruments, voltage, or recovery procedure. Physical checks (V-04,
  V-06/V-07/V-08, V-11, V-13d) need manual operator work.
- The sensor link (UART2, 9600) cannot be sniffed on this bench. Sensor-side diagnosis
  relies on the firmware counters.
- Stage 2 checks need a 2.4 GHz AP, an MQTT broker and HA, none of which are listed on the
  bench. `mosquitto_sub` on any LAN host is the recommended way to observe MQTT traffic.
- `check_log.py` verdicts count as evidence only after V-09b passes, and only together with
  the log they were computed from. Store captures as `test/bench/evidence/<check>-<date>.txt`
  (`*.log` is git-ignored, F-16).
- Builds run on GitHub Actions and only see committed files. CI images never contain
  credentials, so Stage 2 checks need NVS provisioning (§5.3) or a local build.
- Opening the serial port can reset the board (DTR/RTS).
- No agent phase has had hardware access.

## Unverified acceptance criteria (all of them)

| AC | State | Blocking item |
|----|-------|---------------|
| AC-1 | NOT VERIFIED (build half passed in CI) | Warning scan (full compile log needed); V-09/V-09b never executed |
| AC-2 | NOT VERIFIED | Hardware (V-02) |
| AC-3 | NOT VERIFIED | Board + sensor + log capture (V-03, V-05); G1 unconfirmed |
| AC-4 | NOT VERIFIED | Tape measure and target (V-06); tolerance is an assumption |
| AC-5 | NOT VERIFIED | Manual unplug/replug (V-11, V-03b) |
| AC-6 | NOT VERIFIED | ≥ 1 h run (V-12) |
| AC-7 | NOT VERIFIED | Stage 2 code not written (M6a); then Wi-Fi + broker + HA (G5) |
| AC-8 | NOT VERIFIED | As AC-7, plus AP/broker fault injection |

All eight criteria are still **proposals** (G6) until the operator confirms them.

## Results log

| Date | Check | Source | Outcome | Evidence |
|------|-------|--------|---------|----------|
| 2026-10-16 | V-01 | Build #1 | **BLOCKED**, not compiled (dirty worktree, F-15) | "Project Git worktree is dirty; commit changes before building" |
| 2026-10-16 | V-01 | Build #2 | **BLOCKED**, not compiled (push 403, F-17) | "Permission to seblanc/water_level.git denied to seblanc … 403" |
| 2026-10-17 | V-01 | Build #3 | **BLOCKED**, same 403 | Same message as #2 |
| 2026-10-17 | V-01 | Build #4 | **BLOCKED**, not compiled (`workflow` scope, F-18) | "refusing to allow a Personal Access Token to create or update workflow … without `workflow` scope" |
| 2026-10-17 | V-01 | Build #5 (`d587225`, `espressif/idf:v5.4.2`) | **Compile + link PASSED; job FAILED** in packaging (F-19) | "[1004/1004] … Project build complete", "water_tank.bin binary size 0x35210 bytes … 0xcadf0 bytes (79%) free", then "detected dubious ownership" → exit 128 |
| 2026-10-17 | V-01 | Build #6 (`b05cc7f`) | Same as #5 | Identical image size; same packaging error |
| 2026-10-17 | V-01 | Build #7 (`d11d329`) | Same as #5 (size not in the excerpt) | Post-build esptool hint, then the same packaging error |
| 2026-10-17 | V-01 | Build #8 (`43e0382`, after operator fix `a45bcc0`) | **CI job COMPLETED**. Passes except the "no warnings" sub-criterion. Compilation evidence only | "Artifact nostromo-firmware-8 has been successfully uploaded! Final size is 32716500 bytes. Artifact ID is 12094526795"; zip SHA256 `5d335266a1c0536bd8af73ace958f953ffdf2a2dd79c0ac0bf3cbdecaba31a3b`; 1360 files |

No other check has been executed.
