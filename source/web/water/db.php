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

function wt_json(array $data, int $code = 200): void
{
    http_response_code($code);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    echo json_encode($data, JSON_UNESCAPED_SLASHES);
    exit;
}
