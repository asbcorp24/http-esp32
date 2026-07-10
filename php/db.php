<?php
declare(strict_types=1);

function pdo(): PDO {
    static $db = null;
    if ($db instanceof PDO) {
        return $db;
    }

    $db = new PDO("sqlite:" . __DIR__ . "/server.db");
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    $db->setAttribute(PDO::ATTR_DEFAULT_FETCH_MODE, PDO::FETCH_ASSOC);
    return $db;
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

    $db->exec("CREATE INDEX IF NOT EXISTS idx_data_ts ON data(ts);");
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
    $db = pdo();
    $updated = time();
    $relayOn = $state ? 1 : 0;

    $st = $db->prepare("
        UPDATE device_control
        SET relay_on = ?, updated = ?
        WHERE device_id = ?
    ");
    $st->execute([$relayOn, $updated, $device_id]);

    if ($st->rowCount() === 0) {
        $ins = $db->prepare("
            INSERT OR IGNORE INTO device_control(device_id, relay_on, updated)
            VALUES (?, ?, ?)
        ");
        $ins->execute([$device_id, $relayOn, $updated]);
    }
}
