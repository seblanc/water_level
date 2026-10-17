<?php
/*
 * Shared helpers: settings, database connection, level computation.
 * Included by post.php, api.php and index.php; not meant to be opened directly.
 */
if (basename($_SERVER['SCRIPT_FILENAME'] ?? '') === basename(__FILE__)) {
    http_response_code(403);
    exit;
}

define('WT_APP', true);

function wt_config(): array
{
    static $cfg = null;
    if ($cfg === null) {
        $cfg = require __DIR__ . '/config.php';
        date_default_timezone_set($cfg['timezone'] ?? 'UTC');
    }
    return $cfg;
}

function wt_table(): string
{
    $name = wt_config()['db_table'] ?? 'water_readings';
    if (!preg_match('/^[A-Za-z0-9_]{1,64}$/', $name)) {
        throw new RuntimeException('invalid db_table name');
    }
    return $name;
}

function wt_db(): PDO
{
    static $pdo = null;
    if ($pdo !== null) {
        return $pdo;
    }
    $cfg = wt_config();
    $pdo = new PDO($cfg['db_dsn'], $cfg['db_user'] ?? null, $cfg['db_pass'] ?? null, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        PDO::ATTR_EMULATE_PREPARES => false,
    ]);
    wt_ensure_schema($pdo);
    return $pdo;
}

/* Creates the table on first use. received_at is a Unix timestamp (UTC). */
function wt_ensure_schema(PDO $pdo): void
{
    $t = wt_table();
    if ($pdo->getAttribute(PDO::ATTR_DRIVER_NAME) === 'sqlite') {
        $pdo->exec("CREATE TABLE IF NOT EXISTS $t (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            received_at INTEGER NOT NULL,
            device TEXT NOT NULL,
            distance_mm INTEGER NULL,
            status TEXT NOT NULL,
            age_ms INTEGER NULL,
            frames_ok INTEGER NULL,
            checksum_err INTEGER NULL,
            rssi INTEGER NULL,
            uptime_s INTEGER NULL,
            fw TEXT NULL)");
        $pdo->exec("CREATE INDEX IF NOT EXISTS idx_{$t}_received ON $t (received_at)");
        return;
    }
    $pdo->exec("CREATE TABLE IF NOT EXISTS `$t` (
        id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
        received_at INT UNSIGNED NOT NULL,
        device VARCHAR(16) NOT NULL,
        distance_mm INT NULL,
        status VARCHAR(16) NOT NULL,
        age_ms INT NULL,
        frames_ok INT UNSIGNED NULL,
        checksum_err INT UNSIGNED NULL,
        rssi SMALLINT NULL,
        uptime_s INT UNSIGNED NULL,
        fw VARCHAR(32) NULL,
        INDEX idx_received (received_at)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
}

/*
 * Fill level in percent (0..100) from the sensor distance, or null without a
 * distance. empty_cm > full_cm: a larger distance means less water.
 */
function wt_level_percent(?int $distance_mm): ?float
{
    if ($distance_mm === null) {
        return null;
    }
    $cfg = wt_config();
    $empty = (float)$cfg['empty_cm'];
    $full = (float)$cfg['full_cm'];
    if ($empty <= $full) {
        return null;
    }
    $pct = ($empty - $distance_mm / 10.0) / ($empty - $full) * 100.0;
    return round(max(0.0, min(100.0, $pct)), 1);
}

/* Newest row as shown by index.php / api.php, or null when the table is empty. */
function wt_latest(): ?array
{
    $t = wt_table();
    $row = wt_db()->query("SELECT * FROM $t ORDER BY id DESC LIMIT 1")->fetch();
    if (!$row) {
        return null;
    }
    $cfg = wt_config();
    $dist = $row['distance_mm'] === null ? null : (int)$row['distance_mm'];
    $at = (int)$row['received_at'];
    $age = max(0, time() - $at);
    return [
        'received_at' => $at,
        'received_iso' => date('c', $at),
        'age_s' => $age,
        'stale' => $age > (int)($cfg['stale_after_s'] ?? 300),
        'status' => (string)$row['status'],
        'distance_cm' => $dist === null ? null : round($dist / 10.0, 1),
        'level_pct' => wt_level_percent($dist),
        'rssi' => $row['rssi'] === null ? null : (int)$row['rssi'],
        'uptime_s' => $row['uptime_s'] === null ? null : (int)$row['uptime_s'],
        'fw' => (string)($row['fw'] ?? ''),
        'device' => (string)$row['device'],
    ];
}

/*
 * Level history for the last $hours, averaged into at most $points buckets.
 * Returns [[unix_time, level_pct], ...] in time order; NO_DATA rows are skipped.
 */
function wt_history(int $hours, int $points = 300): array
{
    $t = wt_table();
    $since = time() - $hours * 3600;
    $bucket = max(60, intdiv($hours * 3600, $points));
    $stmt = wt_db()->prepare("SELECT received_at, distance_mm FROM $t
        WHERE received_at >= ? AND distance_mm IS NOT NULL ORDER BY received_at");
    $stmt->execute([$since]);

    $sums = [];
    while ($r = $stmt->fetch()) {
        $b = intdiv((int)$r['received_at'] - $since, $bucket);
        $sums[$b][0] = ($sums[$b][0] ?? 0) + (int)$r['distance_mm'];
        $sums[$b][1] = ($sums[$b][1] ?? 0) + 1;
    }
    $out = [];
    foreach ($sums as $b => [$sum, $n]) {
        $out[] = [$since + $b * $bucket + intdiv($bucket, 2), wt_level_percent((int)round($sum / $n))];
    }
    return $out;
}

function wt_json(array $data, int $code = 200): void
{
    http_response_code($code);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    echo json_encode($data, JSON_UNESCAPED_SLASHES);
    exit;
}
