#!/usr/bin/env python3
"""Offline checker for water_tank console logs.

Applies the telemetry contract (SYSTEM_ARCHITECTURE.md section 8) and the pass
criteria of VERIFICATION_MATRIX.md checks V-03, V-03b, V-05, V-10, V-11 and
V-12 to a log file captured on the bench. It only reads a file (or stdin); it
never opens a serial port or touches hardware.

Usage:
    python3 test/bench/check_log.py capture.log                 # default: v03 v05 v10 v12
    python3 test/bench/check_log.py capture.log --check v11
    python3 test/bench/check_log.py nosensor.log --check v03b --check v12

Exit status: 0 = all requested checks PASS, 1 = at least one FAIL,
3 = no FAIL but at least one INCONCLUSIVE (e.g. capture too short).

Python 3.7+, standard library only.
"""
import argparse
import re
import sys
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, Tuple

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
# ESP-IDF log prefix: "<L> (<ms since boot>) <tag>: <message>"
LOG_RE = re.compile(r"\b([EWIDV]) \((\d+)\) ([^\s:]+): (.*)$")
BANNER_RE = re.compile(r"\bwt: === water_tank fw ")
REPORT_TAG = "wt"
REPORT_PREFIX = "distance_mm="
# Text that means a crash or reset when seen after the first banner (V-12).
FAILURE_PATTERNS = ("Guru Meditation", "abort()", "Task watchdog", "rst:", "Brownout detector")

PASS, FAIL, INCONCLUSIVE = "PASS", "FAIL", "INCONCLUSIVE"
GOOD_STATUSES = ("OK", "OUT_OF_RANGE")


@dataclass
class Report:
    line_no: int
    t_ms: int
    fields: Dict[str, str]

    @property
    def status(self) -> str:
        return self.fields.get("status", "")

    def num(self, key: str) -> Optional[int]:
        try:
            return int(self.fields[key])
        except (KeyError, ValueError):
            return None


@dataclass
class Event:
    line_no: int
    t_ms: int
    level: str
    message: str


@dataclass
class Boot:
    """Lines between two firmware banners. banner_line is None for a capture
    that started mid-run (before any banner was seen)."""
    banner_line: Optional[int]
    banner_t_ms: Optional[int]
    reports: List[Report] = field(default_factory=list)
    events: List[Event] = field(default_factory=list)
    timestamps: List[Tuple[int, int]] = field(default_factory=list)  # (line_no, t_ms), all tags


@dataclass
class Log:
    boots: List[Boot] = field(default_factory=list)
    failures: List[Tuple[int, str]] = field(default_factory=list)  # after the first banner

    @property
    def main(self) -> Optional[Boot]:
        """The first boot that has a banner, else the mid-run segment, else None."""
        for b in self.boots:
            if b.banner_line is not None:
                return b
        return self.boots[0] if self.boots else None

    @property
    def banners(self) -> int:
        return sum(1 for b in self.boots if b.banner_line is not None)


@dataclass
class Result:
    check: str
    verdict: str
    details: List[str] = field(default_factory=list)


def parse_report_fields(message: str) -> Dict[str, str]:
    return dict(kv.split("=", 1) for kv in message.split() if "=" in kv)


def parse_log(lines: Iterable[str]) -> Log:
    log = Log()
    current: Optional[Boot] = None
    for line_no, raw in enumerate(lines, 1):
        line = ANSI_RE.sub("", raw).rstrip("\r\n")
        m = LOG_RE.search(line)

        if BANNER_RE.search(line):
            current = Boot(line_no, int(m.group(2)) if m else None)
            log.boots.append(current)
            if m:
                current.timestamps.append((line_no, int(m.group(2))))
            continue

        if log.banners > 0 and any(p in line for p in FAILURE_PATTERNS):
            log.failures.append((line_no, line.strip()))

        if not m:
            continue
        level, t_ms, tag, message = m.group(1), int(m.group(2)), m.group(3), m.group(4)

        if current is None:
            if tag != REPORT_TAG:
                continue  # bootloader / IDF startup lines before the first banner
            current = Boot(None, None)  # capture started mid-run
            log.boots.append(current)

        current.timestamps.append((line_no, t_ms))
        if tag != REPORT_TAG:
            continue
        if message.startswith(REPORT_PREFIX):
            current.reports.append(Report(line_no, t_ms, parse_report_fields(message)))
        else:
            current.events.append(Event(line_no, t_ms, level, message))
    return log


def banner_value(boot: Boot, key: str) -> Optional[int]:
    """Read an integer key=value from the banner info lines (e.g. report_period_ms)."""
    for ev in boot.events:
        fields = parse_report_fields(ev.message)
        if key in fields:
            try:
                return int(fields[key])
            except ValueError:
                return None
    return None


def span_ms(reports: List[Report]) -> int:
    return reports[-1].t_ms - reports[0].t_ms if len(reports) >= 2 else 0


def worst(verdicts: Iterable[str]) -> str:
    vs = list(verdicts)
    if FAIL in vs:
        return FAIL
    if INCONCLUSIVE in vs:
        return INCONCLUSIVE
    return PASS


def check_v03(log: Log, expect_rx: int, expect_tx: int, first_ok_max_ms: int = 2000) -> Result:
    """Sensor connected: banner pins, first OK within 2 s of the banner, ok increasing."""
    r = Result("V-03", PASS)
    boot = log.main
    if boot is None or boot.banner_line is None:
        return Result("V-03", INCONCLUSIVE, ["no firmware banner in the capture"])

    verdicts = []
    rx, tx, baud = banner_value(boot, "rx_gpio"), banner_value(boot, "tx_gpio"), banner_value(boot, "baud")
    pins_ok = (rx, tx, baud) == (expect_rx, expect_tx, 9600)
    verdicts.append(PASS if pins_ok else FAIL)
    r.details.append("banner rx_gpio=%s tx_gpio=%s baud=%s (expected %d/%d/9600)"
                     % (rx, tx, baud, expect_rx, expect_tx))

    ok_reports = [x for x in boot.reports if x.status == "OK"]
    if not ok_reports:
        verdicts.append(FAIL)
        r.details.append("no status=OK report line after the banner")
        last = boot.reports[-1] if boot.reports else None
        if last is not None:
            r.details.append("last report: ok=%s rx_bytes=%s line_err=%s (rx_bytes=0: nothing arrives,"
                             " check wiring/G1; rx_bytes>0: bytes but no valid frames)"
                             % (last.num("ok"), last.num("rx_bytes"), last.num("line_err")))
    else:
        first = ok_reports[0]
        delay = first.t_ms - boot.banner_t_ms if boot.banner_t_ms is not None else None
        in_time = delay is not None and delay <= first_ok_max_ms
        verdicts.append(PASS if in_time else FAIL)
        r.details.append("first OK at line %d, %s ms after the banner (limit %d ms)"
                         % (first.line_no, delay, first_ok_max_ms))
        stuck = [b.line_no for a, b in zip(ok_reports, ok_reports[1:])
                 if (b.num("ok") or 0) <= (a.num("ok") or 0)]
        verdicts.append(FAIL if stuck else PASS)
        r.details.append("%d OK lines, %d with a non-increasing ok counter%s"
                         % (len(ok_reports), len(stuck), " (lines %s)" % stuck[:5] if stuck else ""))
    r.verdict = worst(verdicts)
    return r


def check_v03b(log: Log, min_reports: int = 5) -> Result:
    """Sensor disconnected at boot: one 'data lost' warning, NO_DATA/-1/-1/ok=0 lines, no reboot."""
    r = Result("V-03b", PASS)
    boot = log.main
    if boot is None or boot.banner_line is None:
        return Result("V-03b", INCONCLUSIVE, ["no firmware banner in the capture"])

    verdicts = []
    lost = [e for e in boot.events if e.message.startswith("sensor data lost")]
    verdicts.append(PASS if len(lost) == 1 else FAIL)
    r.details.append("'sensor data lost' warnings: %d (expected 1)" % len(lost))

    bad = [x.line_no for x in boot.reports
           if not (x.status == "NO_DATA" and x.num("distance_mm") == -1
                   and x.num("age_ms") == -1 and x.num("ok") == 0)]
    verdicts.append(FAIL if bad else PASS)
    r.details.append("%d report lines, %d not 'NO_DATA distance_mm=-1 age_ms=-1 ok=0'%s"
                     % (len(boot.reports), len(bad), " (lines %s)" % bad[:5] if bad else ""))

    if len(boot.reports) < min_reports:
        verdicts.append(INCONCLUSIVE)
        r.details.append("need at least %d report lines" % min_reports)
    verdicts.append(PASS if log.banners == 1 else FAIL)
    r.details.append("banners in capture: %d (expected 1)" % log.banners)
    r.verdict = worst(verdicts)
    return r


def check_v05(log: Log, period_ms: Optional[int], tol_ms: int = 50, min_span_ms: int = 59000) -> Result:
    """1 Hz report timing: every interval within period +/- tol, no cumulative drift."""
    r = Result("V-05", PASS)
    boot = log.main
    if boot is None or len(boot.reports) < 2:
        return Result("V-05", INCONCLUSIVE, ["fewer than 2 report lines"])
    if period_ms is None:
        period_ms = banner_value(boot, "report_period_ms") or 1000
    reps = boot.reports

    verdicts = []
    intervals = [b.t_ms - a.t_ms for a, b in zip(reps, reps[1:])]
    off = [(reps[i + 1].line_no, d) for i, d in enumerate(intervals) if abs(d - period_ms) > tol_ms]
    verdicts.append(FAIL if off else PASS)
    r.details.append("%d intervals, min=%d max=%d ms, %d outside %d +/- %d ms%s"
                     % (len(intervals), min(intervals), max(intervals), len(off), period_ms, tol_ms,
                        " (line, ms): %s" % off[:5] if off else ""))

    drift = span_ms(reps) - (len(reps) - 1) * period_ms
    verdicts.append(FAIL if abs(drift) > tol_ms else PASS)
    r.details.append("%d lines over %d ms, cumulative drift %+d ms (limit +/- %d)"
                     % (len(reps), span_ms(reps), drift, tol_ms))

    if span_ms(reps) < min_span_ms:
        verdicts.append(INCONCLUSIVE)
        r.details.append("capture span %d ms < required %d ms" % (span_ms(reps), min_span_ms))
    r.verdict = worst(verdicts)
    return r


def check_v10(log: Log, max_cks_ratio: float = 0.01, min_span_ms: int = 600000) -> Result:
    """Data integrity over >= 10 min: cks_err < 1 % of ok, uart_ovf == 0."""
    r = Result("V-10", PASS)
    boot = log.main
    if boot is None or not boot.reports:
        return Result("V-10", INCONCLUSIVE, ["no report lines"])
    last = boot.reports[-1]
    ok, cks, ovf = last.num("ok"), last.num("cks_err"), last.num("uart_ovf")
    if ok is None or cks is None or ovf is None:
        return Result("V-10", FAIL, ["last report line lacks ok/cks_err/uart_ovf (line %d)" % last.line_no])

    verdicts = []
    ratio = cks / ok if ok else float("inf")
    verdicts.append(PASS if ok > 0 and ratio < max_cks_ratio else FAIL)
    verdicts.append(PASS if ovf == 0 else FAIL)
    r.details.append("final counters: ok=%d cks_err=%d (%.3f %%, limit %.1f %%) uart_ovf=%d sync_skip=%s"
                     " rx_bytes=%s line_err=%s"
                     % (ok, cks, ratio * 100 if ok else float("inf"), max_cks_ratio * 100, ovf,
                        last.num("sync_skip"), last.num("rx_bytes"), last.num("line_err")))
    if span_ms(boot.reports) < min_span_ms:
        verdicts.append(INCONCLUSIVE)
        r.details.append("capture span %d ms < required %d ms" % (span_ms(boot.reports), min_span_ms))
    r.verdict = worst(verdicts)
    return r


def check_v11(log: Log, detect_max_ms: int = 2000) -> Result:
    """Sensor unplug/replug during a run.

    Detection latency is bounded by age_ms of the first NO_DATA line (time since
    the last valid frame, which arrived before the unplug). The replug moment is
    not visible in the log, so reconnect latency must be timed by the operator;
    this check only verifies that recovery happened without a reboot.
    """
    r = Result("V-11", PASS)
    boot = log.main
    if boot is None or not boot.reports:
        return Result("V-11", INCONCLUSIVE, ["no report lines"])

    verdicts = []
    episodes = 0
    reps = boot.reports
    for i in range(1, len(reps)):
        if not (reps[i].status == "NO_DATA" and reps[i - 1].status in GOOD_STATUSES):
            continue
        episodes += 1
        start = reps[i]
        age = start.num("age_ms")
        fast = age is not None and 0 <= age <= detect_max_ms
        verdicts.append(PASS if fast else FAIL)
        warned = any(e.message.startswith("sensor data lost")
                     and reps[i - 1].line_no < e.line_no < start.line_no for e in boot.events)
        verdicts.append(PASS if warned else FAIL)

        recovered = next((x for x in reps[i + 1:] if x.status in GOOD_STATUSES), None)
        if recovered is None:
            verdicts.append(FAIL)
            r.details.append("episode %d: NO_DATA at line %d, age_ms=%s (limit %d), warning=%s,"
                             " never recovered" % (episodes, start.line_no, age, detect_max_ms, warned))
            continue
        announced = any(e.message.startswith("sensor data available")
                        and start.line_no < e.line_no < recovered.line_no for e in boot.events)
        verdicts.append(PASS if announced else FAIL)
        r.details.append("episode %d: NO_DATA at line %d, age_ms=%s (limit %d), warning=%s; recovered at"
                         " line %d after %d ms of NO_DATA, 'data available'=%s"
                         % (episodes, start.line_no, age, detect_max_ms, warned, recovered.line_no,
                            recovered.t_ms - start.t_ms, announced))

    if episodes == 0:
        return Result("V-11", INCONCLUSIVE, ["no OK -> NO_DATA transition in the capture"])
    verdicts.append(PASS if log.banners <= 1 else FAIL)
    r.details.append("banners in capture: %d (expected 1); reconnect latency must be timed manually"
                     % log.banners)
    r.verdict = worst(verdicts)
    return r


def check_v12(log: Log, min_span_ms: int = 3600000) -> Result:
    """Stability: one boot, no crash/reset text, monotonic timestamps, >= 1 h."""
    r = Result("V-12", PASS)
    boot = log.main
    if boot is None or boot.banner_line is None:
        return Result("V-12", INCONCLUSIVE, ["no firmware banner in the capture"])

    verdicts = []
    verdicts.append(PASS if len(log.boots) == 1 else FAIL)
    r.details.append("boot segments: %d (expected 1)" % len(log.boots))
    verdicts.append(FAIL if log.failures else PASS)
    r.details.append("crash/reset lines after the banner: %d" % len(log.failures))
    for line_no, text in log.failures[:5]:
        r.details.append("  line %d: %s" % (line_no, text[:120]))

    backwards = [b[0] for a, b in zip(boot.timestamps, boot.timestamps[1:]) if b[1] < a[1]]
    verdicts.append(FAIL if backwards else PASS)
    r.details.append("timestamps going backwards: %d%s"
                     % (len(backwards), " (lines %s)" % backwards[:5] if backwards else ""))

    span = boot.timestamps[-1][1] - boot.timestamps[0][1] if boot.timestamps else 0
    if span < min_span_ms:
        verdicts.append(INCONCLUSIVE)
        r.details.append("run length %d ms < required %d ms" % (span, min_span_ms))
    r.verdict = worst(verdicts)
    return r


CHECKS = ("v03", "v03b", "v05", "v10", "v11", "v12")
DEFAULT_CHECKS = ("v03", "v05", "v10", "v12")


def run_checks(log: Log, checks: Iterable[str], args: argparse.Namespace) -> List[Result]:
    results = []
    for c in checks:
        if c == "v03":
            results.append(check_v03(log, args.rx_gpio, args.tx_gpio))
        elif c == "v03b":
            results.append(check_v03b(log))
        elif c == "v05":
            results.append(check_v05(log, args.period_ms))
        elif c == "v10":
            results.append(check_v10(log))
        elif c == "v11":
            results.append(check_v11(log))
        elif c == "v12":
            results.append(check_v12(log))
    return results


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logfile", help="captured console log, or '-' for stdin")
    ap.add_argument("--check", action="append", choices=CHECKS,
                    help="check to run (repeatable); default: %s" % " ".join(DEFAULT_CHECKS))
    ap.add_argument("--rx-gpio", type=int, default=16, help="expected rx_gpio in the banner (V-03)")
    ap.add_argument("--tx-gpio", type=int, default=17, help="expected tx_gpio in the banner (V-03)")
    ap.add_argument("--period-ms", type=int, default=None,
                    help="report period for V-05 (default: from the banner, else 1000)")
    args = ap.parse_args(argv)

    if args.logfile == "-":
        log = parse_log(sys.stdin)
    else:
        with open(args.logfile, "r", encoding="utf-8", errors="replace") as fh:
            log = parse_log(fh)

    main_boot = log.main
    print("banners=%d boot_segments=%d report_lines=%d"
          % (log.banners, len(log.boots), len(main_boot.reports) if main_boot else 0))

    results = run_checks(log, args.check or DEFAULT_CHECKS, args)
    for res in results:
        print("%-6s %s" % (res.check, res.verdict))
        for d in res.details:
            print("       %s" % d)

    overall = worst(res.verdict for res in results)
    return {PASS: 0, FAIL: 1, INCONCLUSIVE: 3}[overall]


if __name__ == "__main__":
    sys.exit(main())
