# Water tank web server (https://randoresto.ch/water)

PHP files that receive readings from the ESP32 and show the tank level.
Copy the whole `water/` folder (including the hidden `.htaccess` files) to the web root, so that
`https://randoresto.ch/water/post.php` exists.

| File | Purpose |
|---|---|
| `index.php` | Web page: tank graphic, level %, distance, last update, 24 h / 7 d / 30 d history. Refreshes every 30 s. |
| `post.php` | ESP32 endpoint (form POST, API key checked). Stores one row per post. |
| `api.php` | Read-only JSON used by the page (`api.php?hours=24`). |
| `db.php` | Shared helpers (database, level computation). Not opened directly. |
| `config.php` | **Your settings**: database, API key, tank limits. |
| `.htaccess`, `data/.htaccess` | Block direct access to settings/helpers and the SQLite folder (Apache). |

## Setup

1. Create a MySQL/MariaDB database on the hosting (or use SQLite, see `config.php`).
2. Edit `config.php`: `db_dsn`, `db_user`, `db_pass`.
3. `api_key` must equal `CONFIG_WT_WEB_API_KEY` in the firmware's `sdkconfig.defaults`
   (or the NVS key `web_key`). Change both together if you change it.
4. Upload. The table `water_readings` is created automatically on the first request.
5. Open `https://randoresto.ch/water/`: it shows "waiting for first reading" until the ESP32 posts.

On later updates, do not overwrite your edited `config.php`.

## Tank limits

`empty_cm` (30) and `full_cm` (5) in `config.php` are the sensor-to-water distances for an empty
and a full tank. They are provisional: change them in `config.php` and the page updates at once,
also for old readings (only the raw distance is stored). No firmware change is needed.

Level % = (empty_cm − distance) / (empty_cm − full_cm) × 100, clamped to 0..100.

## Testing the endpoint by hand

```sh
curl -i -X POST https://randoresto.ch/water/post.php \
  -d key=YOUR_API_KEY -d device=0123456789ab -d distance_mm=175 -d status=OK \
  -d age_ms=50 -d frames_ok=100 -d checksum_err=0 -d rssi=-60 -d uptime_s=120 -d fw=test
```

Expected reply (since run #20):
`{"ok":true,"stored":true,"id":<new row id>,"rows":<rows in table>,"distance_mm":175,"uptime_s":120,"level_pct":50.0}`
(PHP 7.1+; older PHP prints `50`). `post.php` reads the new row back from the database and
only answers `"ok":true` when the stored values match the request; otherwise it answers
500 `{"ok":false,"error":"readback"}`. The firmware checks the echoed `distance_mm` and
`uptime_s` against what it sent and only then logs the post as stored. The page header of the history
chart shows how many readings arrived in the selected period. Wrong key → 401, missing/invalid field → 400,
database problem → 500 (details in the server's PHP error log).

### HTTP 500: what the `error` text means (since run #23)

The ESP32 prints it in its UART line `web: post rejected: http 500 (<error>)`.

| `error` | Cause | Fix |
|---|---|---|
| `server not configured` | `config.php` still has `YOUR_DB…` placeholders (repository copy uploaded) | Put the real `db_dsn`/`db_user`/`db_pass` in `config.php` on the server |
| `database open HY000 1045` | Wrong database user/password | `db_user`/`db_pass` |
| `database open HY000 1049` | Unknown database name | `dbname=` in `db_dsn` |
| `database open HY000 2002` | Database host unreachable | `host=` in `db_dsn` (often `localhost`) |
| `database open 42000 1142` | User may not `CREATE TABLE` | Grant CREATE, or create the table by hand (schema in `db.php`) |
| `database insert 42S22 1054` | Table from an older version lacks a column | Add the column (see `db.php`) or drop the empty table |
| `readback <field>` | Row stored, but read back with another value | Check column types against `db.php` |
| `no server message` (firmware text) | PHP did not run or crashed before replying | PHP error log; PHP ≥ 7.4 with PDO needed |
Delete test rows afterwards (`DELETE FROM water_readings WHERE device='0123456789ab'`).

## Requirements / assumptions

- PHP ≥ 7.4 with PDO (pdo_mysql or pdo_sqlite). Not tested on the actual hosting.
- `.htaccess` rules assume Apache 2.4; on nginx, deny `config.php`, `db.php` and `data/` in the
  server config (the PHP files also refuse direct access themselves).
- The page is public (no login). Only `post.php` requires the key.
