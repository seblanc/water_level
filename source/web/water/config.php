<?php
/*
 * Water tank server settings. Edit this file on the server, then keep it out
 * of future copy-paste updates (it holds the database password).
 */
if (!defined('WT_APP')) {
    http_response_code(403);
    exit;
}

return [
    /*
     * Database (PDO DSN). MySQL/MariaDB on the hosting:
     *   'db_dsn' => 'mysql:host=localhost;dbname=YOUR_DB;charset=utf8mb4',
     * Or SQLite with no database server (the data/ folder must be writable):
     *   'db_dsn' => 'sqlite:' . __DIR__ . '/data/water.sqlite',
     * The table is created automatically on the first request.
     */
    'db_dsn'  => 'mysql:host=localhost;dbname=YOUR_DB;charset=utf8mb4',
    'db_user' => 'YOUR_DB_USER',
    'db_pass' => 'YOUR_DB_PASSWORD',
    'db_table' => 'water_readings',

    /* Must equal CONFIG_WT_WEB_API_KEY in the firmware (sdkconfig.defaults). */
    'api_key' => 'wt-q7m2x9k4p8v3n6c1',

    /*
     * Tank geometry: sensor-to-water distance in cm when the tank is empty and
     * when it is full. Provisional values; change them here, no reflash needed.
     */
    'empty_cm' => 30.0,
    'full_cm'  => 5.0,

    /* Page title and display time zone. */
    'title'    => 'Water tank',
    'timezone' => 'Europe/Zurich',

    /* The page shows "no recent data" after this many seconds without a post. */
    'stale_after_s' => 300,

    /* Rows older than this are deleted (0 = keep everything). */
    'keep_days' => 365,
];
