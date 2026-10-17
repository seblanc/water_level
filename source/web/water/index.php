<?php
/*
 * Water tank page. Rendered once on the server, then refreshed from api.php
 * every 30 s by the script at the bottom (no external libraries or fonts).
 */
require __DIR__ . '/db.php';

$cfg = wt_config();
$latest = null;
$history = [];
$db_error = false;
try {
    $latest = wt_latest();
    $history = wt_history(24);
} catch (Throwable $e) {
    error_log('water/index.php: ' . $e->getMessage());
    $db_error = true;
}

function h($s): string
{
    return htmlspecialchars((string)$s, ENT_QUOTES, 'UTF-8');
}

$initial = [
    'ok' => !$db_error,
    'latest' => $latest,
    'history' => $history,
    'hours' => 24,
    'empty_cm' => (float)$cfg['empty_cm'],
    'full_cm' => (float)$cfg['full_cm'],
];
header('Cache-Control: no-store');
?><!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title><?= h($cfg['title'] ?? 'Water tank') ?></title>
<style>
:root {
    --bg: #0b1622; --card: #12233a; --line: #1f3654; --text: #e6eef8; --muted: #8aa3c0;
    --water: #2f9bff; --water2: #1366c9; --ok: #3ccf8e; --warn: #ffb547; --bad: #ff5d6c;
}
* { box-sizing: border-box; }
body {
    margin: 0; min-height: 100vh; font: 16px/1.4 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
    color: var(--text); background: radial-gradient(1200px 600px at 20% -10%, #17324f 0%, var(--bg) 60%);
    display: flex; justify-content: center; padding: 24px 16px;
}
main { width: 100%; max-width: 880px; }
header { display: flex; align-items: baseline; justify-content: space-between; gap: 12px; margin-bottom: 18px; }
h1 { font-size: 1.6rem; margin: 0; letter-spacing: .02em; }
.badge { font-size: .8rem; padding: 4px 10px; border-radius: 999px; background: var(--line); color: var(--muted); white-space: nowrap; }
.badge.ok { background: rgba(60, 207, 142, .15); color: var(--ok); }
.badge.warn { background: rgba(255, 181, 71, .15); color: var(--warn); }
.badge.bad { background: rgba(255, 93, 108, .15); color: var(--bad); }
.card { background: var(--card); border: 1px solid var(--line); border-radius: 18px; padding: 22px; box-shadow: 0 10px 30px rgba(0, 0, 0, .25); }
.top { display: grid; grid-template-columns: 220px 1fr; gap: 28px; align-items: center; }
@media (max-width: 620px) { .top { grid-template-columns: 1fr; justify-items: center; } }

.tank {
    position: relative; width: 180px; height: 260px; margin: 0 auto; border-radius: 22px 22px 28px 28px;
    border: 3px solid #4d6f96; background: linear-gradient(180deg, #0e1c2e, #0a1523); overflow: hidden;
}
.water {
    position: absolute; left: 0; right: 0; bottom: 0; height: 0%;
    background: linear-gradient(180deg, var(--water), var(--water2)); transition: height 1.2s cubic-bezier(.2, .8, .2, 1);
}
.water::before, .water::after {
    content: ""; position: absolute; left: -50%; width: 200%; height: 24px; top: -12px; border-radius: 45%;
    background: var(--water); opacity: .55; animation: wave 6s linear infinite;
}
.water::after { opacity: .35; animation-duration: 9s; top: -9px; }
@keyframes wave { from { transform: translateX(0) rotate(0); } to { transform: translateX(25%) rotate(360deg); } }
.tank.nodata .water { background: linear-gradient(180deg, #4d5d72, #34414f); }
.tank.nodata .water::before, .tank.nodata .water::after { background: #4d5d72; }
.ticks { position: absolute; inset: 0; pointer-events: none; }
.ticks span { position: absolute; right: 8px; font-size: .7rem; color: rgba(255, 255, 255, .55); transform: translateY(50%); }
.ticks span::after { content: ""; display: inline-block; width: 10px; height: 1px; background: rgba(255, 255, 255, .4); margin-left: 4px; vertical-align: middle; }

.pct { font-size: 4.2rem; font-weight: 700; line-height: 1; letter-spacing: -.02em; }
.pct small { font-size: 1.6rem; font-weight: 500; color: var(--muted); margin-left: 4px; }
.sub { color: var(--muted); margin: 6px 0 18px; }
.stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 12px; }
.stat { background: rgba(255, 255, 255, .03); border: 1px solid var(--line); border-radius: 12px; padding: 10px 12px; }
.stat b { display: block; font-size: 1.15rem; }
.stat span { font-size: .78rem; color: var(--muted); text-transform: uppercase; letter-spacing: .06em; }

.chart-card { margin-top: 18px; }
.chart-head { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
.chart-head h2 { font-size: 1rem; margin: 0; color: var(--muted); font-weight: 600; }
.range button {
    background: transparent; color: var(--muted); border: 1px solid var(--line); border-radius: 8px;
    padding: 4px 10px; margin-left: 4px; cursor: pointer; font: inherit; font-size: .85rem;
}
.range button.on { background: var(--water2); border-color: var(--water2); color: #fff; }
svg { width: 100%; height: 220px; display: block; }
.empty-chart { fill: var(--muted); font-size: 13px; }
footer { color: var(--muted); font-size: .78rem; text-align: center; margin-top: 18px; }
</style>
</head>
<body>
<main>
    <header>
        <h1><?= h($cfg['title'] ?? 'Water tank') ?></h1>
        <span id="badge" class="badge">loading…</span>
    </header>

    <section class="card top">
        <div class="tank" id="tank">
            <div class="water" id="water"></div>
            <div class="ticks">
                <span style="bottom:100%">100%</span>
                <span style="bottom:75%">75</span>
                <span style="bottom:50%">50</span>
                <span style="bottom:25%">25</span>
            </div>
        </div>
        <div>
            <div class="pct" id="pct">–<small>%</small></div>
            <div class="sub" id="sub">&nbsp;</div>
            <div class="stats">
                <div class="stat"><span>Distance</span><b id="dist">–</b></div>
                <div class="stat"><span>Last update</span><b id="ago">–</b></div>
                <div class="stat"><span>Wi-Fi signal</span><b id="rssi">–</b></div>
                <div class="stat"><span>Sensor</span><b id="status">–</b></div>
            </div>
        </div>
    </section>

    <section class="card chart-card">
        <div class="chart-head">
            <h2>Level history</h2>
            <div class="range" id="range">
                <button data-h="24" class="on">24 h</button><button data-h="168">7 d</button><button data-h="720">30 d</button>
            </div>
        </div>
        <svg id="chart" viewBox="0 0 800 220" preserveAspectRatio="none" role="img" aria-label="Level history"></svg>
    </section>

    <footer id="foot">&nbsp;</footer>
</main>

<script>
(function () {
    'use strict';
    var state = <?= json_encode($initial, JSON_UNESCAPED_SLASHES | JSON_HEX_TAG | JSON_HEX_AMP) ?>;
    var hours = 24;
    var $ = function (id) { return document.getElementById(id); };

    function ago(s) {
        if (s < 60) return s + ' s ago';
        if (s < 3600) return Math.round(s / 60) + ' min ago';
        if (s < 86400) return Math.round(s / 3600) + ' h ago';
        return Math.round(s / 86400) + ' d ago';
    }

    function signal(rssi) {
        if (rssi === null || rssi === 0) return '–';
        var q = rssi >= -60 ? 'good' : rssi >= -72 ? 'fair' : 'weak';
        return rssi + ' dBm · ' + q;
    }

    function render() {
        var L = state.latest, badge = $('badge'), tank = $('tank');
        $('foot').textContent = 'Empty = ' + state.empty_cm + ' cm · Full = ' + state.full_cm +
            ' cm (sensor to water surface)';
        if (!state.ok) {
            badge.className = 'badge bad'; badge.textContent = 'server error';
            return;
        }
        if (!L) {
            badge.className = 'badge'; badge.textContent = 'waiting for first reading';
            $('sub').textContent = 'No data received from the ESP32 yet.';
            return;
        }
        var has = L.level_pct !== null;
        $('water').style.height = (has ? L.level_pct : 0) + '%';
        tank.className = 'tank' + (has && !L.stale ? '' : ' nodata');
        $('pct').innerHTML = (has ? Math.round(L.level_pct) : '–') + '<small>%</small>';
        $('dist').textContent = L.distance_cm !== null ? L.distance_cm.toFixed(1) + ' cm' : '–';
        $('ago').textContent = ago(L.age_s);
        $('rssi').textContent = signal(L.rssi);
        $('status').textContent = L.status === 'OK' ? 'OK' : L.status === 'NO_DATA' ? 'no data' : 'out of range';

        if (L.stale) {
            badge.className = 'badge warn'; badge.textContent = 'no recent data';
        } else if (L.status !== 'OK') {
            badge.className = 'badge bad'; badge.textContent = 'sensor problem';
        } else {
            badge.className = 'badge ok'; badge.textContent = 'live';
        }
        var when = new Date(L.received_at * 1000);
        $('sub').textContent = 'Measured ' + when.toLocaleString() + (has && L.level_pct <= 15 ? ' · low level' : '');
        drawChart();
    }

    function drawChart() {
        var svg = $('chart'), W = 800, H = 220, P = 28, pts = state.history || [];
        var ns = 'http://www.w3.org/2000/svg', out = [];
        var t1 = Date.now() / 1000, t0 = t1 - state.hours * 3600;
        var x = function (t) { return P + (t - t0) / (t1 - t0) * (W - 2 * P); };
        var y = function (v) { return 8 + (100 - v) / 100 * (H - 8 - P); };

        [0, 25, 50, 75, 100].forEach(function (v) {
            out.push('<line x1="' + P + '" x2="' + (W - P) + '" y1="' + y(v) + '" y2="' + y(v) +
                '" stroke="#1f3654" stroke-width="1"/>');
            out.push('<text x="2" y="' + (y(v) + 4) + '" fill="#8aa3c0" font-size="11">' + v + '%</text>');
        });
        if (pts.length < 2) {
            out.push('<text class="empty-chart" x="' + W / 2 + '" y="' + H / 2 +
                '" text-anchor="middle">Not enough data for this period yet</text>');
            svg.innerHTML = out.join('');
            return;
        }
        var line = pts.map(function (p) { return x(p[0]).toFixed(1) + ',' + y(p[1]).toFixed(1); }).join(' ');
        var area = x(pts[0][0]).toFixed(1) + ',' + y(0) + ' ' + line + ' ' + x(pts[pts.length - 1][0]).toFixed(1) + ',' + y(0);
        out.push('<defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1">' +
            '<stop offset="0" stop-color="#2f9bff" stop-opacity=".45"/>' +
            '<stop offset="1" stop-color="#2f9bff" stop-opacity="0"/></linearGradient></defs>');
        out.push('<polygon points="' + area + '" fill="url(#g)"/>');
        out.push('<polyline points="' + line + '" fill="none" stroke="#2f9bff" stroke-width="2.5" ' +
            'stroke-linejoin="round" vector-effect="non-scaling-stroke"/>');

        var fmt = state.hours <= 24 ? { hour: '2-digit', minute: '2-digit' } : { day: 'numeric', month: 'short' };
        for (var i = 0; i <= 4; i++) {
            var t = t0 + (t1 - t0) * i / 4;
            out.push('<text x="' + x(t) + '" y="' + (H - 6) + '" fill="#8aa3c0" font-size="11" text-anchor="' +
                (i === 0 ? 'start' : i === 4 ? 'end' : 'middle') + '">' +
                new Date(t * 1000).toLocaleString([], fmt) + '</text>');
        }
        svg.innerHTML = out.join('');
    }

    function refresh() {
        fetch('api.php?hours=' + hours, { cache: 'no-store' })
            .then(function (r) { return r.json(); })
            .then(function (d) { state = d; render(); })
            .catch(function () { $('badge').className = 'badge bad'; $('badge').textContent = 'offline'; });
    }

    $('range').addEventListener('click', function (e) {
        var b = e.target.closest('button');
        if (!b) return;
        hours = parseInt(b.getAttribute('data-h'), 10);
        Array.prototype.forEach.call(this.children, function (c) { c.classList.toggle('on', c === b); });
        refresh();
    });

    render();
    setInterval(refresh, 30000);
})();
</script>
</body>
</html>
