# DEVELOPMENT PLAN — Water Tank Level (project 1a95ca931d72)

See SYSTEM_ARCHITECTURE.md for the design and VERIFICATION_MATRIX.md for the checks.
Status markers:
- `[x]` done and observable in this repo or in a recorded CI result
- `[~]` partly done (the remaining part is stated)
- `[ ]` not done

"Written" means source exists. "CI-compiled" means a GitHub Actions build compiled it.
Nothing has been flashed or run on hardware.

Last updated: run #6, ARCHITECTURE phase (attempt 1), 2026-10-17.

## Milestones

### M0 — Architecture
- [x] SYSTEM_ARCHITECTURE.md: hardware, pin map, modules, mapping, gaps, findings, telemetry contract (runs #1–#3)
- [x] VERIFICATION_MATRIX.md: derived requirements, proposed acceptance criteria, checks, results log
- [x] Run #4: docs condensed. CI-artifact flashing procedure derived from the workflow, read-only (§4.1, G13 closed, F-20)
- [x] Run #4: **Stage 2 design** (SYSTEM_ARCHITECTURE.md §5): MQTT + HA discovery, runtime gating, NVS provisioning, topics/payloads, task model, reconnect policy, resource budget, new gaps G17–G19
- [x] Run #5: static review of the partial Stage 2 code from `7bac3ea` (SYSTEM_ARCHITECTURE.md §5.10, F-22…F-30); build-gating decisions for the remaining steps (§5.10.2); §8 event lines updated (F-28)
- [x] Run #6: compile-surface pass over the Stage 2 sources against IDF v5.4 APIs and warning flags (SYSTEM_ARCHITECTURE.md §5.10.4). No new blocker; F-31 (esp-mqtt console noise) recorded as accepted. M6a below is unchanged
- [ ] Operator confirms or corrects G1 (pin direction), G2 (module type), G5 (broker/HA setup), G6 (acceptance criteria AC-1…AC-8)

### M1–M4 — Stage 1 firmware (written runs #1–#3; **CI-compiled**, Builds #5–#8)
- [x] Project skeleton, `sdkconfig.defaults`, Kconfig `WT_*` options, boot banner, README
- [x] `a02yyuw_parser` / `a02yyuw_filter` (pure C) + `test/host/test_parser.c` (**never executed**)
- [x] `a02yyuw` driver: UART2 9600 8N1, event-queue RX task, overflow/line-error handling, staleness, optional median
- [x] `level_report` dispatcher + console sink, fixed-rate 1 Hz reporter task, NO_DATA transition logging
- [x] Static-review fixes F-03, F-04, F-05, F-12; F-13 documented
- [x] `test/bench/check_log.py` + self-tests (**never executed**)
- [ ] Operator decision: keep `WT_SENSOR_TX_GPIO=17` (current) or default it to -1 (F-13)

### M4z — CI pipeline (run #3)
- [x] Blockers F-15 (dirty tree), F-17 (403), F-18 (`workflow` scope), F-19 (dubious ownership) resolved, partly by the operator
- [x] **Build #8 (`43e0382`) completed**; artifact `nostromo-firmware-8` uploaded (Stage 1 baseline)
- [ ] Supply a full compile log (any build after #4) so it can be scanned for warnings from `components/` and `main/` (V-01 sub-criterion)

### M5 — Stage 1 bench verification (operator; needs hardware)
Ordered procedure. Stop at the first failure and record it in VERIFICATION_MATRIX.md.
Use **Build #8's artifact** (Stage 1 only, available until about 2026-11-16) or any later
build. A later build with HA enabled but not provisioned should behave the same, plus one
`ha: disabled` line.

0. [ ] V-04 wiring inspection **before power-up** (sensor TX → GPIO16; F-13)
1. [ ] V-09 `make -C test/host run`, and V-09b `python3 test/bench/test_check_log.py`
2. [~] V-01: CI build passed (Build #8). The warning scan is still pending
3. [ ] V-02 flash the CI artifact (SYSTEM_ARCHITECTURE.md §4.1) or a local build
4. [ ] V-03b boot **without** the sensor → banner + NO_DATA, no crash (`check_log.py --check v03b`)
5. [ ] V-03 boot with the sensor → `status=OK` (resolves G1; if `ok=0`, use `rx_bytes` to tell wiring from protocol problems)
6. [ ] V-05, V-06, V-07, V-11 (short bench checks)
7. [ ] V-08, V-10, V-12 (long runs, real tank or equivalent; resolves G7)
8. [ ] (Optional) V-14 invalid-pin rejection

### M6a — Stage 2 implementation (**next IMPLEMENTATION phase**; agent, file tools only)
Scope follows SYSTEM_ARCHITECTURE.md §5 exactly. Every step must keep the §8 report line
byte-identical and must not change Stage 1 module behaviour.

Steps 1–4 were written in run #4 (commit `7bac3ea`, phase recorded as failed, G20) and
reviewed read-only in run #5 (§5.10). They are **written, never compiled**. **Do not
rewrite them**; only apply the listed fixes.

1. [x] `components/ha_mqtt/include/ha_payload.h` + `ha_payload.c`: pure-C builders (written; host test not run)
2. [x] `test/host/test_ha_payload.c` + Makefile target (written; **never executed**, V-09)
3. [x] `components/ha_mqtt/ha_config.c` + `ha_mqtt_priv.h` (written)
4. [x] `components/ha_mqtt/ha_mqtt.c` + `include/ha_mqtt.h` (written). F-25 (backoff
   advances only when `esp_timer_start_once` returns `ESP_OK`) and the F-26 comment were
   applied in run #6

**Steps 5, 6 and 7 go into one change** (F-22, F-30): any one of them alone is expected
to break the build. Use the exact text in SYSTEM_ARCHITECTURE.md §5.10.2.

**Execution order (run #6):** steps 5–7 first, then step 8, then the step 4 fixes (F-25/F-26),
then 9–10. The point is to get Stage 2 into a CI build as early as possible: two phases
in a row (run #4 impl, run #5 arch) ended as "failed" without a build. The F-25 fix is
small and independent, and it can wait if the phase runs short.

**Run #6 status (attempts 1–3):** steps 1–10 are written and statically reviewed. **None
of them has been compiled yet**: the next CI build is the first one that includes Stage 2.

5. [x] `components/ha_mqtt/CMakeLists.txt`: `ha_payload.c` always; `ha_config.c` and
   `ha_mqtt.c` only `if(CONFIG_WT_HA_ENABLE)`. `REQUIRES level_report`; `PRIV_REQUIRES
   esp_wifi esp_netif esp_event nvs_flash mqtt esp_timer esp_app_format` (F-23).
   `main/CMakeLists.txt`: add `ha_mqtt` to `REQUIRES`. `main.c`: `#if CONFIG_WT_HA_ENABLE`
   include + `(void)ha_mqtt_start();` between `level_report_init()` and the reporter
   `xTaskCreate`; update the two stale "Stage 1 / not used" comments
6. [x] `main/Kconfig.projbuild` submenu "Home Assistant": `WT_HA_ENABLE` (bool, default y);
   `depends on WT_HA_ENABLE`: `WT_WIFI_SSID`, `WT_WIFI_PASSWORD`, `WT_MQTT_BROKER_URI`,
   `WT_MQTT_USERNAME`, `WT_MQTT_PASSWORD` (string, default ""), `WT_HA_PUBLISH_PERIOD_MS`
   (int, 10000, range 1000 600000), `WT_HA_DISCOVERY_PREFIX` (string, "homeassistant").
   Help texts: credentials belong in NVS or a local `sdkconfig`, never in `sdkconfig.defaults`
7. [x] `sdkconfig.defaults`: `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`,
   `CONFIG_WT_HA_ENABLE=y`. **No credentials**
8. [x] `tools/nvs_config.example.csv` (header `key,type,encoding,value`; `wt_cfg,namespace,,`
   then the five `string` keys with placeholder values). `.gitignore`: `nvs_config.csv`,
   `nvs*.bin`, `test/host/test_ha_payload` (F-24). README section "Home Assistant":
   provisioning steps (§5.3), HA-side expectations (§5.4), security note, NVS-erase note (F-21)
9. [x] `test/bench/test_check_log.py`: a synthetic log with `ha:` lines (enabled and
   disabled variants, plus a `phy_init` INFO line, F-29) and unchanged V-03/V-05/V-12 verdicts
   (class `HaLinesTest`; written, **not run**, V-09b)
10. [x] Self-review: every `CONFIG_WT_*` macro used in C exists in Kconfig with the same
    name and type; format specifiers vs. types (`%d` with `int` Kconfig ints, `PRIu32`);
    no `abort`/`esp_restart`/`ESP_ERROR_CHECK` in `components/ha_mqtt`

Exit criterion for the phase: the files are written and statically reviewed. The **next CI
build** must compile them (V-01 re-run, record the image size against §5.8). If it fails,
the following phase fixes the build from the CI log before doing anything else.

### M6b — Stage 2 verification (operator; needs Wi-Fi AP, broker, HA)
- [ ] V-01 re-run: CI build with HA compiled in; record the image size against the §5.8 estimate
- [ ] V-13a…V-13g (VERIFICATION_MATRIX.md)
- [ ] Re-run V-05 and V-12 with HA connected (Wi-Fi must not disturb the 1 Hz stream or cause brownouts, G17)

### M7 — Later
- [ ] Tank geometry → `level_percent` / `volume_l` (G8)
- [ ] (Optional) MQTT over TLS (`mqtts://`, CA cert in NVS or embedded), OTA (needs an OTA partition table)

## Order and dependencies

```
M0 ─→ M1–M4 (CI-compiled) ─→ M5 (operator, hardware)
                    └──────→ M6a steps 1–4 (written, 7bac3ea) ─→ steps 5–10 (next phase) ─→ CI build ─→ M6b (operator, needs G5)
```
M5 and M6a are independent. M6a does not need the hardware results, because its default
runtime behaviour without provisioning equals Stage 1. M6b should follow M5 step 5, so
that sensor problems are not confused with network problems.

## Constraints
- Agent phases have no shell, compiler or hardware. Only the CI builder compiles. M5/M6b
  results must come from real executions, with log excerpts as evidence. They must never be
  inferred.
- Keep `a02yyuw_parser`, `a02yyuw_filter` and `ha_payload` free of IDF dependencies.
- Keep the §8 report line backward-compatible: only append fields.
- No secrets in the repository. CI images therefore never contain credentials.
- Agents must not edit `.github/workflows/` (platform infrastructure).

## Risks

| Risk | Mitigation |
|------|------------|
| TX/RX pins reversed (G1) | Kconfig pins; banner; V-03 + `rx_bytes`. Contention (F-13): inspect before power-up, diagnose with TX = -1 |
| WROVER module, PSRAM on GPIO16/17 (G2) | Check the module marking; alternative pins GPIO25/26 |
| Stage 2 code fails to compile (never built, no local toolchain) | Pure-C payload part host-testable; API list in §7.3; next phase fixes it from the CI log before anything else |
| Stage 2 image exceeds the 1 MB app partition | Switch to the single-app-large table (1.5 MB) in M6a step 7 |
| Another partial M6a phase (as in run #4) leaves CMake without Kconfig, or vice versa, and breaks the Stage 1 build | Steps 5–7 are one atomic change, written before steps 8–10. If time is short, stop before step 5 rather than inside 5–7 |
| Wi-Fi TX peaks brown out the shared 3V3 rail (G17) | V-12 brownout pattern, V-10 `cks_err`; bulk capacitor on sensor VCC |
| Network code disturbs the 1 Hz console stream | Depth-1 overwrite queue; publisher prio below reporter; V-13g re-runs V-05 |
| Credentials leaked into git | Only NVS images or a local `sdkconfig`, both git-ignored; example CSV has placeholders |
| No broker / operator prefers ESPHome (G5) | Runtime-gated: unprovisioned devices behave as Stage 1; the sink can be replaced |
| Build #8 artifact expires (~2026-11-16) | Any later CI build reproduces Stage 1 behaviour when unprovisioned |
| Parser locks onto a false `0xFF` header | Resync design + host tests (V-09) |
| 3.3 V supply reduces range (G7) | V-08 at real tank depth |
| Flash and log share one port | `idf.py flash monitor` sequences them |
