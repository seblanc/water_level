# VERIFICATION MATRIX — Water Tank Level (project 1a95ca931d72)

The ACCEPTANCE spec is empty (revision 1) and there are no structured requirements yet.
The requirements and acceptance criteria below are **derived** from PURPOSE, FLASHING and
TELEMETRY revision 1. They are **proposals that need operator confirmation** (gap G6).

**Exception (run #9):** `specifications/verification.md` **revision 3** is an
operator-defined pass bar: *"Verify that the ESP32 boots correctly and reports at least 10
valid distance measurements through UART. All measurements must be between 10 and 60 cm,
with no reported communication errors."* It is tracked as R-10 / **AC-VS3** / **V-15**
below. Only its operational reading (SYSTEM_ARCHITECTURE.md §4.2.1) is an interpretation
(G24).

Run #10, IMPLEMENTATION phase (attempt 1), 2026-10-17: operator guidance "do verification".
There is no source change: the firmware is the one Build #12 compiled (`84dfcb1`), and spec
rev 3 is unchanged. Physical verification is scheduled by the orchestrator after this phase.
**No check result changed. Nothing was compiled, flashed or measured in this phase.**

### Run #10: reading a capture against spec rev 3 (for the UART verifier)
Bench "Simple UART": flash and log both use `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0`,
console 115200. Capture ≥ 20 s from reset with HA **not** provisioned. Expected sequence
(full rules: SYSTEM_ARCHITECTURE.md §4.2.1, check V-15):
1. Boot: one banner line with `rx_gpio=16 tx_gpio=17 baud=9600`, then one `ha: disabled (...)`
   line. No `Guru Meditation`/`abort()`/`rst:` after the banner and no `E (` line.
2. Possibly one `NO_DATA` line and one `sensor data lost` before the sensor locks (tolerated,
   F-34). Then one `I a02yyuw: sensor locked after N bytes (acquisition discarded: ...)` line
   (informational; it is not a reported communication error).
3. One `I wt: distance_mm=<d> status=OK ... ok=<n> cks_err=0 sync_skip=<s> uart_ovf=0 ...
   line_err=0` line per second. Units are **mm**, so the spec's 10–60 cm is
   **100 ≤ `distance_mm` ≤ 600**. PASS needs ≥ 10 such lines with increasing `ok=`, all in
   range, and `cks_err`, `line_err` and `uart_ovf` = 0 on every line from the first OK.
   `sync_skip` is not an error counter (§4.2.1).
4. Automated verdict: `python3 test/bench/test_check_log.py` (V-09b) first, then
   `python3 test/bench/check_log.py <capture> --check v15`.

Failure hints: `ok=0` with `rx_bytes=0` points to wiring or pins (G1, F-13). `distance_mm`
outside 100–600 means the target is not at 10–60 cm (G25), not a firmware fault. Non-zero
counters only on a pre-lock line are F-35: look at the `sensor locked` line and repeat once.

Run #9, ARCHITECTURE phase (attempts 1–2), 2026-10-17: spec rev 3 analysed. Static
analysis of `a02yyuw.c` shows the boot-cumulative error counters can record start-up
alignment as `cks_err`/`line_err` (F-33). V-15 could then fail with no real fault.
The fix (counter rebase at first frame) and check `v15` are planned as DEVELOPMENT_PLAN.md
M8a. There is no new builder result: Build #11 is still the latest. **No check result
changed. Nothing was compiled, executed or measured in this phase.**

Run #9, IMPLEMENTATION phase (attempts 3–4): **M8a written**. `a02yyuw.c` rebases
`cks_err`/`sync_skip`/`line_err` at the first valid frame and logs the discarded counts
once; `check_log.py` gained `Log.errors` and `--check v15`; `test_check_log.py` gained
class `V15Test` (2 PASS, 4 FAIL, 1 INCONCLUSIVE case, plus a `CHECKS` membership test).
The tolerated first-OK bound is 3 s, not the 2 s of the architecture draft (§4.2.1).
**Not compiled, not executed.** The next CI build is the first with M8a.

Run #9, REVIEW phase (attempt 6), 2026-10-17: **Build #12 (`84dfcb1`, the current HEAD,
clean worktree) completed**. It is the first build that contains M8a, so the `a02yyuw.c`
counter rebase is now **CI-compiled**. The artifact is `nostromo-firmware-12`, 35631780 B,
1365 files. As before, the excerpt shows only the upload steps, so the image size and the
warning scan are not confirmed. I did a static review of the rebase code against
§4.2.3 and found no defect. `a02yyuw_parser_reset()` keeps the counters, so the
`checksum_err - cks_base` subtraction cannot wrap after a UART overflow. I also found
one residual risk, F-35 (see V-15). It is documented only, with no code change.
`check_log.py` is Python, so CI does not cover it: V-09b and V-15 have **never been
executed**. **No runtime check changed. Nothing was flashed, executed or measured.**

**Only one check has evidence:** V-01, the CI build. Build #9 (`c804544`) is the first
build with Stage 2 (`ha_mqtt`, `CONFIG_WT_HA_ENABLE=y`) compiled in. It completed and its
artifact was uploaded. Builds #10 (`d240242`) and #11 (`9be93a0`) also completed with
the same firmware sources. Build #12 (`84dfcb1`, the latest) adds the M8a counter rebase
and also completed. The "no warnings" sub-criterion and the image size are still
unconfirmed, because the builder excerpt only shows the upload steps. Every other result
is `NOT VERIFIED` or `DEFERRED`. Nothing has been flashed, run, or measured, and the host
tests have not been executed. **CI build evidence is compilation evidence only, not
hardware verification.**

Run #8, REVIEW phase (attempt 3), 2026-10-17: Build #11 (`9be93a0`, the current HEAD) is
recorded below. The commits since Build #10 (`efb1328`, `0d6b28b`, `9be93a0`) changed docs
only, so Build #11 compiled the same firmware sources a third time. It re-confirms V-01 and
adds no new coverage. The artifact is 35629338 B, the same 1365 files. The image size and
the warning scan are still not in the excerpt. I re-read `main/main.c` and found no defect.
V-02/V-13a may now use `nostromo-firmware-11` (expected `source-commit.txt`: `9be93a0…`).
**No source change. No check result changed except the V-01 log entry. Nothing was
executed or measured.**

Run #8, ARCHITECTURE phase (attempt 1), 2026-10-17: there is no new builder result
(Build #10 is still the latest and is already logged below), and no spec, operator or
bench input arrived. Requirements, proposed criteria and checks are unchanged. V-02/V-13a
should use artifact `nostromo-firmware-10` (expected `source-commit.txt`: `d240242…`).
**No check result changed. Nothing was compiled, executed or measured in this phase.**

Run #7, REVIEW phase (attempt 3), 2026-10-17: Build #10 (`d240242`) has been recorded. The
commits since Build #9 changed docs only (run #6 review, run #7 architecture and
implementation notes), so Build #10 compiled the same firmware sources. It re-confirms
V-01, but it does not add new coverage. The artifact is 35629344 B (Build #9: 35629350 B).
The 6 B difference is in the zip, not the app. It is plausibly embedded commit/date text,
but that is not verified. The image size and the warning scan are still not in the
excerpt. I re-read `main/main.c` and found no defect: the init order is NVS → banner →
sensor → `level_report` → HA → reporter, and the first NO_DATA report gives exactly one
`sensor data lost` line (V-03b). **No source change. No check result changed except the
V-01 log entry. Nothing was executed or measured.**

Run #7, ARCHITECTURE phase (attempt 1), 2026-10-17: there is no new builder result
(Build #9 is still the latest), and no source files changed. One **inference**, not a
measurement: IDF fails the build when the app does not fit its partition, so Build #9
passing means the Stage 2 app is ≤ 1.5 MB (SYSTEM_ARCHITECTURE.md §5.8, G23). The exact
size is still unknown. V-13h was added for the optional M7a level-percentage feature
(§5.11, not implemented). **No check result changed. Nothing was compiled, executed or
measured in this phase.**

Run #6, REVIEW phase (attempt 4), 2026-10-17: Build #9 was recorded. I did a static review
of `ha_mqtt.c`, `ha_config.c`, `ha_payload.c`, `main.c`, the Kconfig and the CMake files
against the §5 design, and found no defect that needs a code change. The console sink is
registered before the HA sink, so the §8 UART line stays first and unchanged. The HA paths
contain no abort or restart, and nothing in the HA code calls `ESP_ERROR_CHECK`. The
traceability states below now read "CI-compiled". **No runtime check changed, and nothing
was executed or measured in this phase.**

Run #6, ARCHITECTURE phase (attempt 1), 2026-10-17: there is no new builder result
(Build #8 is still the latest) and the source tree has not changed since `7bac3ea`. A
read-only compile-surface pass (SYSTEM_ARCHITECTURE.md §5.10.4) found no new blocker.
F-31 adds an expectation to V-13e/V-13f: esp-mqtt prints `E` lines every ~10 s while the
broker is down, and these are not failures. **No check result changed. Nothing was
compiled, executed or measured in this phase.**

Run #5, ARCHITECTURE phase (attempt 1), 2026-10-17: there is still no builder result after
Build #8. Commit `7bac3ea` added partial Stage 2 sources (`components/ha_mqtt/*` without a
`CMakeLists.txt`, `test/host/test_ha_payload.c`). They are **not in any build and have never
been compiled or executed**. Traceability for R-08 now points at the written files, and the
V-09 procedure covers both host test binaries. No check result changed.

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
| R-09 (optional) | HA shows the tank level in % from the provisioned empty/full distances | Derived (G8, §5.11). **Not implemented**; only applies if M7a is done |
| R-10 | Boots correctly; ≥ 10 valid distance readings over UART, all 10–60 cm, no reported communication errors | **VERIFICATION rev 3 (operator)**; reading per §4.2.1 (G24) |

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
| **AC-VS3** (operator-defined, not a proposal) | Verification spec rev 3, as quoted above | V-15 (with V-09b for the checker) |

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
| R-08, R-08b, R-08c | `components/ha_mqtt/ha_payload.c` (topics, discovery, availability), `ha_mqtt.c` (connect/announce, sink, publisher), `components/ha_mqtt/CMakeLists.txt`; `test/host/test_ha_payload.c`. Groundwork: `level_report_add_sink` | CI-compiled (Build #9); host test never run |
| R-08a | Runtime gate in `ha_mqtt_start()`; `main.c` hook (`#ifdef CONFIG_WT_HA_ENABLE`), `main/Kconfig.projbuild` "Home Assistant" menu, `sdkconfig.defaults` (`WT_HA_ENABLE=y`, `SINGLE_APP_LARGE`) | CI-compiled (Build #9) |
| R-08d | `ha_mqtt.c` backoff timer (F-25 fix: backoff only advances when the timer starts), esp-mqtt reconnect, no abort/restart | CI-compiled (Build #9) |
| R-08e | `esp_wifi_set_storage(WIFI_STORAGE_RAM)`, password never logged, `.gitignore` entries, placeholder `tools/nvs_config.example.csv`; empty Kconfig credential defaults | CI-compiled (Build #9); repo/artifact grep not done |
| R-08f | Depth-1 overwrite queue + prio-3 publisher in `ha_mqtt.c`; console sink registered first | CI-compiled (Build #9) |
| R-09 | — (planned: DEVELOPMENT_PLAN.md M7a) | Not implemented |
| R-10 | Existing: report line (`main.c`, `level_report_uart.c`), range check (`a02yyuw_distance_in_range`), counters (`a02yyuw.c`). M8a (run #9): counter rebase in `a02yyuw.c`, `check_v15` in `test/bench/check_log.py` | Report path and M8a rebase CI-compiled (Build #12, `84dfcb1`). `check_v15` and its self-tests never run |

## Verification checks

Report-line fields and parsing rules are defined in SYSTEM_ARCHITECTURE.md §8.

| Check | Req | Method | Procedure | Pass criterion | Needs | Result |
|-------|-----|--------|-----------|----------------|-------|--------|
| V-01 | R-01 | Build (GitHub Actions) | Committed tree → CI `idf.py -C source set-target esp32 build` | CI job succeeds, `water_tank.bin` produced, no warnings from project sources. Record the IDF version and the image size | CI | **PASS except warning scan / size** (Build #9, `c804544`, Stage 1 + Stage 2 compiled in; re-confirmed by Builds #10, `d240242`, and #11, `9be93a0`, same sources; Build #12, `84dfcb1`, adds M8a). IDF v5.4.2 assumed (same workflow as Build #5). The Stage 2 image size is not in the excerpt. It is ≤ 1.5 MB by inference (the build passed IDF's size check), but the exact size is NOT VERIFIED (the Stage 1 image was 0x35210 B, Build #5). Warning scan NOT VERIFIED |
| V-02 | R-02 | Bench | SYSTEM_ARCHITECTURE.md §4.1 (CI artifact, `write_flash @flash_args`) or `idf.py -p /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 flash` | esptool prints "Hash of data verified" for each segment and the board resets into the app. `source-commit.txt` matches the intended commit | Board + USB | NOT VERIFIED |
| V-03 | R-03 | Bench / log (auto: `check_log.py --check v03`) | Sensor connected; capture the monitor output to a file | Banner shows `rx_gpio=16 tx_gpio=17 baud=9600`; within 2 s, `status=OK` lines with `ok=` increasing. On failure, record `rx_bytes`/`line_err` | Board + sensor | NOT VERIFIED |
| V-03b | R-07 | Bench / log (auto: `--check v03b`) | Boot with the sensor **disconnected** | Banner; one `sensor data lost` warning; `status=NO_DATA distance_mm=-1 age_ms=-1 ok=0` lines at 1 Hz; no reboot | Board | NOT VERIFIED |
| V-04 | R-04 | Inspection | Visual wiring check **before first power-up**; multimeter on sensor VCC (optional) | VCC on 3V3, common GND, ~3.3 V; sensor TX (white) on GPIO16, not on GPIO17 (F-13) | Multimeter (not on bench) | NOT VERIFIED |
| V-05 | R-05 | Log timing (auto: `--check v05`) | Capture ≥ 60 s of log | 60 ± 1 report lines; consecutive timestamps 1000 ± 50 ms apart; cumulative drift ≤ 50 ms | Board + log capture | NOT VERIFIED |
| V-06 | R-05 | Bench accuracy | Flat rigid target at 300 / 1000 / 2000 mm (tape-measured), 10 readings each | Mean `distance_mm` within ±(10 mm + 1 %) **[ASSUMPTION: tolerance]** | Tape measure, target | NOT VERIFIED |
| V-07 | R-05 | Bench | Target < 30 mm from the face | `status=OUT_OF_RANGE` or a stable blind-zone value (record which); no crash | Target | NOT VERIFIED |
| V-08 | R-05 | Field | Sensor over the real tank at max depth (G8), 30 s | `status=OK` throughout; spread ≤ ±20 mm **[ASSUMPTION]** | Tank | NOT VERIFIED |
| V-09 | R-06, R-08b | Host unit test | `make -C test/host run` | Both `./test_parser` and `./test_ha_payload` print `<N> checks, 0 failures` and exit 0 (the Makefile already builds both; `test_ha_payload.c` exists since `7bac3ea`) | Host C compiler | NOT VERIFIED |
| V-09b | (tooling) | Host self-test | `python3 test/bench/test_check_log.py` | All cases OK, exit 0. Must pass before any `check_log.py` verdict counts as evidence | Python 3.7+ | NOT VERIFIED |
| V-10 | R-06 | Log (auto: `--check v10`) | Run ≥ 10 min with the sensor connected | `cks_err` < 1 % of `ok` **[ASSUMPTION threshold]**; `uart_ovf` = 0 | Board + sensor | NOT VERIFIED |
| V-11 | R-07 | Fault injection (partly auto: `--check v11`) | Disconnect the sensor TX wire for ~5 s, then reconnect | One `sensor data lost`; `NO_DATA` within ≤ 2 s; `sensor data available` + `OK` within ≤ 2 s of reconnect (manual timing); no reboot | Board + sensor | NOT VERIFIED |
| V-12 | R-01..R-07 | Stability (auto: `--check v12`) | Run ≥ 1 h | No `Guru Meditation`, `abort()`, `Task watchdog`, `Brownout detector`, or new `rst:` after the banner; monotonic timestamps | Board + sensor | NOT VERIFIED |
| V-14 | R-03 | Bench / config | `WT_SENSOR_RX_GPIO=6`, build, flash, observe; then restore 16 | `E a02yyuw: RX GPIO6 is not a usable input ...` then abort/reboot; no silent hang. Optional | Board | NOT VERIFIED |
| V-15 | R-10 (AC-VS3) | Bench / log (auto after M8a: `check_log.py --check v15`) | Flat rigid target ~30 cm, square to the beam (or water surface at 10–60 cm). HA **not provisioned** (G24, F-31). Open the monitor (resets the board) and capture ≥ 20 s from reset. Use an artifact built after M8a | One banner with `rx_gpio=16 tx_gpio=17 baud=9600`; no crash text, no `E` line after it. ≥ 10 `status=OK` lines with increasing `ok=`. From the first OK on: every line `OK` with 100 ≤ `distance_mm` ≤ 600; `cks_err=0 line_err=0 uart_ovf=0` on every line; no `sensor data lost` after the first OK. Tolerated: `NO_DATA` lines and one `sensor data lost` before the first OK, which must come ≤ 3 s after the banner (F-34). Full rules: SYSTEM_ARCHITECTURE.md §4.2.1 | Board + sensor + 10–60 cm target (G25) | NOT VERIFIED (M8a CI-compiled in Build #12; use artifact `nostromo-firmware-12`, expected `source-commit.txt` `84dfcb1…`. Checker and self-tests never run. **F-35 residual risk:** before lock the raw counters are published, so if a cold boot gives a leading NO_DATA line (F-34) *and* a pre-lock framing/checksum error, that tolerated line shows `cks_err`/`line_err` > 0 and `check_v15` reports FAIL. On a FAIL, look at the `sensor locked … discarded` line and repeat once. Not changed, because §4.2.1 requires zero counters on every line until the operator decides otherwise (G24)) |
| V-13a | R-08a, R-08e | Log + repo inspection | Flash a CI image with HA compiled in, NVS not provisioned; run V-03/V-05 | Exactly one `ha: disabled (...)` line; V-03/V-05 verdicts as with Build #8; no `wifi` lines. Repo/artifact grep finds no SSID or password | Board + sensor | NOT VERIFIED (ready: the Build #9–#12 artifacts have HA compiled in; use #12) |
| V-13b | R-08 | Log | Provision NVS (§5.3), reboot | `ha: enabled id=wt_xxxxxx ...`, `wifi connected ip=...`, `mqtt connected` within 30 s | Board, Wi-Fi AP, broker | DEFERRED |
| V-13c | R-08b | HA UI + `mosquitto_sub -v -t 'watertank/#' -t 'homeassistant/#'` | After V-13b | Retained discovery configs present; device "Water tank" with a "Distance" entity (mm) in HA; state updates every ~10 s and matches the UART `distance_mm` | Broker, HA | DEFERRED |
| V-13d | R-08c | Fault injection | Disconnect the sensor TX for ≥ 15 s, then reconnect | `watertank/<id>/sensor` → `offline` within ≤ 3 s (one report period after NO_DATA); the HA entity becomes unavailable; it returns after reconnect | Broker, HA, sensor | DEFERRED |
| V-13e | R-08d | Fault injection | Switch off the AP (or block the device) for 2 min, then restore | `wifi disconnected` + backoff lines (periodic esp-mqtt `E` lines are expected, F-31); UART report lines continue at 1 Hz throughout; reconnect without reboot; HA marks the device unavailable via the LWT (broker keepalive timeout) and recovers | AP control, broker, HA | DEFERRED |
| V-13f | R-08d | Fault injection | Restart the broker / HA | `mqtt disconnected` → `mqtt connected`; discovery and availability republished; entity recovers without a device reboot | Broker | DEFERRED |
| V-13g | R-08f | Log (auto: `--check v05`, `--check v12`) | Provisioned and connected: capture ≥ 60 s (V-05) and ≥ 1 h (V-12) | Same pass criteria as V-05 / V-12; no `Brownout detector`; `cks_err` rate as in V-10 (G17). Record the `wt_ha` stack high-water mark if logged | Board, sensor, Wi-Fi, broker | DEFERRED |
| V-13h | R-09 | HA UI + `mosquitto_sub` (only after M7a) | Provision `tank_empty_mm`/`tank_full_mm`, reboot; hold the target at the full and empty distances and at the midpoint | "Level" entity in %; ≈100 / ≈0 / ≈50 (±2 points), clamped outside the range; with the geometry removed, the entity disappears from HA (empty retained config); UART line unchanged | Broker, HA, target | NOT APPLICABLE (not implemented) |

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
| AC-1 | NOT VERIFIED (build half passed in CI, Builds #9–#12) | Warning scan and image size (full compile log needed); V-09/V-09b never executed |
| AC-2 | NOT VERIFIED | Hardware (V-02) |
| AC-3 | NOT VERIFIED | Board + sensor + log capture (V-03, V-05); G1 unconfirmed |
| AC-4 | NOT VERIFIED | Tape measure and target (V-06); tolerance is an assumption |
| AC-5 | NOT VERIFIED | Manual unplug/replug (V-11, V-03b) |
| AC-6 | NOT VERIFIED | ≥ 1 h run (V-12) |
| AC-7 | NOT VERIFIED | Code is CI-compiled (Build #9). Still needs NVS provisioning (§5.3) plus Wi-Fi, a broker and HA (G5) |
| AC-8 | NOT VERIFIED | As AC-7, plus AP/broker fault injection |

| **AC-VS3** | NOT VERIFIED | M8a firmware part CI-compiled (Build #12). Still needs V-09b (host Python) and V-15 on hardware with a 10–60 cm target (G25) |

AC-1…AC-8 are still **proposals** (G6) until the operator confirms them. AC-VS3 is
operator-defined (verification spec rev 3).

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
| 2026-10-17 | V-01 | Build #9 (`c804544`, first build with `ha_mqtt` + `WT_HA_ENABLE=y`) | **CI job COMPLETED**. Compile and link of Stage 2 implied by the uploaded artifact. Warning scan and image size not in the excerpt. Compilation evidence only | "Artifact nostromo-firmware-9 has been successfully uploaded! Final size is 35629350 bytes. Artifact ID is 12103102433"; zip SHA256 `26bb8a9931eafdf442f7ff75f7969cbcddd69702867fecbcf02e93765816044b`; 1365 files |
| 2026-10-17 | V-01 | Build #10 (`d240242`, same firmware sources as #9; docs-only commits since) | **CI job COMPLETED**. Same conclusions as #9. Compilation evidence only | "Artifact nostromo-firmware-10 has been successfully uploaded! Final size is 35629344 bytes. Artifact ID is 12106702118"; zip SHA256 `63b9a2a11ff97faa9c016918a8145cc762ec2cb726d4882ac72d711dbc9f9c30`; 1365 files |
| 2026-10-17 | V-01 | Build #11 (`9be93a0`, same firmware sources as #9/#10; docs-only commits since) | **CI job COMPLETED**. Same conclusions as #9. Compilation evidence only | "Artifact nostromo-firmware-11 has been successfully uploaded! Final size is 35629338 bytes. Artifact ID is 12106614008"; zip SHA256 `d06fa3a455abe3b0a7446a7f6a3be4c86334fe50265b5ed4053cb7113c1bab5f`; 1365 files |
| 2026-10-17 | V-01 | Build #12 (`84dfcb1`, first build with M8a counter rebase in `a02yyuw.c`) | **CI job COMPLETED**. M8a compiles and links (implied by the uploaded artifact). Warning scan and image size not in the excerpt. Compilation evidence only | "Artifact nostromo-firmware-12 has been successfully uploaded! Final size is 35631780 bytes. Artifact ID is 12109515780"; zip SHA256 `a67e6d58e177277aba34d1a78575400ba59f4d5f1fdcce6374aafd4558f29e62`; 1365 files |

No other check has been executed. No build result exists for commits `184d5de`,
`7bac3ea`, `bba5d98`, `84d1412`, `30c9833`, `9ecc02d` or `2c139be` (`9ecc02d` and
`2c139be` changed docs only).
