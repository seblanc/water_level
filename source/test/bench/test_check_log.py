#!/usr/bin/env python3
"""Self-tests for check_log.py.

All logs here are SYNTHETIC, generated in memory to exercise the checker
logic. They are not captured from hardware and are not verification evidence.

Run:  python3 test/bench/test_check_log.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_log as cl  # noqa: E402

BANNER = [
    "rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)",
    "I (29) boot: ESP-IDF v5.2 2nd stage bootloader",
    "I (310) cpu_start: Starting scheduler on PRO CPU.",
    "I (320) wt: === water_tank fw 0.1.0 (built Oct 16 2026 12:00:00) ===",
    "I (321) wt: idf=v5.2 chip_rev=v3.0 cores=2 flash=4MB",
    "I (322) wt: sensor=A02YYUW uart=2 rx_gpio=16 tx_gpio=17 baud=9600",
    "I (323) wt: report_period_ms=1000 stale_ms=1000 median_window=1",
    "I (330) a02yyuw: UART2 @9600 8N1, rx=GPIO16 tx=GPIO17, median_window=1",
]
LOST = "W (%d) wt: sensor data lost: no valid frame for > 1000 ms (check wiring, RX/TX pins)"
AVAILABLE = "I (%d) wt: sensor data available"


def report(t, dist, status, age, ok, cks=0, ovf=0, rx=None):
    rx = ok * 4 if rx is None else rx
    return ("I (%d) wt: distance_mm=%d status=%s age_ms=%d ok=%d cks_err=%d sync_skip=0"
            " uart_ovf=%d rx_bytes=%d line_err=0" % (t, dist, status, age, ok, cks, ovf, rx))


def ok_run(n, t0=1330, period=1000, ok0=10):
    return [report(t0 + i * period, 1234, "OK", 42, ok0 + 10 * i) for i in range(n)]


class ParseTest(unittest.TestCase):
    def test_banner_and_reports(self):
        log = cl.parse_log(BANNER + ok_run(3))
        self.assertEqual(log.banners, 1)
        self.assertEqual(len(log.main.reports), 3)
        self.assertEqual(log.main.reports[0].num("distance_mm"), 1234)
        self.assertEqual(log.main.reports[0].num("rx_bytes"), 40)
        self.assertEqual(cl.banner_value(log.main, "rx_gpio"), 16)
        self.assertEqual(log.failures, [])  # the pre-banner "rst:" is ignored

    def test_ansi_codes_stripped(self):
        lines = ["\x1b[0;32m%s\x1b[0m" % x for x in BANNER + ok_run(2)]
        log = cl.parse_log(lines)
        self.assertEqual(len(log.main.reports), 2)

    def test_capture_started_mid_run(self):
        log = cl.parse_log(ok_run(3))
        self.assertEqual(log.banners, 0)
        self.assertEqual(len(log.boots), 1)
        self.assertIsNone(log.main.banner_line)


class V03Test(unittest.TestCase):
    def test_pass(self):
        res = cl.check_v03(cl.parse_log(BANNER + ok_run(5)), 16, 17)
        self.assertEqual(res.verdict, cl.PASS, res.details)

    def test_swapped_pins_in_banner(self):
        lines = [x.replace("rx_gpio=16 tx_gpio=17", "rx_gpio=17 tx_gpio=16") for x in BANNER]
        res = cl.check_v03(cl.parse_log(lines + ok_run(5)), 16, 17)
        self.assertEqual(res.verdict, cl.FAIL)

    def test_no_ok_lines(self):
        lines = BANNER + [report(1330 + i * 1000, -1, "NO_DATA", -1, 0, rx=0) for i in range(5)]
        res = cl.check_v03(cl.parse_log(lines), 16, 17)
        self.assertEqual(res.verdict, cl.FAIL)

    def test_ok_counter_stuck(self):
        lines = BANNER + [report(1330 + i * 1000, 1234, "OK", 42, 10) for i in range(3)]
        res = cl.check_v03(cl.parse_log(lines), 16, 17)
        self.assertEqual(res.verdict, cl.FAIL)


class V03bTest(unittest.TestCase):
    def nosensor(self, n=6):
        return BANNER + [LOST % 1329] + [report(1330 + i * 1000, -1, "NO_DATA", -1, 0, rx=0)
                                         for i in range(n)]

    def test_pass(self):
        res = cl.check_v03b(cl.parse_log(self.nosensor()))
        self.assertEqual(res.verdict, cl.PASS, res.details)

    def test_reboot_fails(self):
        res = cl.check_v03b(cl.parse_log(self.nosensor() + self.nosensor()))
        self.assertEqual(res.verdict, cl.FAIL)

    def test_too_short(self):
        res = cl.check_v03b(cl.parse_log(self.nosensor(n=2)))
        self.assertEqual(res.verdict, cl.INCONCLUSIVE)


class V05Test(unittest.TestCase):
    def test_pass_61_lines(self):
        res = cl.check_v05(cl.parse_log(BANNER + ok_run(61)), None)
        self.assertEqual(res.verdict, cl.PASS, res.details)

    def test_short_capture_inconclusive(self):
        res = cl.check_v05(cl.parse_log(BANNER + ok_run(10)), None)
        self.assertEqual(res.verdict, cl.INCONCLUSIVE)

    def test_jitter_fails(self):
        lines = ok_run(61)
        lines[30] = report(1330 + 30 * 1000 + 80, 1234, "OK", 42, 310)
        res = cl.check_v05(cl.parse_log(BANNER + lines), None)
        self.assertEqual(res.verdict, cl.FAIL)

    def test_drift_fails(self):
        res = cl.check_v05(cl.parse_log(BANNER + ok_run(61, period=1010)), 1000)
        self.assertEqual(res.verdict, cl.FAIL)


class V10Test(unittest.TestCase):
    def test_pass_10_min(self):
        res = cl.check_v10(cl.parse_log(BANNER + ok_run(601)))
        self.assertEqual(res.verdict, cl.PASS, res.details)

    def test_checksum_ratio_fails(self):
        lines = ok_run(601)
        lines.append(report(1330 + 601 * 1000, 1234, "OK", 42, 6020, cks=100))
        res = cl.check_v10(cl.parse_log(BANNER + lines))
        self.assertEqual(res.verdict, cl.FAIL)

    def test_overflow_fails(self):
        lines = ok_run(601)
        lines.append(report(1330 + 601 * 1000, 1234, "OK", 42, 6020, ovf=1))
        res = cl.check_v10(cl.parse_log(BANNER + lines))
        self.assertEqual(res.verdict, cl.FAIL)


class V11Test(unittest.TestCase):
    def unplug(self, age_at_detect=1500):
        lines = ok_run(5)                                     # t = 1330 .. 5330, ok = 10 .. 50
        lines.append(LOST % 6329)
        lines.append(report(6330, -1, "NO_DATA", age_at_detect, 50))
        lines += [report(7330 + i * 1000, -1, "NO_DATA", age_at_detect + 1000 * (i + 1), 50)
                  for i in range(4)]
        lines.append(AVAILABLE % 11329)
        lines += ok_run(3, t0=11330, ok0=55)
        return BANNER + lines

    def test_pass(self):
        res = cl.check_v11(cl.parse_log(self.unplug()))
        self.assertEqual(res.verdict, cl.PASS, res.details)

    def test_slow_detection_fails(self):
        res = cl.check_v11(cl.parse_log(self.unplug(age_at_detect=2500)))
        self.assertEqual(res.verdict, cl.FAIL)

    def test_missing_warning_fails(self):
        lines = [x for x in self.unplug() if "data lost" not in x]
        res = cl.check_v11(cl.parse_log(lines))
        self.assertEqual(res.verdict, cl.FAIL)

    def test_no_episode_inconclusive(self):
        res = cl.check_v11(cl.parse_log(BANNER + ok_run(5)))
        self.assertEqual(res.verdict, cl.INCONCLUSIVE)


class V12Test(unittest.TestCase):
    def test_short_clean_run_inconclusive(self):
        res = cl.check_v12(cl.parse_log(BANNER + ok_run(10)))
        self.assertEqual(res.verdict, cl.INCONCLUSIVE)

    def test_one_hour_clean_run_passes(self):
        res = cl.check_v12(cl.parse_log(BANNER + ok_run(3601)))
        self.assertEqual(res.verdict, cl.PASS, res.details)

    def test_panic_fails(self):
        lines = BANNER + ok_run(5) + [
            "Guru Meditation Error: Core  0 panic'ed (LoadProhibited). Exception was unhandled.",
        ]
        res = cl.check_v12(cl.parse_log(lines))
        self.assertEqual(res.verdict, cl.FAIL)

    def test_reboot_fails(self):
        res = cl.check_v12(cl.parse_log(BANNER + ok_run(5) + BANNER + ok_run(5)))
        self.assertEqual(res.verdict, cl.FAIL)

    def test_wdt_fails(self):
        lines = BANNER + ok_run(5) + ["E (6000) task_wdt: Task watchdog got triggered."]
        res = cl.check_v12(cl.parse_log(lines))
        self.assertEqual(res.verdict, cl.FAIL)


HA_DISABLED = ["I (331) ha: disabled (no wifi_ssid/mqtt_uri configured)"]
HA_ENABLED = [
    "I (331) ha: enabled id=wt_a1b2c3 ssid=HomeNet broker=192.168.1.10:1883 user=wt period_ms=10000",
    "I (402) phy_init: phy_version 4830,54550f7,Jun 20 2024,14:22:08",  # F-29
    "I (450) wifi:mode : sta (24:0a:c4:a1:b2:c3)",  # no space after the tag: not an IDF log line
    "I (1250) esp_netif_handlers: sta ip: 192.168.1.42, mask: 255.255.255.0, gw: 192.168.1.1",
    "I (1251) ha: wifi connected ip=192.168.1.42",
]


def ha_run(n, ha_mid):
    """ok_run(n) with HA lines interleaved between the reports (as the wt_ha task would)."""
    lines = []
    for i, rep in enumerate(ok_run(n)):
        lines.append(rep)
        if i == 2:
            lines += ["I (%d) ha: mqtt connected" % (3400 + i),
                      "I (%d) ha: discovery published, wt_ha stack free=1820 B" % (3410 + i)]
        if i == n // 2:
            lines += ha_mid(1330 + i * 1000 + 500)
    return lines


def wifi_drop(t):
    return ["W (%d) ha: mqtt disconnected" % t,
            "W (%d) ha: wifi disconnected reason=8, retry in 1 s" % (t + 1),
            "I (%d) ha: wifi connected ip=192.168.1.42" % (t + 300)]


class HaLinesTest(unittest.TestCase):
    """Stage 2 'ha:' lines must not change the Stage 1 verdicts (F-29)."""

    def test_disabled_variant(self):
        log = cl.parse_log(BANNER + HA_DISABLED + ok_run(61))
        self.assertEqual(len(log.main.reports), 61)
        self.assertEqual(cl.check_v03(log, 16, 17).verdict, cl.PASS)
        self.assertEqual(cl.check_v05(log, None).verdict, cl.PASS)
        self.assertEqual(log.failures, [])

    def test_enabled_variant(self):
        log = cl.parse_log(BANNER + HA_ENABLED + ha_run(61, wifi_drop))
        self.assertEqual(log.banners, 1)
        self.assertEqual(len(log.main.reports), 61)  # ha/phy_init lines are not reports
        self.assertEqual(cl.banner_value(log.main, "report_period_ms"), 1000)
        self.assertEqual(cl.check_v03(log, 16, 17).verdict, cl.PASS)
        self.assertEqual(cl.check_v05(log, None).verdict, cl.PASS)
        self.assertEqual(log.failures, [])

    def test_enabled_v12_one_hour(self):
        res = cl.check_v12(cl.parse_log(BANNER + HA_ENABLED + ha_run(3601, wifi_drop)))
        self.assertEqual(res.verdict, cl.PASS, res.details)

    def test_enabled_reboot_still_fails_v12(self):
        lines = BANNER + HA_ENABLED + ha_run(5, wifi_drop)
        res = cl.check_v12(cl.parse_log(lines + BANNER + HA_ENABLED + ok_run(5)))
        self.assertEqual(res.verdict, cl.FAIL)

    def test_brownout_on_wifi_start_fails_v12(self):
        lines = BANNER + HA_ENABLED[:2] + ["Brownout detector was triggered"] + ok_run(5)
        res = cl.check_v12(cl.parse_log(lines))
        self.assertEqual(res.verdict, cl.FAIL)


LOCKED = ("I (%d) a02yyuw: sensor locked after 9 bytes; discarded during acquisition:"
          " 1 checksum errors, 1 sync skips, 0 line errors")


def target_run(n, t0=1330, dist=300, ok0=10):
    """n fresh OK lines with the target inside the spec rev 3 window (100..600 mm)."""
    return [report(t0 + i * 1000, dist, "OK", 42, ok0 + 10 * i) for i in range(n)]


class V15Test(unittest.TestCase):
    def check(self, lines):
        return cl.check_v15(cl.parse_log(lines), 16, 17)

    def test_pass_clean(self):
        res = self.check(BANNER + HA_DISABLED + [LOCKED % 335] + target_run(12))
        self.assertEqual(res.verdict, cl.PASS, res.details)

    def test_pass_leading_no_data(self):
        lines = (BANNER + HA_DISABLED + [LOST % 1329, report(1330, -1, "NO_DATA", -1, 0, rx=0),
                                         LOCKED % 1500] + target_run(12, t0=2330))
        res = self.check(lines)
        self.assertEqual(res.verdict, cl.PASS, res.details)

    def test_checksum_error_fails(self):
        lines = target_run(12)
        lines[6] = report(1330 + 6 * 1000, 300, "OK", 42, 70, cks=1)
        res = self.check(BANNER + lines)
        self.assertEqual(res.verdict, cl.FAIL)

    def test_out_of_window_fails(self):
        lines = target_run(12)
        lines[4] = report(1330 + 4 * 1000, 605, "OK", 42, 50)
        res = self.check(BANNER + lines)
        self.assertEqual(res.verdict, cl.FAIL)

    def test_no_data_after_first_ok_fails(self):
        lines = target_run(6) + [report(7330, -1, "NO_DATA", 1100, 60)] + target_run(6, t0=8330, ok0=70)
        res = self.check(BANNER + lines)
        self.assertEqual(res.verdict, cl.FAIL)

    def test_error_line_fails(self):
        lines = BANNER + target_run(6) + ["E (7000) ha: mqtt start failed: ESP_FAIL"] + \
            target_run(6, t0=7330, ok0=70)
        res = self.check(lines)
        self.assertEqual(res.verdict, cl.FAIL)

    def test_nine_lines_inconclusive(self):
        res = self.check(BANNER + target_run(9))
        self.assertEqual(res.verdict, cl.INCONCLUSIVE, res.details)

    def test_not_in_default_checks(self):
        self.assertIn("v15", cl.CHECKS)
        self.assertNotIn("v15", cl.DEFAULT_CHECKS)


class MainTest(unittest.TestCase):
    def test_exit_codes(self):
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False) as fh:
            fh.write("\n".join(BANNER + ok_run(61)) + "\n")
            path = fh.name
        try:
            self.assertEqual(cl.main([path, "--check", "v05"]), 0)
            self.assertEqual(cl.main([path, "--check", "v10"]), 3)  # 60 s < 10 min
            self.assertEqual(cl.main([path, "--check", "v03b"]), 1)
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
