# DEVELOPMENT PLAN — Water Tank Level (project 1a95ca931d72)

See SYSTEM_ARCHITECTURE.md for the design and VERIFICATION_MATRIX.md for the checks.
Status markers:
- `[x]` done and observable in this repo or in a recorded CI result
- `[~]` partly done (the remaining part is stated)
- `[ ]` not done

"Written" means source exists. "CI-compiled" means a GitHub Actions build compiled it.
Nothing has been flashed or run on hardware.

Last updated: run #4, ARCHITECTURE phase (attempt 1), 2026-10-17.

## Milestones

### M0 — Architecture
- [x] SYSTEM_ARCHITECTURE.md: hardware, pin map, modules, mapping, gaps, findings, telemetry contract (runs #1–#3)
- [x] VERIFICATION_MATRIX.md: derived requirements, proposed acceptance criteria, checks, results log
- [x] Run #4: docs condensed. CI-artifact flashing procedure derived from the workflow, read-only (§4.1, G13 closed, F-20)
- [x] Run #4: **Stage 2 design** (SYSTEM_ARCHITECTURE.md §5): MQTT + HA discovery, runtime gating, NVS provisioning, topics/payloads, task model, reconnect policy, resource budget, new gaps G17–G19
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

1. [ ] `components/ha_mqtt/ha_payload.{h,c}`, pure C, no IDF headers. Builders for the topic
   strings, the device ID from 3 MAC bytes, the state JSON, the distance/status discovery
   JSON and the availability strings. Each writes into a caller buffer and returns its
   length, or -1 on truncation
2. [ ] `test/host/test_ha_payload.c` + Makefile target. Cases: exact expected JSON for OK /
   NO_DATA / OUT_OF_RANGE, truncation returns -1, ID formatting, discovery topic with a
   custom prefix
3. [ ] `components/ha_mqtt/ha_config.c`: read NVS namespace `wt_cfg` keys, falling back to
   Kconfig per key. A missing namespace is not an error
4. [ ] `components/ha_mqtt/ha_mqtt.c` + `include/ha_mqtt.h`: `esp_err_t ha_mqtt_start(void)`,
   runtime gate, Wi-Fi STA with esp_timer backoff (1→60 s), esp-mqtt client with LWT, birth
   and discovery publishes on each connect, depth-1 overwrite queue sink, `wt_ha` publisher
   task (prio 3, 4096 B), `esp_log_level_set("wifi", ESP_LOG_WARN)`. No `abort()` and no
   `esp_restart()` anywhere in the component
5. [ ] `components/ha_mqtt/CMakeLists.txt` (`REQUIRES level_report`;
   `PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash mqtt esp_timer`) and the `main/`
   integration: `#if CONFIG_WT_HA_ENABLE` call between `level_report_init()` and the
   reporter task creation. An error from `ha_mqtt_start` is logged and does not abort
6. [ ] Kconfig submenu "Home Assistant": `WT_HA_ENABLE` (default y), `WT_WIFI_SSID`,
   `WT_WIFI_PASSWORD`, `WT_MQTT_BROKER_URI`, `WT_MQTT_USERNAME`, `WT_MQTT_PASSWORD` (all
   default ""), `WT_HA_PUBLISH_PERIOD_MS` (10000, range 1000–600000),
   `WT_HA_DISCOVERY_PREFIX` ("homeassistant")
7. [ ] `sdkconfig.defaults`: `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`, `CONFIG_WT_HA_ENABLE=y`.
   **No credentials**
8. [ ] `tools/nvs_config.example.csv` (placeholders only), `.gitignore` additions
   (`nvs_config.csv`, `nvs*.bin`), README section "Home Assistant" (provisioning steps,
   HA-side expectations, security note §5.3)
9. [ ] `test/bench/test_check_log.py`: add a synthetic log containing `ha:` lines and
   confirm that the verdicts are unchanged
10. [ ] Self-review against the §7.3 Stage 2 API list and `-Werror=all` (format specifiers,
    unused variables in the disabled path)

Exit criterion for the phase: the files are written and statically reviewed. The **next CI
build** must compile them (V-01 re-run). If it fails, the following phase fixes the
build from the CI log before doing anything else.

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
                    └──────→ M6a (agent, next phase) ─→ CI build ─→ M6b (operator, needs G5)
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
| Wi-Fi TX peaks brown out the shared 3V3 rail (G17) | V-12 brownout pattern, V-10 `cks_err`; bulk capacitor on sensor VCC |
| Network code disturbs the 1 Hz console stream | Depth-1 overwrite queue; publisher prio below reporter; V-13g re-runs V-05 |
| Credentials leaked into git | Only NVS images or a local `sdkconfig`, both git-ignored; example CSV has placeholders |
| No broker / operator prefers ESPHome (G5) | Runtime-gated: unprovisioned devices behave as Stage 1; the sink can be replaced |
| Build #8 artifact expires (~2026-11-16) | Any later CI build reproduces Stage 1 behaviour when unprovisioned |
| Parser locks onto a false `0xFF` header | Resync design + host tests (V-09) |
| 3.3 V supply reduces range (G7) | V-08 at real tank depth |
| Flash and log share one port | `idf.py flash monitor` sequences them |
