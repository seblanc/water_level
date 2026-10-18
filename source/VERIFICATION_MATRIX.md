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

Run #24, IMPLEMENTATION phase, 2026-10-18: the run #23 report gave `http 500` without
the cause. When the reply has no `error` field (PHP crash, hosting page), the firmware
logged only `no server message`. Now `web_payload_body_excerpt()` (pure C, host-tested
in `test_body_excerpt`) logs up to 95 characters of the page text, with tags, entities
and `=` removed: `post rejected: http <code>, reply not from post.php (<text>)`. Replies
with an `error` field are logged as before. Not flashed, host tests never run (V-24a/b).

Run #24, REVIEW phase, 2026-10-18: **Build #20 (`4ab482d`) completed** (artifact
`nostromo-firmware-20`, 35959118 B, 1372 files), so the run #24 excerpt code compiles and
links. In the review I traced each `test_body_excerpt` case through the code by hand and
found no mismatch, but the test was not run. One defect was fixed: only the first 319 bytes
of the reply are kept, so a page that opens with a long `<style>` block leaves no text in
the excerpt, and the W line then said `empty body` even though the body was not empty. It
now says `no text in first <n> bytes` in that case and `empty body` only when nothing was
received (`web_report.c`). This fix (commit `cc81c1e`) was compiled by **Build #21**
(artifact `nostromo-firmware-21`, 35959671 B, 1372 files). Use artifact -21 for the next
flash. Not flashed; host tests not run.
Known limit, not changed: a lone `<` in plain text makes the excerpt skip to the next `>`,
or end there if there is none.

Run #18, REVIEW phase, 2026-10-17: operator asked for the hostname `espwatertank` (R-12).
**Build #17 (`080af2e`, current HEAD, clean worktree) completed** (artifact
`nostromo-firmware-17`, 35936529 B, 1372 files), so the change compiles and links. Static
review of `wt_wifi.c`, `main/Kconfig.projbuild`, `sdkconfig.defaults` and README found no
defect:
- `esp_netif_set_hostname()` is called right after `esp_netif_create_default_wifi_sta()`
  and before `esp_wifi_start()`. In IDF 5.x the name is stored on the esp_netif and copied
  into the lwIP netif when the interface starts, so the first DHCP DISCOVER carries option 12
  `espwatertank`. Failure only logs a W line; `CONFIG_LWIP_LOCAL_HOSTNAME="espwatertank"`
  is the fallback.
- 12 characters, within the 32-character limit (`ESP_NETIF_HOSTNAME_MAX_SIZE`).
- The new `starting ssid=..., hostname espwatertank` line has no `hostname=` token (F-36 rule).
- Limitation, not a defect: there is no mDNS responder, so `espwatertank.local` only resolves
  if the router registers DHCP names in its DNS. A local build directory with an old `sdkconfig`
  keeps its old `CONFIG_LWIP_LOCAL_HOSTNAME` value; CI builds start clean.

**No source change in this review. Build #17 has not been flashed; V-18a/V-18b are NOT VERIFIED.**

Run #16, REVIEW phase (attempt 4), 2026-10-17: **Build #16 (`063d391`, current HEAD,
clean worktree) completed** (artifact `nostromo-firmware-16`, 35935147 B, 1372 files). It is
the first build with the run #16 firmware change: `web_report.c` captures the reply body
and counts a post as delivered only when `web_payload_parse_reply()` finds `"ok":true`.
It then logs `web: post ok: http 200, stored as row <id>, <n> rows in table`. That line has no
`key=value` tokens, so the F-36 rule still holds. Static review of `web_report.c`,
`web_payload.c/.h`, `test_web_payload.c`, `post.php`, `db.php`, `api.php` and `index.php`
found no defect that needs a code change:
- I traced the `test_parse_reply` cases by hand against `json_value()`, including
  `{"error":"ok","ok":false}` and negative or non-numeric `id`/`rows`. All give the
  expected result. **The test was not executed.**
- The field contract is unchanged. `post.php` replies `ok`/`id`/`rows`/`level_pct`, which
  matches `web_reply_t`.
- Wave loop: the front path has a 100-unit period and the back path a 200-unit period,
  in a 400-unit viewBox. The SVG is 200 % of the tank width, so `translateX(-50%)` moves it
  200 units, a whole number of periods for both waves. The loop therefore has no seam (on
  paper). Under reduced motion, `animation: none` also overrides the `.back` longhands.
- Residual risk (documented, not changed): the firmware keeps only the first 191 reply
  bytes. If the host prints PHP notices or HTML before the JSON, the `"ok"` key can fall
  outside that window. The firmware would then log `post rejected: http 200 but the reply
  is not from post.php` even though the row was stored. If V-16g shows this, check the
  server error display settings first.
- Doc fix: V-16c expected the pre-run-16 reply. It now expects `id`/`rows`.

**PHP has still never been executed. Build #16 has not been flashed. V-16f/g/h, V-15 and
every runtime check stay NOT VERIFIED.**

Run #14, REVIEW phase (attempt 9), 2026-10-17: **Build #15 (`f3fc197`, current HEAD)
completed** (artifact `nostromo-firmware-15`, 35918416 B, 1372 files). It is the first
build with the F-36 fix: the once-per-boot `sensor locked` line no longer contains
`key=value` tokens, so the platform verifier's `line_err=` sampler only sees the 1 Hz report
lines. UART verification #4 failed only on that line ("Unparseable line_err=0)"). A
static grep found that the checked keys (`distance_mm=`, `status=`, `cks_err=`,
`sync_skip=`, `uart_ovf=`, `line_err=`) now appear in a log format string only in
`level_report_uart.c` (`web_payload.c` builds the POST body, which is never logged).
Build #15 also compiles the attempt-6 `web_report.c` log-rate edit. **No source change in
this attempt. Build #15 has not been flashed. V-15 / UART verification stays NOT VERIFIED
until it is re-run with artifact -15.**

Run #14, REVIEW phase (attempt 6), 2026-10-17: **Build #14 (`eeca1a6`) completed**. It
is the first build with the run #14 sources (`wt_wifi`, `web_report`, credentials and
API key in `sdkconfig.defaults`), so V-16b now passes except the warning scan and image
size. Artifact `nostromo-firmware-14`, 35918562 B, 1372 files. Static review of
`wt_wifi.c`, `web_report.c`, `web_payload.c`, `main.c`, `web/water/*.php` found two
inconsistencies, both fixed after Build #14:
1. `web_report.c` logged `post failed` on every 15 s retry, which contradicts V-16e.
   Both W lines (`post failed`, `post rejected`) are now logged once per outage. **This
   edit is not compiled yet.** The next CI build must cover it. Until then, Build #14 is
   the newest compiled image, and only its log rate differs.
2. The README and V-16c expected `"level_pct":50`, but PHP 7.1+ `json_encode(50.0)`
   prints `50.0`. Doc fix only.

The firmware/PHP field contract (`web_payload_form` ↔ `post.php`) matches. Both
`ha_mqtt` and `web_report` wait for `wt_wifi_is_connected()`, and neither one starts Wi-Fi
itself. **V-15 impact:** run #14 images start Wi-Fi on every boot. So a V-15 capture
now also contains `wt_wifi`/`web` I/W lines. These are not counted as errors, because
only `E` lines fail. If the AP or TLS stack prints an `E` line that is not silenced
(`wifi` is at W level; `HTTP_CLIENT`/`esp-tls*`/`transport_base` are off), V-15 would
FAIL for a network reason. This risk is unverified. If it happens, use the
`nostromo-firmware-12`/`-13` artifact for V-15. **No PHP code has been executed, nothing
was flashed or measured, and no runtime check changed.**

Run #10, IMPLEMENTATION phase (attempt 1), 2026-10-17: operator guidance "do verification".
There is no source change: the firmware is the one Build #12 compiled (`84dfcb1`), and spec
rev 3 is unchanged. Physical verification is scheduled by the orchestrator after this phase.
**No check result changed. Nothing was compiled, flashed or measured in this phase.**

Run #10, REVIEW phase (attempt 2), 2026-10-17: **Build #13 (`80d9fe7`, the current HEAD)
completed**. The only commit since Build #12 is the run #10 implementation note, which
changed docs only. So Build #13 compiled the same firmware sources as #12 and re-confirms
V-01 without adding coverage. The artifact is `nostromo-firmware-13`, 35631779 B, 1365
files. That is 1 B less than #12, plausibly from embedded commit text (not verified). The
image size and the warning scan are still not in the excerpt. Either artifact (-12 or -13)
is valid for V-15. With -13, `source-commit.txt` should read `80d9fe7…`. I re-read
`specifications/verification.md`, which is still rev 3, against `check_v15` in
`test/bench/check_log.py` and found no defect. Bootloader `rst:` text before the banner
is ignored. A second banner, a non-zero `cks_err`/`line_err`/`uart_ovf`, or any `E` line
after the banner gives FAIL. Fewer than 10 counted readings gives INCONCLUSIVE.
**No source change. No hardware or physical-verification result was available to this
phase, so V-15 and every other runtime check stay NOT VERIFIED. Nothing was executed or
measured.**

### Run #10: reading a capture against spec rev 3 (for the UART verifier)
Bench "Simple UART": flash and log both use `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0`,
console 115200. Capture ≥ 20 s from reset with HA **not** provisioned. Expected sequence
(full rules: SYSTEM_ARCHITECTURE.md §4.2.1, check V-15):
1. Boot: one banner line with `rx_gpio=16 tx_gpio=17 baud=9600`, then one `ha: disabled (...)`
   line. No `Guru Meditation`/`abort()`/`rst:` after the banner and no `E (` line.
2. Possibly one `NO_DATA` line and one `sensor data lost` before the sensor locks (tolerated,
   F-34). Then one `I a02yyuw: sensor locked after N bytes; discarded during acquisition: ...` line
   (informational; it is not a reported communication error). Since run #14 attempt 7 it has
   no `key=value` tokens (F-36). Verifiers must not sample it.
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
| R-11 | Join Wi-Fi `weefee`; POST readings to PHP files at `https://randoresto.ch/water` that store them in a database; web page shows the tank level with empty = 30 cm, full = 5 cm (provisional) | **Operator guidance 2026-10-17 (run #14)** |
| R-12 | The ESP32's network hostname is `espwatertank` | **Operator guidance 2026-10-17 (run #18)** |

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
| AC-HOST (run #18) | The router's client list shows the board as `espwatertank` | V-18a, V-18b |

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
| R-11 | `components/wt_wifi/*` (shared STA, backoff 1→60 s), `components/web_report/*` (form POST over HTTPS + CA bundle, 60 s period, 15 s retry), `sdkconfig.defaults` (SSID/password, URL, API key), `main/main.c`; server: `web/water/*.php`, `.htaccess`; `test/host/test_web_payload.c` | Firmware CI-compiled (Build #15, `f3fc197`, includes all run #14 edits; Build #16, `063d391`, adds the run #16 reply parsing); PHP never executed (no PHP runtime in any phase); host test never run |
| R-10 | Existing: report line (`main.c`, `level_report_uart.c`), range check (`a02yyuw_distance_in_range`), counters (`a02yyuw.c`). M8a (run #9): counter rebase in `a02yyuw.c`, `check_v15` in `test/bench/check_log.py` | Report path and M8a rebase CI-compiled (Build #12, `84dfcb1`). `check_v15` and its self-tests never run |
| R-12 | `components/wt_wifi/wt_wifi.c` (`esp_netif_set_hostname` before `esp_wifi_start`), `main/Kconfig.projbuild` (`WT_WIFI_HOSTNAME`), `sdkconfig.defaults` (`CONFIG_WT_WIFI_HOSTNAME`, `CONFIG_LWIP_LOCAL_HOSTNAME`), README config table | CI-compiled (Build #17, `080af2e`); not flashed |

## Verification checks

Report-line fields and parsing rules are defined in SYSTEM_ARCHITECTURE.md §8.

| Check | Req | Method | Procedure | Pass criterion | Needs | Result |
|-------|-----|--------|-----------|----------------|-------|--------|
| V-01 | R-01 | Build (GitHub Actions) | Committed tree → CI `idf.py -C source set-target esp32 build` | CI job succeeds, `water_tank.bin` produced, no warnings from project sources. Record the IDF version and the image size | CI | **PASS except warning scan / size** (Build #9, `c804544`, Stage 1 + Stage 2 compiled in; re-confirmed by Builds #10, `d240242`, and #11, `9be93a0`, same sources; Build #12, `84dfcb1`, adds M8a; Build #13, `80d9fe7`, same sources as #12). IDF v5.4.2 assumed (same workflow as Build #5). The Stage 2 image size is not in the excerpt. It is ≤ 1.5 MB by inference (the build passed IDF's size check), but the exact size is NOT VERIFIED (the Stage 1 image was 0x35210 B, Build #5). Warning scan NOT VERIFIED |
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
| V-15 | R-10 (AC-VS3) | Bench / log (auto after M8a: `check_log.py --check v15`) | Flat rigid target ~30 cm, square to the beam (or water surface at 10–60 cm). HA **not provisioned** (G24, F-31). Open the monitor (resets the board) and capture ≥ 20 s from reset. Use an artifact built after M8a | One banner with `rx_gpio=16 tx_gpio=17 baud=9600`; no crash text, no `E` line after it. ≥ 10 `status=OK` lines with increasing `ok=`. From the first OK on: every line `OK` with 100 ≤ `distance_mm` ≤ 600; `cks_err=0 line_err=0 uart_ovf=0` on every line; no `sensor data lost` after the first OK. Tolerated: `NO_DATA` lines and one `sensor data lost` before the first OK, which must come ≤ 3 s after the banner (F-34). Full rules: SYSTEM_ARCHITECTURE.md §4.2.1 | Board + sensor + 10–60 cm target (G25) | NOT VERIFIED (platform UART verification #4 failed only on the F-36 log-format issue; re-run with `nostromo-firmware-15`, `source-commit.txt` `f3fc197…`, which contains the fix. Builds #12/#13 predate the F-36 fix and print the lock-line tokens. Checker and self-tests never run. **F-35 residual risk:** before lock the raw counters are published, so if a cold boot gives a leading NO_DATA line (F-34) *and* a pre-lock framing/checksum error, that tolerated line shows `cks_err`/`line_err` > 0 and `check_v15` reports FAIL. On a FAIL, look at the `sensor locked … discarded` line and repeat once. Not changed, because §4.2.1 requires zero counters on every line until the operator decides otherwise (G24)) |
| V-13a | R-08a, R-08e | Log + repo inspection | Flash a CI image with HA compiled in, NVS not provisioned; run V-03/V-05 | Exactly one `ha: disabled (...)` line; V-03/V-05 verdicts as with Build #8; no `wifi` lines. Repo/artifact grep finds no SSID or password | Board + sensor | NOT VERIFIED (ready: the Build #9–#12 artifacts have HA compiled in; use #12) |
| V-13b | R-08 | Log | Provision NVS (§5.3), reboot | `ha: enabled id=wt_xxxxxx ...`, `wifi connected ip=...`, `mqtt connected` within 30 s | Board, Wi-Fi AP, broker | DEFERRED |
| V-13c | R-08b | HA UI + `mosquitto_sub -v -t 'watertank/#' -t 'homeassistant/#'` | After V-13b | Retained discovery configs present; device "Water tank" with a "Distance" entity (mm) in HA; state updates every ~10 s and matches the UART `distance_mm` | Broker, HA | DEFERRED |
| V-13d | R-08c | Fault injection | Disconnect the sensor TX for ≥ 15 s, then reconnect | `watertank/<id>/sensor` → `offline` within ≤ 3 s (one report period after NO_DATA); the HA entity becomes unavailable; it returns after reconnect | Broker, HA, sensor | DEFERRED |
| V-13e | R-08d | Fault injection | Switch off the AP (or block the device) for 2 min, then restore | `wifi disconnected` + backoff lines (periodic esp-mqtt `E` lines are expected, F-31); UART report lines continue at 1 Hz throughout; reconnect without reboot; HA marks the device unavailable via the LWT (broker keepalive timeout) and recovers | AP control, broker, HA | DEFERRED |
| V-13f | R-08d | Fault injection | Restart the broker / HA | `mqtt disconnected` → `mqtt connected`; discovery and availability republished; entity recovers without a device reboot | Broker | DEFERRED |
| V-13g | R-08f | Log (auto: `--check v05`, `--check v12`) | Provisioned and connected: capture ≥ 60 s (V-05) and ≥ 1 h (V-12) | Same pass criteria as V-05 / V-12; no `Brownout detector`; `cks_err` rate as in V-10 (G17). Record the `wt_ha` stack high-water mark if logged | Board, sensor, Wi-Fi, broker | DEFERRED |
| V-16a | R-11 (run #14) | Host unit test | `make -C test/host run` | `./test_web_payload` prints `<N> checks, 0 failures` and exits 0 | Host C compiler | NOT VERIFIED (never run) |
| V-16b | R-11 | CI build | Committed run #14 tree (`e600560` or later) → CI | Build succeeds with `WT_WEB_ENABLE=y`, `wt_wifi`, `esp_http_client` and the certificate bundle; image fits the 1.5 MB app partition | CI | **PASS except warning scan / size** (Build #14, `eeca1a6`, and Build #15, `f3fc197`: CI jobs completed and artifacts uploaded, so compile, link and the partition size check passed. Build #15 includes the run #14 review edits to `web_report.c` and `a02yyuw.c`; Build #16, `063d391`, adds the run #16 reply parsing; Build #18, `1cbfc95`, adds the run #20 readback check. Exact image size and warnings not in the excerpts) |
| V-16c | R-11 | Server | Upload `web/water/`, set `db_dsn`/credentials in `config.php`, run the `curl` test in `web/water/README.md` | `{"ok":true,"id":<n>,"rows":<n>,"level_pct":50.0}` since run #16 (`50` on PHP < 7.1); wrong key → 401; the page shows the reading | Web host, DB | NOT VERIFIED (operator action) |
| V-16d | R-11 | Log + server | Flash a run #14 image; capture ≥ 2 min | `web: enabled … key=set`, Wi-Fi gets an IP on `weefee`, `web: post ok (http 200)` within 60 s of the IP; a new row about every 60 s; the page level matches (30 − distance_cm)/25 × 100 %. 1 Hz UART lines unchanged (V-05 still passes) | Board, sensor, AP, server | NOT VERIFIED |
| V-16e | R-11 | Fault injection | Stop the AP or rename `post.php` for 3 min, then restore | One `post failed`/`post rejected` W line per outage start (not one per retry); no `E` lines from HTTP/TLS; posts resume without a reboot; UART report continues at 1 Hz | AP / server control | NOT VERIFIED |
| V-16f | R-11 (run #16) | Host unit test | `make -C test/host run` | `test_parse_reply` checks pass: `"ok":true` accepted with/without `id`/`rows`; `"ok":false`, HTML pages, empty/NULL bodies rejected | Host C compiler | NOT VERIFIED (never run) |
| V-16g | R-11 (run #16) | Log + server | Upload the run #16 `web/water/`, flash a run #16 image, capture ≥ 3 min | `web: post ok: http 200, stored as row <id>, <n> rows in table`, about one per 60 s, `<id>` increasing; page chart header "· N readings received" grows by about 1 per minute; a 200 page that is not post.php's JSON gives `post rejected: http 200 but the reply is not from post.php` (run #24 and later: `post rejected: http 200, reply not from post.php (<text>)`, see V-24b) | Board, sensor, AP, server | NOT VERIFIED |
| V-16h | Operator (run #16) | Browser | Open `index.php` at several levels, and with reduced motion enabled in the OS | Water surface is two drifting sine waves with no seam or jump at the loop point, no rotating ellipses; bubbles rise; stale/no data → grey water with frozen waves; reduced motion → no animation | Server, browser | NOT VERIFIED |
| V-20a | R-11 (run #20) | Host unit test | `make -C test/host run` | `test_parse_reply` / `test_check_stored` pass: echo parsed (`stored`, `distance_mm` incl. `null`, `uptime_s`, sanitised `error`); matching echo → CONFIRMED, differing/missing echo → MISMATCH, old reply → UNCONFIRMED, `ok:false`/HTML → FAILED | Host C compiler | NOT VERIFIED (never run) |
| V-20b | R-11 (run #20) | Server | Upload the run #20 `web/water/post.php`, run the `curl` test in `web/water/README.md` | Reply contains `"stored":true`, the new `id`, and `distance_mm`/`uptime_s` equal to the request (`distance_mm":null` for `status=NO_DATA`) | Web host, DB | NOT VERIFIED (operator action) |
| V-20c | R-11 (run #20) | Log + server | Run #20 `post.php` uploaded; flash a run #20 image; capture ≥ 3 min | `web: post ok: http 200, row <id> stored and read back (<d> mm), <n> rows in table` about once per 60 s, `<d>` close to the 1 Hz `distance` values; with the old `post.php` still uploaded: `stored as row <id> (not read back, old post.php)`; wrong key → `post rejected: http 401 (bad key)` | Board, sensor, AP, server | NOT VERIFIED (firmware CI-compiled in Build #18, `nostromo-firmware-18`; not flashed; `post.php` not uploaded) |
| V-23a | R-11 (run #23) | Server + log | Upload the run #23 `web/water/post.php` (keep the server's edited `config.php`); flash the next build; capture ≥ 3 min | Either V-20c's `stored and read back` line, or `post rejected: http 500 (<error>)` with a specific cause from the `web/water/README.md` table (not the bare `database`/`readback` of run #20) | Board, AP, server | NOT VERIFIED (PHP never run; firmware unchanged since Build #18; Build #19 artifact equivalent) |
| V-24a | R-11 (run #24) | Host unit test | `make -C test/host run` | `test_body_excerpt` passes: HTML tags, `<style>`/`<script>` content and `&entities;` removed, `=` never output, blanks collapsed and trimmed, truncation to `size - 1`, NULL/empty handling | Host C compiler | NOT VERIFIED (never run) |
| V-24b | R-11 (run #24) | Log + server | Flash the run #24 build; with a non-JSON reply (e.g. rename `config.php` so PHP dies, or point `web_url` at a static page) capture one post | One W line `web: post rejected: http <code>, reply not from post.php (<text>)` where `<text>` is readable page text (no tags, no `=`), or `no text in first <n> bytes` / `empty body`; replies with an `error` field still give `post rejected: http <code> (<error>)` | Board, AP, server | NOT VERIFIED (excerpt compiled in Build #20; review fix for `no text in first <n> bytes` not compiled; never flashed) |
| V-18a | R-12 (run #18) | Log | Flash `nostromo-firmware-17` (or later); capture from reset until Wi-Fi gets an IP | `wt_wifi` I line `starting ssid=weefee, hostname espwatertank`; no `hostname espwatertank rejected` W line; IP obtained. 1 Hz report lines unchanged | Board, AP | NOT VERIFIED |
| V-18b | R-12 (run #18) | Router / LAN | After V-18a: check the router's DHCP client list; optionally `nslookup espwatertank` / `ping espwatertank` from a LAN host | Client list shows `espwatertank` with the IP from V-18a. Name resolution works only if the router publishes DHCP names; `espwatertank.local` is **not** expected to work (no mDNS) | Router admin access | NOT VERIFIED (operator action) |
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
- Builds run on GitHub Actions and only see committed files. Since run #14, CI images
  contain the Wi-Fi credentials and the web API key from `sdkconfig.defaults` (operator
  request), so Wi-Fi starts on every boot. V-13a ("no `wifi` lines", "no SSID in the repo")
  no longer applies to run #14 images. MQTT still needs NVS provisioning (§5.3).
- Opening the serial port can reset the board (DTR/RTS).
- No agent phase has had hardware access.

## Unverified acceptance criteria (all of them)

| AC | State | Blocking item |
|----|-------|---------------|
| AC-1 | NOT VERIFIED (build half passed in CI, Builds #9–#13) | Warning scan and image size (full compile log needed); V-09/V-09b never executed |
| AC-2 | NOT VERIFIED | Hardware (V-02) |
| AC-3 | NOT VERIFIED | Board + sensor + log capture (V-03, V-05); G1 unconfirmed |
| AC-4 | NOT VERIFIED | Tape measure and target (V-06); tolerance is an assumption |
| AC-5 | NOT VERIFIED | Manual unplug/replug (V-11, V-03b) |
| AC-6 | NOT VERIFIED | ≥ 1 h run (V-12) |
| AC-7 | NOT VERIFIED | Code is CI-compiled (Build #9). Still needs NVS provisioning (§5.3) plus Wi-Fi, a broker and HA (G5) |
| AC-8 | NOT VERIFIED | As AC-7, plus AP/broker fault injection |

| AC-WEB (run #14) | NOT VERIFIED | V-16b passed in CI (Builds #14, #15, #16) except warning scan/size. V-16a, V-16c–V-16h: no host test, server deployment or hardware run yet; tank limits 30/5 cm are provisional |
| AC-HOST (run #18) | NOT VERIFIED | CI-compiled (Build #17). Needs a flash plus V-18a/V-18b on the operator's network |
| AC-STORE (run #20) | NOT VERIFIED | Firmware CI-compiled (Build #18, `1cbfc95`). Needs the run #20 `post.php` uploaded (V-20b), a flash of `nostromo-firmware-18` and V-20c; host test V-20a never run |
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
| 2026-10-17 | V-01 | Build #13 (`80d9fe7`, same firmware sources as #12; docs-only commit since) | **CI job COMPLETED**. Same conclusions as #12. Compilation evidence only | "Artifact nostromo-firmware-13 has been successfully uploaded! Final size is 35631779 bytes. Artifact ID is 12111696880"; zip SHA256 `2d2177e2989880c24995e4ed42258d0b3499f053a9ce4baa43699688a2368c8e`; 1365 files |
| 2026-10-17 | V-01, V-16b | Build #14 (`eeca1a6`, first build with `wt_wifi`, `web_report`, `WT_WEB_ENABLE=y` and the CA bundle) | **CI job COMPLETED**. Run #14 firmware compiles, links and fits the app partition (implied by the uploaded artifact). Warning scan and image size not in the excerpt. Compilation evidence only | "Artifact nostromo-firmware-14 has been successfully uploaded! Final size is 35918562 bytes. Artifact ID is 12113824428"; zip SHA256 `a04121ee5cfd978147ff184bbd6109fac50ab3818de1b3933ea59ac2d942579c`; 1372 files |
| 2026-10-17 | Platform UART verification #4 (Flash #2; the image is not named in the report, most likely Build #14) | Hardware, 12 sample lines, 4402 bytes | **FAILED**. Only `line_err equals 0` failed, with "Unparseable line_err=0)". The status, distance, `cks_err`, `uart_ovf` and `sync_skip` checks passed on 11–12 samples. The fault is in the log format (F-36), not in the sensor link | Evidence SHA-256 `74bddc02f2fbb5718b1a904b42cc7657ee95540ce3aa3d95bf8f081b51d91812`; 45 decode replacements (boot ROM noise). Fixed in source at run #14 attempt 7; compiled in Build #15, not reflashed yet |
| 2026-10-17 | V-01, V-16b | Build #15 (`f3fc197`, first build with the F-36 lock-line rewording and the `web_report.c` once-per-outage W lines) | **CI job COMPLETED**. Compile, link and partition fit implied by the uploaded artifact. Warning scan and image size not in the excerpt. Compilation evidence only; F-36 fix **not yet verified on hardware** | "Artifact nostromo-firmware-15 has been successfully uploaded! Final size is 35918416 bytes. Artifact ID is 12114311601"; zip SHA256 `b008855877b824998e6b0242bb04865a68f20e1a89f426657735f81633007f93`; 1372 files |
| 2026-10-17 | V-01, V-16b | Build #16 (`063d391`, first build with the run #16 `web_payload_parse_reply()` and the `post ok … stored as row` log line) | **CI job COMPLETED**. Compile, link and partition fit implied by the uploaded artifact. Warning scan and image size not in the excerpt. Compilation evidence only; not flashed. The run #16 PHP/page changes are not part of the firmware and are not covered by CI | "Artifact nostromo-firmware-16 has been successfully uploaded! Final size is 35935147 bytes. Artifact ID is 12115176256"; zip SHA256 `01887bed0c432860a944e3d2cfa5a041e81cdd8a182081c763b4075979124402`; 1372 files |
| 2026-10-17 | V-01, V-16b | Build #17 (`080af2e`, first build with the run #18 hostname `espwatertank`) | **CI job COMPLETED**. Compile, link and partition fit implied by the uploaded artifact. Warning scan and image size not in the excerpt. Compilation evidence only; not flashed, hostname not observed on any network | "Artifact nostromo-firmware-17 has been successfully uploaded! Final size is 35936529 bytes. Artifact ID is 12115899534"; zip SHA256 `38d60441f08a713bfb8e4bbc978f5d07e17b20fd39538b6c775348fe20a68514`; 1372 files |
| 2026-10-17 | V-01, V-16b | Build #18 (`1cbfc95`, first build with the run #20 `web_payload_check_stored()` and the `stored and read back` log line) | **CI job COMPLETED**. Compile, link and partition fit implied by the uploaded artifact. Warning scan and image size not in the excerpt. Compilation evidence only; not flashed. The run #20 `post.php` is not part of the firmware, is not covered by CI and has never been run | "Artifact nostromo-firmware-18 has been successfully uploaded! Final size is 35947417 bytes. Artifact ID is 12118095362"; zip SHA256 `dcc6f9640e6553f14a9bc5749cf1d0f65751500d091a308efcf51e933fca025a`; 1372 files |
| 2026-10-18 | Platform acceptance run (run #22/#23 objective "reflash and perform the tests"; image not named in the report) | Hardware UART + manual checks | **FAILED**. `uart_http`: "Device reported HTTP statuses: [500]". The other 11 web/server checks were `not_verified` (they need server logs, a DB query or a browser). The report does not include the `error` text from the device's `post rejected` line, so the 500 cause (config, DB access, schema or readback) is unknown | Run #23 response: `post.php` now names the failing stage, SQLSTATE and driver code, or the readback field, in the 500 reply, and returns `server not configured` for a placeholder `config.php`. A failed prune no longer turns a stored row into a 500. Server upload is an operator action |
| 2026-10-18 | V-01 | Build #19 (`1b8def9`, run #23; firmware sources unchanged since Build #18, only `web/water/` PHP and docs changed) | **CI job COMPLETED**. Same firmware conclusions as #18. Compilation evidence only; not flashed. CI does not run or deploy PHP, so the run #23 `post.php` is still unexecuted | "Artifact nostromo-firmware-19 has been successfully uploaded! Final size is 35947412 bytes. Artifact ID is 12135321073"; zip SHA256 `51bd428f360045855a1d2d1116b60e3430b12e5fe6ca5ac5ca5fd9d7940ccc1e`; 1372 files |
| 2026-10-18 | V-01 | Build #20 (`4ab482d`, first build with run #24 `web_payload_body_excerpt()` and the `reply not from post.php (<text>)` W line) | **CI job COMPLETED**. Compile, link and partition fit implied by the uploaded artifact. Warning scan and image size not in the excerpt. Compilation evidence only; not flashed; host tests not run by CI. The run #24 review fix in `web_report.c` came later and is not in this build | "Artifact nostromo-firmware-20 has been successfully uploaded! Final size is 35959118 bytes. Artifact ID is 12135461177"; zip SHA256 `adfe706b0aa2429eb0dd3d2813cd6d9e09a8d9354f840ee966e557009de08095`; 1372 files |
| 2026-10-18 | V-01 | Build #21 (`cc81c1e`, adds the run #24 review fix: `no text in first <n> bytes` instead of `empty body`) | **CI job COMPLETED**. Compile, link and partition fit implied by the uploaded artifact. Warning scan and image size not in the excerpt. Compilation evidence only; not flashed; host tests not run by CI | "Artifact nostromo-firmware-21 has been successfully uploaded! Final size is 35959671 bytes. Artifact ID is 12135916546"; zip SHA256 `c666617e67233a51a606722bb6e5d431300b071355bf168127e777baf8ebd923`; 1372 files |

No other check has been executed. No build result exists for commits `184d5de`,
`7bac3ea`, `bba5d98`, `84d1412`, `30c9833`, `9ecc02d` or `2c139be` (`9ecc02d` and
`2c139be` changed docs only).
