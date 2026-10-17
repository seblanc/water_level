# VERIFICATION MATRIX — Water Tank Level (project 1a95ca931d72)

The ACCEPTANCE spec is empty (revision 1) and there are no structured requirements yet.
The requirements and acceptance criteria below are **derived** from PURPOSE, FLASHING and
TELEMETRY revision 1. They are **proposals that need operator confirmation** (gap G6).

**No check below has been executed.** Every result is `NOT VERIFIED` (or `DEFERRED`):
there is no real evidence for any of them. Firmware source and host tests exist. Nothing
has been compiled, flashed, run, or measured. The only analysis done is static review by
reading the code (SYSTEM_ARCHITECTURE.md §7.2).

Run #3, IMPLEMENTATION phase (attempt 5), 2026-10-17: GitHub Actions Build #4 was
**blocked** at `git push` with a new error. The 403 is gone, but GitHub refused the push
because the PAT lacks the `workflow` scope needed to create `.github/workflows/nostromo-build.yml`
(F-18, G15). Nothing was compiled. Source files are unchanged in this phase.

Run #3, IMPLEMENTATION phase (attempt 4), 2026-10-17: GitHub Actions Build #3 was
**blocked** by the same HTTP 403 on `git push` (F-17, G14). The CI credentials were not
changed after Build #2, so further builds will fail the same way until the operator fixes
access. Nothing was compiled. Source files are unchanged in this phase.

Run #3, IMPLEMENTATION phase (attempt 3), 2026-10-16: GitHub Actions Build #2 was
**blocked** when the platform's `git push` to `seblanc/water_level` returned HTTP 403
(permission denied). That is a CI credential/repository-access problem (F-17, G14), not a
source problem. Nothing was compiled. The sources were re-read statically and no change was
needed, so source files are unchanged in this phase.

Run #3, IMPLEMENTATION phase (attempt 2), 2026-10-16: the tree is now committed (`65c6ad9`)
and the worktree was clean at phase start, so the next CI build should compile. No new
builder result exists. The `level_report` sink dispatcher was added (F-08). The R-05 output
format is unchanged, and none of the check procedures change.

Run #3, ARCHITECTURE phase (attempt 1), 2026-10-16: the first builder result
is in the results log: GitHub Actions Build #1 was **blocked** by the dirty worktree, so no
compilation happened (F-15). Earlier, the run #2 review re-read all sources,
host tests, and the log checker. It fixed F-12 (driver component selection by IDF version)
and documented F-13 (contention on GPIO17 if the wiring is swapped). Earlier in run #2,
F-03/F-04/F-05 and the offline log checker `test/bench/check_log.py` were added. The checker
automates the log-based pass criteria marked "auto" below.

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
| R-08 | Send data to Home Assistant | PURPOSE rev 1 (Stage 2, deferred) |

## Proposed acceptance criteria (Stage 1), pending operator confirmation

| AC | Criterion | Checks |
|----|-----------|--------|
| AC-1 | Builds without errors; host unit tests pass | V-01, V-09 |
| AC-2 | Flashes over the bench USB port and boots to the banner | V-02 |
| AC-3 | With the sensor connected, a `status=OK` line with a plausible distance appears every 1 s ± 50 ms | V-03, V-05 |
| AC-4 | Reported distance matches a tape measurement within ±(10 mm + 1 %) between 300 and 2000 mm | V-06 |
| AC-5 | Unplugging the sensor shows `NO_DATA` within ≤ 2 s; reconnecting recovers without a reboot | V-11, V-03b |
| AC-6 | Runs ≥ 1 h with no reset, panic, or WDT | V-12 |

## Implementation traceability (static: code written, not verified)

| Req | Implemented in |
|-----|----------------|
| R-01 | `CMakeLists.txt`, `main/CMakeLists.txt`, `components/*/CMakeLists.txt` (IDF-version-dependent driver requirement, F-12), `sdkconfig.defaults` |
| R-02 | No firmware work needed; procedure in `README.md` and SYSTEM_ARCHITECTURE.md §4 |
| R-03 | `components/a02yyuw/a02yyuw.c` (`a02yyuw_init` with port/GPIO validation, `rx_task`), `main/Kconfig.projbuild` pins |
| R-04 | Wiring only (README.md wiring table) |
| R-05 | `main/main.c` `reporter_task` (`xTaskDelayUntil`), `components/level_report/level_report.c` (sink dispatcher), `components/level_report/level_report_uart.c` (console sink) |
| R-06 | `components/a02yyuw/a02yyuw_parser.c` (checksum + resync); `test/host/test_parser.c`; `rx_bytes`/`line_err` diagnostics in `level_report_uart.c` |
| R-07 | `a02yyuw_get_latest` staleness; `status=NO_DATA` + transition logging in `main/main.c`; filter reset after a gap in `a02yyuw.c` `on_frame` |
| R-08 | Groundwork only: `level_report_add_sink` (Stage 2 sink attaches here). No HA code yet |

## Verification checks

Report-line fields and parsing rules are defined in SYSTEM_ARCHITECTURE.md §8.

| Check | Req | Method | Procedure | Pass criterion | Needs | Result |
|-------|-----|--------|-----------|----------------|-------|--------|
| V-01 | R-01 | Build (GitHub Actions) | Commit `source/`, then the CI build runs (equivalent to `idf.py set-target esp32 && idf.py build`) | CI job succeeds, `water_tank.bin` produced, no warnings from project sources. Record the IDF version (and so which F-12 branch, `driver` or `esp_driver_*`, was exercised) and the artifact list | Clean committed worktree + CI with IDF v5.x | NOT VERIFIED (Builds #1–#4 blocked, see the results log) |
| V-02 | R-02 | Bench | `idf.py -p /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 flash`, or from the CI `build/` artifacts: `python -m esptool --chip esp32 -p <port> write_flash @flash_args` | esptool prints "Hash of data verified" for each segment and the board resets into the app | Board + USB | NOT VERIFIED |
| V-03 | R-03 | Bench / log (auto: `check_log.py --check v03`) | Sensor connected; `idf.py -p <port> monitor`, capture to a file | Banner shows `rx_gpio=16 tx_gpio=17 baud=9600`; within 2 s, `status=OK` lines appear with `ok=` increasing. On failure, record `rx_bytes`/`line_err` (README troubleshooting table) | Board + sensor | NOT VERIFIED |
| V-03b | R-07 | Bench / log (auto: `--check v03b`) | Boot with the sensor **disconnected** | Banner printed; one `sensor data lost` warning; `status=NO_DATA distance_mm=-1 age_ms=-1 ok=0` lines at 1 Hz; no reboot | Board | NOT VERIFIED |
| V-04 | R-04 | Inspection | Visual wiring check **before first power-up**; multimeter on sensor VCC (optional) | VCC on 3V3, common GND, ~3.3 V; sensor TX (white) on GPIO16, not on GPIO17 (F-13) | Multimeter (not listed on bench) | NOT VERIFIED |
| V-05 | R-05 | Log timing (auto: `--check v05`) | Capture ≥ 60 s of log | 60 ± 1 report lines; consecutive log timestamps differ by 1000 ± 50 ms; no cumulative drift (> 50 ms over 60 lines) | Board + host log capture | NOT VERIFIED |
| V-06 | R-05 | Bench accuracy | Flat rigid target at 300 / 1000 / 2000 mm (tape-measured), 10 readings each | Mean `distance_mm` within ±(10 mm + 1 %) **[ASSUMPTION: tolerance; nominal ±(1 cm + 0.3 %)]** | Tape measure, target | NOT VERIFIED |
| V-07 | R-05 | Bench | Target < 30 mm from the face | `status=OUT_OF_RANGE` or a stable blind-zone value (record which); no crash | Target | NOT VERIFIED |
| V-08 | R-05 | Field | Sensor over the real tank at the max expected depth (G8), 30 s | `status=OK` throughout; spread ≤ ±20 mm **[ASSUMPTION]** | Tank or equivalent | NOT VERIFIED |
| V-09 | R-06 | Host unit test | `make -C test/host run` | Prints `<N> checks, 0 failures`, exit 0 | Host C compiler | NOT VERIFIED |
| V-09b | (tooling) | Host self-test | `python3 test/bench/test_check_log.py` | All unittest cases OK, exit 0. Must pass before any `check_log.py` verdict is used as evidence | Python 3.7+ | NOT VERIFIED |
| V-10 | R-06 | Log (auto: `--check v10`) | Run ≥ 10 min with the sensor connected | `cks_err` < 1 % of `ok` **[ASSUMPTION threshold]**; `uart_ovf` = 0 | Board + sensor | NOT VERIFIED |
| V-11 | R-07 | Fault injection (partly auto: `--check v11`) | During a run, disconnect the sensor TX (white) wire for ~5 s, then reconnect | One `sensor data lost` warning; `NO_DATA` within ≤ 2 s of disconnect (auto: `age_ms` of the first `NO_DATA` line ≤ 2000); `sensor data available` + `OK` within ≤ 2 s of reconnect (**manual timing**: the replug moment is not in the log); no reboot | Board + sensor | NOT VERIFIED |
| V-12 | R-01..R-07 | Stability (auto: `--check v12`) | Run ≥ 1 h | No `Guru Meditation`, `abort()`, `Task watchdog`, `Brownout detector`, or new `rst:` after the banner; log timestamps monotonic | Board + sensor | NOT VERIFIED |
| V-14 | R-03 | Bench / config | `menuconfig`: set `WT_SENSOR_RX_GPIO=6` (flash pin), build, flash, observe; then restore 16 | `E a02yyuw: RX GPIO6 is not a usable input ...` followed by abort/reboot. The chip must not hang silently. Optional: only run it if a reflash for recovery is acceptable | Board | NOT VERIFIED |
| V-13 | R-08 | Deferred | Stage 2: HA entity appears via MQTT discovery and updates | Defined in Stage 2 | Wi-Fi, HA, broker | DEFERRED |

## Bench constraints affecting verification
- Bench "Simple UART" has one CH340 port for both flashing and logging (`UART @115200`), and
  lists no stimuli, instruments, voltage, or recovery procedure. The physical-action checks
  need manual operator work: V-04 (multimeter), V-06/V-07/V-08 (target, tape measure, tank),
  and V-11 (unplugging a wire).
- The sensor link (UART2, 9600) cannot be sniffed on this bench without an extra USB-UART
  adapter. Sensor-side diagnosis relies on the firmware counters (`ok`, `cks_err`,
  `sync_skip`, `uart_ovf`, `rx_bytes`, `line_err`).
- `check_log.py` verdicts count as evidence only after V-09b passes, and only together with
  the log excerpt or file they were computed from. `.gitignore` ignores `*.log`, so store
  committed captures as `test/bench/evidence/<check>-<date>.txt` (F-16).
- Builds run on GitHub Actions and only see committed files. Each build request needs a
  clean worktree (F-15).
- Opening the serial port can reset the board (DTR/RTS), so captured logs normally start
  with a fresh boot.
- No agent phase so far has had hardware access. Results must be filled in from real
  executions with log excerpts as evidence.

## Unverified acceptance criteria (all of them)

| AC | State | Blocking item |
|----|-------|---------------|
| AC-1 | NOT VERIFIED | CI Build #1 was blocked (worktree not committed, F-15). Builds #2 and #3 were blocked (git push 403, F-17). Build #4 was blocked because the PAT has no `workflow` scope (F-18, G15). No compilation yet (V-01). No host compiler for V-09/V-09b |
| AC-2 | NOT VERIFIED | No hardware access (V-02) |
| AC-3 | NOT VERIFIED | Needs board + sensor + log capture (V-03, V-05); pin direction G1 unconfirmed |
| AC-4 | NOT VERIFIED | Needs tape measure and target (V-06); tolerance is an assumption |
| AC-5 | NOT VERIFIED | Needs manual unplug/replug (V-11, V-03b) |
| AC-6 | NOT VERIFIED | Needs a ≥ 1 h run (V-12) |

All six criteria are also still **proposals** (G6) until the operator confirms them.

## Results log

| Date | Check | Source | Outcome | Evidence |
|------|-------|--------|---------|----------|
| 2026-10-16 | V-01 | GitHub Actions Build #1 (commit "pending") | **BLOCKED**, not executed. The source was not compiled, so this is neither a pass nor a fail | Builder message: "Project Git worktree is dirty; commit changes before building" |
| 2026-10-16 | V-01 | GitHub Actions Build #2 (commit "pending") | **BLOCKED**, not executed. Push to the CI repository was refused, so the source was not compiled. Neither a pass nor a fail | Builder message: "remote: Permission to seblanc/water_level.git denied to seblanc. ... The requested URL returned error: 403" |
| 2026-10-17 | V-01 | GitHub Actions Build #3 (commit "pending") | **BLOCKED**, not executed. Same 403 on `git push` as Build #2, so the credentials were not changed between builds. Not compiled. Neither a pass nor a fail | Builder message: "remote: Permission to seblanc/water_level.git denied to seblanc. fatal: unable to access 'https://github.com/seblanc/water_level.git/': The requested URL returned error: 403" |
| 2026-10-17 | V-01 | GitHub Actions Build #4 (commit "pending") | **BLOCKED**, not executed. The 403 is gone, but the remote rejected the ref update because the token has no `workflow` scope. Not compiled. Neither a pass nor a fail | Builder message: "! [remote rejected] HEAD -> main (refusing to allow a Personal Access Token to create or update workflow `.github/workflows/nostromo-build.yml` without `workflow` scope)" |

No other check has been executed.
