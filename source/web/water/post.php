<?php
/*
 * ESP32 endpoint. POST application/x-www-form-urlencoded fields (firmware
 * components/web_report/web_payload.c builds them; keep both sides in sync):
 *   key, device, distance_mm (empty when NO_DATA), status, age_ms, frames_ok,
 *   checksum_err, rssi, uptime_s, fw
 * Replies 200 {"ok":true,"id":<row id>,"rows":<rows in table>,"level_pct":..}
 * (the firmware parses ok/id/rows), 400 bad field, 401 bad key, 405 not POST,
 * 500 database error.
 */
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

try {
    $pdo = wt_db();
    $t = wt_table();
    $stmt = $pdo->prepare("INSERT INTO $t
        (received_at, device, distance_mm, status, age_ms, frames_ok, checksum_err, rssi, uptime_s, fw)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    $stmt->execute([
        time(), $device, $row['distance_mm'], $status, $row['age_ms'], $row['frames_ok'],
        $row['checksum_err'], $row['rssi'], $row['uptime_s'], $fw,
    ]);
    /* Echoed to the ESP32, which logs them as proof the row was stored. */
    $id = (int)$pdo->lastInsertId();
    $rows = (int)$pdo->query("SELECT COUNT(*) FROM $t")->fetchColumn();

    /* Occasional pruning, about once per 500 posts. */
    $keep_days = (int)($cfg['keep_days'] ?? 0);
    if ($keep_days > 0 && random_int(1, 500) === 1) {
        $pdo->prepare("DELETE FROM $t WHERE received_at < ?")
            ->execute([time() - $keep_days * 86400]);
    }
} catch (Throwable $e) {
    error_log('water/post.php: ' . $e->getMessage());
    wt_json(['ok' => false, 'error' => 'database'], 500);
}

wt_json(['ok' => true, 'id' => $id, 'rows' => $rows, 'level_pct' => wt_level_percent($row['distance_mm'])]);
