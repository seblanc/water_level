<?php
/*
 * Read-only JSON for the web page (index.php refreshes from here).
 *   GET api.php?hours=24   (1 .. 2160)
 * Reply: {"ok":true,"latest":{...}|null,"history":[[unix,level_pct],...],
 *         "empty_cm":30,"full_cm":5}
 */
require __DIR__ . '/db.php';

$hours = filter_var($_GET['hours'] ?? 24, FILTER_VALIDATE_INT,
    ['options' => ['min_range' => 1, 'max_range' => 2160]]);
if ($hours === false) {
    $hours = 24;
}

try {
    $cfg = wt_config();
    wt_json([
        'ok' => true,
        'latest' => wt_latest(),
        'history' => wt_history($hours),
        'hours' => $hours,
        'empty_cm' => (float)$cfg['empty_cm'],
        'full_cm' => (float)$cfg['full_cm'],
    ]);
} catch (Throwable $e) {
    error_log('water/api.php: ' . $e->getMessage());
    wt_json(['ok' => false, 'error' => 'database'], 500);
}
