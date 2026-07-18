<?php
declare(strict_types=1);

require_once __DIR__ . "/config.php";
require_once __DIR__ . "/crypto.php";
require_once __DIR__ . "/logger.php";
require_once __DIR__ . "/db.php";

header("Content-Type: application/json; charset=utf-8");

$rawBody = file_get_contents("php://input");

function json_ok(array $data, int $code = 200): void {
    if (defined("DEBUG_LOG") && DEBUG_LOG) {
        log_line("RESPONSE", ["code" => $code, "data" => $data]);
    }
    http_response_code($code);
    echo json_encode($data, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
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
        if (DEBUG_LOG) {
            log_line("DECRYPT_FAIL", ["err" => $err]);
        }
        json_ok(["status" => "badenc", "err" => $err], 400);
    }

    $payload = json_decode($plain, true);
    if (!is_array($payload)) {
        json_ok(["status" => "badjson"], 400);
    }

    if (DEBUG_LOG && defined("DEBUG_DUMP_JSON") && DEBUG_DUMP_JSON) {
        log_line("JSON_DUMP", $payload);
    }

    return $payload;
}

try {
    if (defined("DEBUG_LOG") && DEBUG_LOG) {
        $headers = [];
        foreach ($_SERVER as $k => $v) {
            if (strpos($k, "HTTP_") === 0) {
                $headers[$k] = $v;
            }
        }

        log_line("RAW_REQUEST", [
            "time" => date("Y-m-d H:i:s"),
            "ip" => $_SERVER["REMOTE_ADDR"] ?? "",
            "method" => $_SERVER["REQUEST_METHOD"] ?? "",
            "uri" => $_SERVER["REQUEST_URI"] ?? "",
            "headers" => $headers,
            "body_len" => strlen((string)$rawBody),
            "body_hex_head" => bin_preview((string)$rawBody, 128),
        ]);
    }

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
            INSERT OR IGNORE INTO data(
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

            $row = [
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
            ];

            if (DEBUG_LOG) {
                log_line("DATA_RECORD", [
                    "device_id" => $device_id,
                    "ts" => $row[1],
                    "current1_mA" => $row[2],
                    "current2_mA" => $row[3],
                    "current3_mA" => $row[4],
                    "power_dW" => $row[5],
                    "temp1_cC" => $row[6],
                    "temp2_cC" => $row[7],
                    "phase_imbalance_dPct" => $row[8],
                    "relay_on" => $row[9],
                    "heater_on" => $row[10],
                    "phase_trip" => $row[11],
                    "relay_cmd_on" => $row[12],
                ]);
            }

            $ins->execute($row);
            $saved += $ins->rowCount();
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
