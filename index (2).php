<?php
// index.php

declare(strict_types=1);

require_once __DIR__ . "/config.php";
require_once __DIR__ . "/crypto.php";
require_once __DIR__ . "/logger.php";
header("Content-Type: application/json; charset=utf-8");
$rawBody = file_get_contents("php://input");

$headers = [];
foreach ($_SERVER as $k => $v) {
    if (strpos($k, "HTTP_") === 0) {
        $headers[$k] = $v;
    }
}

log_line("RAW_REQUEST", [ 
    "time"   => date("Y-m-d H:i:s"),
    "ip"     => $_SERVER["REMOTE_ADDR"] ?? "",
    "method" => $_SERVER["REQUEST_METHOD"] ?? "",
    "uri"    => $_SERVER["REQUEST_URI"] ?? "",
    "headers"=> $headers,
    "body_len" => strlen($rawBody),
    "body_hex_head" => bin_preview($rawBody, 128),
]);
// -------------------------
// response helper
// -------------------------
function json_ok(array $data, int $code = 200): void {
    if (defined("DEBUG_LOG") && DEBUG_LOG) {
        log_line("RESPONSE", ["code" => $code, "data" => $data]);
    }
    http_response_code($code);
    echo json_encode($data, JSON_UNESCAPED_UNICODE);
    exit;
}
// -------------------------
// DB
// -------------------------
function pdo(): PDO {
    static $pdo = null;
    if ($pdo instanceof PDO) return $pdo;

    $pdo = new PDO("sqlite:" . DB_PATH);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    return $pdo;
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
            current_mA INTEGER,
            power_dW INTEGER,
            temp_cC INTEGER
        );
    ");

    $db->exec("
        CREATE TABLE IF NOT EXISTS nonces (
            device_id TEXT,
            nonce TEXT,
            ts INTEGER
        );
    ");

    $db->exec("CREATE INDEX IF NOT EXISTS idx_data_device_ts ON data(device_id, ts);");
    $db->exec("CREATE INDEX IF NOT EXISTS idx_nonces_device_nonce ON nonces(device_id, nonce);");
}

function is_registered(string $device_id): bool {
    $db = pdo();
    $st = $db->prepare("SELECT 1 FROM devices WHERE id=? LIMIT 1");
    $st->execute([$device_id]);
    return (bool)$st->fetchColumn();
}

function register_device(string $device_id): void {
    $db = pdo();
    $st = $db->prepare("INSERT OR IGNORE INTO devices(id, created) VALUES (?, ?)");
    $st->execute([$device_id, time()]);
}

function check_nonce(string $device_id, string $nonce): bool {
    $db = pdo();

    $st = $db->prepare("SELECT 1 FROM nonces WHERE device_id=? AND nonce=? LIMIT 1");
    $st->execute([$device_id, $nonce]);
    if ($st->fetchColumn()) return false;

    $ins = $db->prepare("INSERT INTO nonces(device_id, nonce, ts) VALUES (?, ?, ?)");
    $ins->execute([$device_id, $nonce, time()]);
    return true;
}

// -------------------------
// Router
// -------------------------
try {
    if (defined("DEBUG_LOG") && DEBUG_LOG) {

    $headers = [];
    foreach ($_SERVER as $k => $v) {
        if (strpos($k, "HTTP_") === 0) {
            $headers[$k] = $v;
        }
    }

    log_line("REQUEST_IN", [
        "time"   => time(),
        "ip"     => $_SERVER["REMOTE_ADDR"] ?? null,
        "method" => $_SERVER["REQUEST_METHOD"] ?? null,
        "uri"    => $_SERVER["REQUEST_URI"] ?? null,
        "headers"=> $headers
    ]);
}

init_db();

$method = $_SERVER["REQUEST_METHOD"] ?? "GET";
$uri = $_SERVER["REQUEST_URI"] ?? "/";
$path = parse_url($uri, PHP_URL_PATH) ?? "/";
$path = preg_replace("~^/index\.php~", "", $path);
if ($path === "") $path = "/";

// -------------------------
// GET /sync_time
// -------------------------
if (($method === "GET" || $method === "POST")&& $path === "/sync_time") {
    json_ok(["ts" => time()]);
}

// For POST endpoints: read raw
if ($method === "POST") {
$blob = $rawBody;

if (DEBUG_LOG) {
    log_line("RAW_BODY", [
        "len" => strlen($blob),
        "hex_head" => bin_preview($blob, 64),
    ]);
}
    if ($blob === false || strlen($blob) === 0) {
        if (DEBUG_LOG) log_line("POST_EMPTY");
        json_ok(["status" => "empty"], 400);
    }

    if (DEBUG_LOG) {
        log_line("POST_BLOB", [
            "len" => strlen($blob),
            "head_hex" => bin_preview($blob, 32),
        ]);
    }

    $pass = SERVER_CRYPTO_PASS;

    [$ok, $plain, $err] = aes_decrypt_blob($pass, $blob);
    if (!$ok) {
        if (DEBUG_LOG) log_line("DECRYPT_FAIL", ["err" => $err]);
        json_ok(["status" => "badenc", "err" => $err], 400);
    }

    if (DEBUG_LOG) {
        log_line("DECRYPT_OK", ["plain_len" => strlen($plain)]);
    }

    $payload = json_decode($plain, true);
    if (!is_array($payload)) {
        if (DEBUG_LOG) log_line("JSON_FAIL");
        json_ok(["status" => "badjson"], 400);
    }

    if (DEBUG_LOG && defined("DEBUG_DUMP_JSON") && DEBUG_DUMP_JSON) {
        log_line("JSON_DUMP", $payload);
    } else if (DEBUG_LOG) {
        // без полного дампа: только ключевые поля
        log_line("JSON_KEYS", [
            "device_id" => $payload["device_id"] ?? null,
            "nonce" => $payload["nonce"] ?? null,
            "records_n" => is_array($payload["records"] ?? null) ? count($payload["records"]) : null,
        ]);
    }
}

// -------------------------
// POST /register
// plain contains JSON with device_id, etc
// -------------------------
if ($method === "POST" && $path === "/register") {
    $device_id = $payload["device_id"] ?? "";
    if (!is_string($device_id) || $device_id === "") {
        json_ok(["status" => "badreq"], 400);
    }

    register_device($device_id);
    json_ok(["status" => "OK"]);
}

// -------------------------
// POST /data
// expects: device_id, nonce, records[]
// -------------------------
if ($method === "POST" && $path === "/data") {
    $device_id = $payload["device_id"] ?? "";
    $nonce     = $payload["nonce"] ?? "";
    $records   = $payload["records"] ?? null;

    if (DEBUG_LOG) {
        log_line("DATA_REQUEST", [
            "device_id" => $device_id,
            "nonce"     => $nonce,
            "records_n" => is_array($records) ? count($records) : null
        ]);
    }

    // ---- проверка структуры ----
    if (!is_string($device_id) || $device_id === "" ||
        !is_string($nonce) || $nonce === "" ||
        !is_array($records)) {

        if (DEBUG_LOG) log_line("DATA_BADREQ");
        json_ok(["status" => "badreq"], 400);
    }

    // ---- регистрация ----
    if (!is_registered($device_id)) {
        if (DEBUG_LOG) {
            log_line("DATA_NOT_REGISTERED", [
                "device_id" => $device_id
            ]);
        }
        json_ok(["status" => "notreg"], 403);
    }

    // ---- защита от повторов ----
    if (!check_nonce($device_id, $nonce)) {
        if (DEBUG_LOG) {
            log_line("DATA_REPLAY", [
                "device_id" => $device_id,
                "nonce" => $nonce
            ]);
        }
        json_ok(["status" => "replay"], 403);
    }

    // ---- запись ----
    $db = pdo();
    $db->beginTransaction();

    $ins = $db->prepare(
        "INSERT INTO data(device_id, ts, current_mA, power_dW, temp_cC)
         VALUES (?, ?, ?, ?, ?)"
    );

    $saved = 0;

  foreach ($records as $r) {
    if (!is_array($r)) continue;

    $ts         = (int)($r["ts"] ?? 0);
    $current_mA = (int)($r["current_mA"] ?? 0);
    $power_dW   = (int)($r["power_dW"] ?? 0);
    $temp_cC    = (int)($r["temp_cC"] ?? 0);

    if (DEBUG_LOG) {
        log_line("DATA_RECORD", [
            "device_id" => $device_id,
            "ts" => $ts,
            "current_mA" => $current_mA,
            "power_dW" => $power_dW,
            "temp_cC" => $temp_cC
        ]);
    }

    if ($ts <= 0) continue;

    $ins->execute([
        $device_id,
        $ts,
        $current_mA,
        $power_dW,
        $temp_cC
    ]);

    $saved++;
}


    $db->commit();

    if (DEBUG_LOG) {
        log_line("DATA_SAVED", [
            "device_id" => $device_id,
            "saved_rows" => $saved
        ]);
    }

    json_ok(["status" => "OK"]);
}

json_ok(["status" => "nf"], 404);
} catch (Throwable $e) {
    if (defined("DEBUG_LOG") && DEBUG_LOG) {
        log_exception($e, "FATAL");
    }
    json_ok(["status" => "error"], 500);
}