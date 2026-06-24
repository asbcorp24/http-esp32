<?php
declare(strict_types=1);

require_once __DIR__ . "/config.php";
require_once __DIR__ . "/crypto.php";
require_once __DIR__ . "/logger.php";

header("Content-Type: application/json; charset=utf-8");

$rawBody = file_get_contents("php://input");

function json_ok(array $data, int $code = 200): void {
    if (defined("DEBUG_LOG") && DEBUG_LOG) {
        log_line("RESPONSE", ["code" => $code, "data" => $data]);
    }
    http_response_code($code);
    echo json_encode($data, JSON_UNESCAPED_UNICODE);
    exit;
}

function pdo(): PDO {
    static $pdo = null;
    if ($pdo instanceof PDO) {
        return $pdo;
    }

    $pdo = new PDO("sqlite:" . DB_PATH);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    return $pdo;
}

function has_column(PDO $db, string $table, string $column): bool {
    $st = $db->query("PRAGMA table_info($table)");
    foreach ($st->fetchAll(PDO::FETCH_ASSOC) as $row) {
        if (($row["name"] ?? "") === $column) {
            return true;
        }
    }
    return false;
}

function ensure_column(PDO $db, string $table, string $column, string $definition): void {
    if (!has_column($db, $table, $column)) {
        $db->exec("ALTER TABLE $table ADD COLUMN $column $definition");
    }
}

function init_db(): void {
    $db = pdo();

    $db->exec("
        CREATE TABLE IF NOT EXISTS devices (
            id TEXT PRIMARY KEY,
            created INTEGER
        );
    ");

    $db->exec("
        CREATE TABLE IF NOT EXISTS data (
            device_id TEXT,
            ts INTEGER,
            current1_mA INTEGER,
            current2_mA INTEGER,
            current3_mA INTEGER,
            power_dW INTEGER,
            temp1_cC INTEGER,
            temp2_cC INTEGER,
            phase_imbalance_dPct INTEGER,
            relay_on INTEGER,
            heater_on INTEGER,
            phase_trip INTEGER,
            relay_cmd_on INTEGER
        );
    ");

    $db->exec("
        CREATE TABLE IF NOT EXISTS nonces (
            device_id TEXT,
            nonce TEXT,
            ts INTEGER
        );
    ");

    $db->exec("
        CREATE TABLE IF NOT EXISTS device_control (
            device_id TEXT PRIMARY KEY,
            relay_on INTEGER NOT NULL DEFAULT 0,
            updated INTEGER NOT NULL DEFAULT 0
        );
    ");

    ensure_column($db, "data", "current1_mA", "INTEGER");
    ensure_column($db, "data", "current2_mA", "INTEGER");
    ensure_column($db, "data", "current3_mA", "INTEGER");
    ensure_column($db, "data", "power_dW", "INTEGER");
    ensure_column($db, "data", "temp1_cC", "INTEGER");
    ensure_column($db, "data", "temp2_cC", "INTEGER");
    ensure_column($db, "data", "phase_imbalance_dPct", "INTEGER");
    ensure_column($db, "data", "relay_on", "INTEGER");
    ensure_column($db, "data", "heater_on", "INTEGER");
    ensure_column($db, "data", "phase_trip", "INTEGER");
    ensure_column($db, "data", "relay_cmd_on", "INTEGER");

    $db->exec("CREATE INDEX IF NOT EXISTS idx_data_device_ts ON data(device_id, ts);");
    $db->exec("CREATE INDEX IF NOT EXISTS idx_nonces_device_nonce ON nonces(device_id, nonce);");
}

function is_registered(string $device_id): bool {
    $st = pdo()->prepare("SELECT 1 FROM devices WHERE id=? LIMIT 1");
    $st->execute([$device_id]);
    return (bool)$st->fetchColumn();
}

function register_device(string $device_id): void {
    $db = pdo();

    $st = $db->prepare("INSERT OR IGNORE INTO devices(id, created) VALUES (?, ?)");
    $st->execute([$device_id, time()]);

    $st = $db->prepare("INSERT OR IGNORE INTO device_control(device_id, relay_on, updated) VALUES (?, 0, ?)");
    $st->execute([$device_id, time()]);
}

function check_nonce(string $device_id, string $nonce): bool {
    $db = pdo();

    $st = $db->prepare("SELECT 1 FROM nonces WHERE device_id=? AND nonce=? LIMIT 1");
    $st->execute([$device_id, $nonce]);
    if ($st->fetchColumn()) {
        return false;
    }

    $ins = $db->prepare("INSERT INTO nonces(device_id, nonce, ts) VALUES (?, ?, ?)");
    $ins->execute([$device_id, $nonce, time()]);
    return true;
}

function get_relay_desired(string $device_id): bool {
    $st = pdo()->prepare("SELECT relay_on FROM device_control WHERE device_id=? LIMIT 1");
    $st->execute([$device_id]);
    return (bool)$st->fetchColumn();
}

function set_relay_desired(string $device_id, bool $state): void {
    register_device($device_id);
    $st = pdo()->prepare("
        INSERT INTO device_control(device_id, relay_on, updated)
        VALUES (?, ?, ?)
        ON CONFLICT(device_id) DO UPDATE SET relay_on=excluded.relay_on, updated=excluded.updated
    ");
    $st->execute([$device_id, $state ? 1 : 0, time()]);
}

function parse_bool_state($value): ?bool {
    if (is_bool($value)) {
        return $value;
    }
    if (is_int($value) || ctype_digit((string)$value)) {
        return ((int)$value) !== 0;
    }

    $value = strtolower(trim((string)$value));
    if (in_array($value, ["1", "on", "true", "enable", "enabled"], true)) {
        return true;
    }
    if (in_array($value, ["0", "off", "false", "disable", "disabled"], true)) {
        return false;
    }
    return null;
}

function decrypt_payload_or_400(string $rawBody): array {
    if ($rawBody === "") {
        json_ok(["status" => "empty"], 400);
    }

    [$ok, $plain, $err] = aes_decrypt_blob(SERVER_CRYPTO_PASS, $rawBody);
    if (!$ok) {
        json_ok(["status" => "badenc", "err" => $err], 400);
    }

    $payload = json_decode($plain, true);
    if (!is_array($payload)) {
        json_ok(["status" => "badjson"], 400);
    }

    return $payload;
}

try {
    init_db();

    $method = $_SERVER["REQUEST_METHOD"] ?? "GET";
    $uri = $_SERVER["REQUEST_URI"] ?? "/";
    $path = parse_url($uri, PHP_URL_PATH) ?? "/";
    $path = preg_replace("~^/index\.php~", "", $path);
    if ($path === "") {
        $path = "/";
    }

    if (($method === "GET" || $method === "POST") && $path === "/sync_time") {
        json_ok(["ts" => time()]);
    }

    if (($method === "GET" || $method === "POST") && $path === "/relay_set") {
        $device_id = (string)($_REQUEST["device_id"] ?? "");
        $state = parse_bool_state($_REQUEST["state"] ?? null);

        if ($device_id === "" || $state === null) {
            json_ok(["status" => "badreq"], 400);
        }

        set_relay_desired($device_id, $state);
        json_ok(["status" => "OK", "device_id" => $device_id, "relay_on" => $state]);
    }

    if (($method === "GET" || $method === "POST") && $path === "/relay_state") {
        $device_id = (string)($_REQUEST["device_id"] ?? "");
        if ($device_id === "") {
            json_ok(["status" => "badreq"], 400);
        }

        json_ok(["status" => "OK", "device_id" => $device_id, "relay_on" => get_relay_desired($device_id)]);
    }

    if ($method !== "POST") {
        json_ok(["status" => "nf"], 404);
    }

    $payload = decrypt_payload_or_400((string)$rawBody);

    $device_id = (string)($payload["device_id"] ?? "");
    $nonce = (string)($payload["nonce"] ?? "");

    if ($path === "/register") {
        if ($device_id === "") {
            json_ok(["status" => "badreq"], 400);
        }

        register_device($device_id);
        json_ok(["status" => "OK"]);
    }

    if ($device_id === "" || $nonce === "") {
        json_ok(["status" => "badreq"], 400);
    }

    if (!is_registered($device_id)) {
        json_ok(["status" => "notreg"], 403);
    }

    if (!check_nonce($device_id, $nonce)) {
        json_ok(["status" => "replay"], 403);
    }

    if ($path === "/control") {
        json_ok([
            "status" => "OK",
            "device_id" => $device_id,
            "relay_on" => get_relay_desired($device_id),
        ]);
    }

    if ($path === "/data") {
        $records = $payload["records"] ?? null;
        if (!is_array($records)) {
            json_ok(["status" => "badreq"], 400);
        }

        $db = pdo();
        $db->beginTransaction();

        $ins = $db->prepare("
            INSERT INTO data(
                device_id, ts, current1_mA, current2_mA, current3_mA, power_dW,
                temp1_cC, temp2_cC, phase_imbalance_dPct, relay_on, heater_on, phase_trip, relay_cmd_on
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ");

        $saved = 0;
        foreach ($records as $r) {
            if (!is_array($r)) {
                continue;
            }

            $ts = (int)($r["ts"] ?? 0);
            if ($ts <= 0) {
                continue;
            }

            $ins->execute([
                $device_id,
                $ts,
                (int)($r["current1_mA"] ?? 0),
                (int)($r["current2_mA"] ?? 0),
                (int)($r["current3_mA"] ?? 0),
                (int)($r["power_dW"] ?? 0),
                (int)($r["temp1_cC"] ?? 0),
                (int)($r["temp2_cC"] ?? 0),
                (int)($r["phase_imbalance_dPct"] ?? 0),
                (int)($r["relay_on"] ?? 0),
                (int)($r["heater_on"] ?? 0),
                (int)($r["phase_trip"] ?? 0),
                (int)($r["relay_cmd_on"] ?? 0),
            ]);
            $saved++;
        }

        $db->commit();
        json_ok(["status" => "OK", "saved" => $saved]);
    }

    json_ok(["status" => "nf"], 404);
} catch (Throwable $e) {
    if (defined("DEBUG_LOG") && DEBUG_LOG) {
        log_exception($e, "FATAL");
    }
    json_ok(["status" => "error"], 500);
}
