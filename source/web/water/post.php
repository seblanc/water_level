<?php
/*
 * ESP32 endpoint. POST application/x-www-form-urlencoded fields (firmware
 * components/web_report/web_payload.c builds them; keep both sides in sync):
 *   key, device, distance_mm (empty when NO_DATA), status, age_ms, frames_ok,
 *   checksum_err, rssi, uptime_s, fw
 * Replies 200 {"ok":true,"stored":true,"id":<row id>,"rows":<rows in table>,
 *   "distance_mm":<stored value or null>,"uptime_s":<stored value>,"level_pct":..}
 * only after the new row was read back from the database and matched the request.
 * The firmware parses ok/stored/id/rows/distance_mm/uptime_s and compares the echoed
 * values with what it sent. Errors: 400 bad field, 401 bad key, 405 not POST,
 * 500 "server not configured" (placeholder config.php), "database <stage> <SQLSTATE>
 * <code>" or "readback <field>"; all as {"ok":false,"error":"..."} (at most 31
 * characters, the firmware's log limit).
 */

/* Notices or warnings printed before the JSON would break the firmware's reply
 * check; they go to the server error log only. */
ini_set('display_errors', '0');

require __DIR__ . '/db.php';

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
    header('Allow: POST');
    wt_json(['ok' => false, 'error' => 'POST only'], 405);
}

$cfg = wt_config();
$key = (string)($_POST['key'] ?? '');
if ($cfg['api_key'] === '' || !hash_equals((string)$cfg['api_key'], $key)) {
    wt_json(['ok' => false, 'error' => 'bad key'], 401);
}

/* Optional integer field: null when absent or empty, false when malformed. */
function int_field(string $name, int $min, int $max)
{
    $v = $_POST[$name] ?? '';
    if ($v === '') {
        return null;
    }
    $i = filter_var($v, FILTER_VALIDATE_INT, ['options' => ['min_range' => $min, 'max_range' => $max]]);
    return $i === false ? false : $i;
}

$device = (string)($_POST['device'] ?? '');
$status = (string)($_POST['status'] ?? '');
$fw = substr((string)($_POST['fw'] ?? ''), 0, 32);
$row = [
    'distance_mm'  => int_field('distance_mm', 0, 65535),
    'age_ms'       => int_field('age_ms', -1, 2147483647),
    'frames_ok'    => int_field('frames_ok', 0, 4294967295),
    'checksum_err' => int_field('checksum_err', 0, 4294967295),
    'rssi'         => int_field('rssi', -127, 0),
    'uptime_s'     => int_field('uptime_s', 0, 4294967295),
];

$errors = [];
if (!preg_match('/^[0-9a-f]{12}$/', $device)) {
    $errors[] = 'device';
}
if (!in_array($status, ['OK', 'NO_DATA', 'OUT_OF_RANGE'], true)) {
    $errors[] = 'status';
}
if (!preg_match('/^[\x20-\x7E]*$/', $fw)) {
    $errors[] = 'fw';
}
foreach ($row as $name => $v) {
    if ($v === false) {
        $errors[] = $name;
    }
}
if ($status === 'NO_DATA') {
    $row['distance_mm'] = null;
} elseif ($row['distance_mm'] === null) {
    $errors[] = 'distance_mm';
}
if ($errors) {
    wt_json(['ok' => false, 'error' => 'bad field', 'fields' => $errors], 400);
}

/* config.php as shipped in the repository still holds placeholders: a copy
 * uploaded without editing can never reach the database. */
$dsn = (string)($cfg['db_dsn'] ?? '');
if ($dsn === '' || strpos($dsn, 'YOUR_DB') !== false
    || (strncmp($dsn, 'sqlite:', 7) !== 0 && ($cfg['db_user'] ?? '') === 'YOUR_DB_USER')) {
    error_log('water/post.php: config.php still has the placeholder database settings');
    wt_json(['ok' => false, 'error' => 'server not configured'], 500);
}

/*
 * Short cause for the 500 reply, e.g. "database open HY000 1045" (stage,
 * SQLSTATE, driver code). The firmware logs it in its "post rejected" line
 * (max 31 characters, only [A-Za-z0-9 ._-] kept); the full message goes to the
 * PHP error log only.
 */
function db_error(string $stage, Throwable $e): string
{
    $msg = $e->getMessage();
    $out = $stage;
    /* "SQLSTATE[HY000] [1045] Access denied", "SQLSTATE[42S22]: ...: 1054 Unknown
     * column", "SQLSTATE[HY000]: General error: 1 no such table" (SQLite). */
    if (preg_match('/SQLSTATE\[([0-9A-Z]{5})\]\D*(\d+)?/', $msg, $m)) {
        $out .= ' ' . $m[1] . (isset($m[2]) ? ' ' . $m[2] : '');
    }
    return substr($out, 0, 31);
}

$stage = 'database open';
try {
    $pdo = wt_db();
    $t = wt_table();
    $stage = 'database insert';
    $stmt = $pdo->prepare("INSERT INTO $t
        (received_at, device, distance_mm, status, age_ms, frames_ok, checksum_err, rssi, uptime_s, fw)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    $stmt->execute([
        time(), $device, $row['distance_mm'], $status, $row['age_ms'], $row['frames_ok'],
        $row['checksum_err'], $row['rssi'], $row['uptime_s'], $fw,
    ]);
    $id = (int)$pdo->lastInsertId();

    /* Read the row back: success is only reported for data that is really in the
     * table. The stored values are echoed so the ESP32 can compare them too. */
    $stage = 'database read';
    $read = $pdo->prepare("SELECT device, distance_mm, status, uptime_s FROM $t WHERE id = ?");
    $read->execute([$id]);
    $stored = $read->fetch();
    $read->closeCursor();
    $stored_dist = ($stored && $stored['distance_mm'] !== null) ? (int)$stored['distance_mm'] : null;
    $stored_up = ($stored && $stored['uptime_s'] !== null) ? (int)$stored['uptime_s'] : null;
    /* The reply names the first field that differs, e.g. "readback distance_mm". */
    $bad = null;
    if ($id <= 0 || !$stored) {
        $bad = 'row missing';
    } elseif ((string)$stored['device'] !== $device) {
        $bad = 'device';
    } elseif ((string)$stored['status'] !== $status) {
        $bad = 'status';
    } elseif ($stored_dist !== $row['distance_mm']) {
        $bad = 'distance_mm';
    } elseif ($stored_up !== $row['uptime_s']) {
        $bad = 'uptime_s';
    }
    if ($bad !== null) {
        error_log("water/post.php: readback of row $id does not match the request ($bad)");
        wt_json(['ok' => false, 'error' => "readback $bad"], 500);
    }
    $rows = (int)$pdo->query("SELECT COUNT(*) FROM $t")->fetchColumn();

    /* Occasional pruning, about once per 500 posts. A failure here does not
     * undo the stored row, so it is only logged. */
    $keep_days = (int)($cfg['keep_days'] ?? 0);
    if ($keep_days > 0 && random_int(1, 500) === 1) {
        try {
            $pdo->prepare("DELETE FROM $t WHERE received_at < ?")
                ->execute([time() - $keep_days * 86400]);
        } catch (Throwable $e) {
            error_log('water/post.php: prune: ' . $e->getMessage());
        }
    }
} catch (Throwable $e) {
    error_log("water/post.php: $stage: " . $e->getMessage());
    wt_json(['ok' => false, 'error' => db_error($stage, $e)], 500);
}

wt_json([
    'ok' => true,
    'stored' => true,
    'id' => $id,
    'rows' => $rows,
    'distance_mm' => $stored_dist,
    'uptime_s' => $stored_up,
    'level_pct' => wt_level_percent($row['distance_mm']),
]);
