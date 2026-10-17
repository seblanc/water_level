# DEVELOPMENT PLAN — Water Tank Level (project 1a95ca931d72)

See SYSTEM_ARCHITECTURE.md for the design and VERIFICATION_MATRIX.md for the checks.
Status markers:
- `[x]` done and observable in this repo (for code this means **written**, not compiled or run)
- `[ ]` not done

Last updated: run #3, IMPLEMENTATION phase (attempt 5), 2026-10-17.

## Milestones

### M0 — Architecture
- [x] SYSTEM_ARCHITECTURE.md: hardware, pin map, modules, mapping, Stage 2 outlook, gaps (run #1)
- [x] Docs aligned with the implemented code; §7 static review and §8 telemetry contract added (run #2)
- [x] VERIFICATION_MATRIX.md: derived requirements, proposed acceptance criteria, checks
- [x] Run #3: first builder result recorded (Build #1 blocked by the dirty worktree, F-15); CI build and CI-artifact flashing mapped (SYSTEM_ARCHITECTURE.md §4, G12/G13)
- [ ] Operator confirms or corrects G1 (pin direction), G2 (module type), G6 (acceptance criteria AC-1…AC-6)

### M1 — Project skeleton (written in run #1)
- [x] `CMakeLists.txt` (`project(water_tank)`, `PROJECT_VER 0.1.0`), `main/CMakeLists.txt`, `.gitignore`
- [x] `sdkconfig.defaults`: target `esp32`, 4 MB flash, console UART0 @115200, INFO log level, WT_* defaults
- [x] `main/Kconfig.projbuild`: `WT_SENSOR_UART_NUM`, `WT_SENSOR_RX_GPIO`, `WT_SENSOR_TX_GPIO`, `WT_REPORT_PERIOD_MS`, `WT_SENSOR_STALE_MS`, `WT_MEDIAN_FILTER`, `WT_MEDIAN_WINDOW`
- [x] Boot banner, README.md

### M2 — Parser and filter, pure C (written in run #1)
- [x] `a02yyuw_parser.{h,c}`, `a02yyuw_filter.{h,c}`
- [x] `test/host/test_parser.c` + `Makefile`. Test vector checksums checked by hand in run #2 (F-02). **Never executed**

### M3 — Sensor driver (written in run #1)
- [x] UART2 9600 8N1, event-queue RX task, overflow and line-error handling, spinlock-shared sample/stats, staleness, optional median

### M4 — 1 Hz UART streaming, Stage 1 goal (written in run #1)
- [x] `level_report` console sink, fixed-rate reporter task, NO_DATA transition logging

### M4b — Static-review fixes (written in run #2 IMPLEMENTATION, attempt 3; not compiled)
- [x] F-03: `rx_bytes=<u32> line_err=<u32>` appended to the report line (`level_reading_t`, `reporter_task`, `level_report_uart.c`, README, §8)
- [x] F-04: `# CONFIG_LOG_COLORS is not set` in `sdkconfig.defaults`
- [x] F-05: `a02yyuw_init` rejects bad UART ports, invalid/flash GPIOs (6–11), output-incapable TX pins, and RX == TX. Kconfig help updated
- [x] `test/bench/check_log.py` (V-03, V-03b, V-05, V-10, V-11, V-12) + `test/bench/test_check_log.py` self-tests on synthetic logs. **Never executed** (V-09b)
- [x] README troubleshooting table based on the counters

### M4c — Review (run #2 REVIEW, attempt 4; static only, not compiled)
- [x] Re-read all firmware sources, CMake/Kconfig/sdkconfig, host tests, and the log checker + self-tests
- [x] F-12: `components/a02yyuw/CMakeLists.txt` selects `driver` (< IDF v5.3) or `esp_driver_uart`/`esp_driver_gpio` (≥ v5.3)
- [x] F-13: swapped-wiring contention on GPIO17 documented (README caution, Kconfig help, V-04 criterion, G1)
- [x] VERIFICATION_MATRIX.md results set to `NOT VERIFIED`; table of unverified acceptance criteria added
- [ ] Operator decision: keep `WT_SENSOR_TX_GPIO=17` (current) or default it to -1 (F-13)

### M4d — Run #3 implementation (attempt 2; static only, not compiled)
- [x] Confirmed from git status that the tree is committed (`65c6ad9`) and the worktree was clean at phase start. F-15 should no longer block CI; the next builder result must confirm it
- [x] Static re-read of all firmware sources, CMake files and host tests: no defects found
- [x] F-08: `level_report` sink dispatcher (`level_report.c`, `level_report_add_sink`, `LEVEL_REPORT_MAX_SINKS`). The console sink moved behind it with identical output

### M4e — Run #3 implementation (attempt 3; static only, not compiled)
- [x] Recorded Build #2: **blocked** by a `git push` 403 to `seblanc/water_level` (F-17). Platform/operator issue, no compilation happened
- [x] Static re-read of all firmware sources, headers, CMake/Kconfig and sdkconfig.defaults: no defects found, no source changes
- [ ] Operator: give the CI credentials (PAT/deploy key/GitHub App) **write** access to `seblanc/water_level`, or point the backend at a repository it can push to (G14). Then re-trigger the build

### M4f — Run #3 implementation (attempt 4; documentation only)
- [x] Recorded Build #3: **blocked** by the same `git push` 403 as Build #2 (F-17). Nothing was compiled, and no source files changed
- [ ] Operator (same item as M4e, still open): fix CI write access to `seblanc/water_level`. Check in this order: (1) a fine-grained PAT needs `seblanc/water_level` in "Repository access" and "Contents: Read and write"; (2) a classic PAT needs the `repo` scope; (3) the token must not be expired or revoked; (4) branch protection on `main` must allow the push. Then re-trigger the build

### M4g — Run #3 implementation (attempt 5; documentation only)
- [x] Recorded Build #4: **blocked** at `git push`, but with a new error. The 403 is gone (M4e/M4f write access appears fixed). GitHub now refuses the push because the PAT has no `workflow` scope and the history contains `.github/workflows/nostromo-build.yml` (F-18, G15). Nothing was compiled, and no source files changed
- [ ] Operator: give the CI token workflow rights. Classic PAT: tick the `workflow` scope. Fine-grained PAT: "Workflows: Read and write". Alternatively, push the current history once with a credential that has workflow rights. Then re-trigger the build

### M4h — Run #3 implementation (attempt 6; documentation only)
- [x] Recorded Build #5 (`d587225`, IDF v5.4.2): **compile + link succeeded** (`water_tank.bin` 0x35210 bytes, 79 % of the app partition free). The `workflow`-scope push problem (F-18) is resolved
- [x] Identified the job failure: the post-build packaging step `git rev-parse HEAD` hits git "dubious ownership" inside the root container (F-19, G16). This is a workflow issue, not a compile error, so there are no source changes
- [x] Resolved G4/G13 (IDF v5.4.2; artifact layout read from the workflow file, read-only). F-01/F-10 and the ≥ v5.3 branch of F-12 are build-confirmed
- [ ] Operator: in `.github/workflows/nostromo-build.yml` step "Package binaries and build evidence", add `git config --global --add safe.directory "$GITHUB_WORKSPACE"` before `git rev-parse HEAD` (or write `${{ github.sha }}` instead). Then re-trigger the build, which should upload `nostromo-firmware-<id>`
- [ ] Next phase: if the full CI log becomes available, scan it for warnings from `components/` and `main/` (V-01 "no warnings" criterion)

### M5 — Build and bench verification (needs a toolchain and hardware; operator or a bench-enabled phase)
Ordered procedure. Stop at the first failure and record it in VERIFICATION_MATRIX.md:

**Precondition:** commit the whole `source/` tree (platform/operator; agents have no git).
Build #1 was blocked by the dirty worktree (F-15, G12). The platform has since committed
the tree (`65c6ad9`, `3e2dc04`), and it is expected to commit this phase's changes as well.
Builds #2 and #3 then failed at `git push` with HTTP 403 (F-17, G14). Build #4 got past the
403 but was rejected because the token has no `workflow` scope (F-18, G15). Build #5 got through
push and compile, then failed in the workflow's packaging step (F-19, G16), and V-01 is
still open until that workflow step is fixed.

0. [ ] V-04 wiring inspection **before power-up** (sensor TX → GPIO16; F-13)
1. [ ] V-09 `make -C test/host run`, and V-09b `python3 test/bench/test_check_log.py`
2. [ ] V-01 GitHub Actions build of the committed tree (equivalent to `idf.py set-target esp32 && idf.py build`). Record the IDF version and the artifact list from the CI log (resolves G4, G13, F-10). On a compile error, the next IMPLEMENTATION phase fixes it from the CI log
3. [ ] V-02 flash the CI artifacts (`esptool write_flash @flash_args`) or a local build (`idf.py -p /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 flash`)
4. [ ] V-03b boot **without** the sensor → banner + NO_DATA, no crash (`check_log.py --check v03b`)
5. [ ] V-03 boot with the sensor → `status=OK` (resolves G1; if `ok=0`, use `rx_bytes` to tell wiring from protocol problems)
6. [ ] V-05, V-06, V-07, V-11 (short bench checks)
7. [ ] V-08, V-10, V-12 (long runs, real tank or equivalent; resolves G7)
8. [ ] (Optional) V-14 invalid-pin rejection

### M6 — Stage 2: Home Assistant (future run, after M5 passes)
- [ ] Confirm the integration path and broker with the operator (G5; recommended: MQTT + HA discovery)
- [ ] Wi-Fi STA with reconnect/backoff; credentials via a local, git-ignored Kconfig or NVS
- [ ] `esp-mqtt` client, availability/LWT, retained discovery config
- [x] `level_report` fan-out (run #3, written, not compiled)
- [ ] Non-blocking MQTT sink (depth-1 overwrite queue + MQTT task), registered with `level_report_add_sink`
- [ ] Tank geometry → `level_percent` (G8)
- [ ] (Optional) OTA

## Order and dependencies

```
M0 → (M1–M4b written) → M5 → M6
M5 step 1 (host tests) needs only a host C compiler and Python 3
M5 steps 3–7 need the board on the "Simple UART" bench + the sensor
```

## Constraints
- The agent environment so far has no shell, compiler, or hardware access. Code is written
  but **not compiled or flashed**. M5 results must come from a real execution, with log
  excerpts as evidence. They must never be inferred.
- Keep `a02yyuw_parser` and `a02yyuw_filter` free of IDF dependencies.
- Keep the §8 report line backward-compatible: only append fields.
- No secrets in the repository.

## Risks

| Risk | Mitigation |
|------|------------|
| CI never builds because the tree is uncommitted (F-15) | Commit before each build request. `.gitignore` keeps `build/`/`sdkconfig` out of the tree |
| Code does not compile (F-01, never built) | M5 step 2 comes before any bench work; the API surface is listed in SYSTEM_ARCHITECTURE.md §7.3 |
| TX/RX pins reversed (G1) | Kconfig pins; banner shows the pin map; V-03 + `rx_bytes` (F-03) tell the failure modes apart. Output contention on GPIO17 (F-13): inspect before power-up, diagnose with TX = -1 |
| Bench IDF version newer than v5.3 / v6.x | F-12 conditional driver requirement; V-01 records the version |
| WROVER module, PSRAM on GPIO16/17 (G2) | Check the module marking; alternative pins GPIO25/26 |
| Parser locks onto a false `0xFF` header | Resync design + host tests (V-09) |
| Noisy readings in the tank | Optional median filter; evaluate in V-08 |
| 3.3 V supply reduces range (G7) | V-08 at real tank depth |
| IDF v4.x installed on the bench | Not supported (§7.3); require v5.x |
| Flash and log share one port | `idf.py flash monitor` sequences them; close other monitors before flashing |
