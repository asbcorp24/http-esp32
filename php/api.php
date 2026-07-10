<?php
declare(strict_types=1);

require_once __DIR__ . '/db.php';

header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST');

init_db();

function json_response(array $payload, int $code = 200): void {
    http_response_code($code);
    echo json_encode($payload, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

function bool_from_request($value): ?bool {
    if (is_bool($value)) {
        return $value;
    }
    if (is_int($value) || ctype_digit((string)$value)) {
        return ((int)$value) !== 0;
    }
    $value = strtolower(trim((string)$value));
    if (in_array($value, ['1', 'on', 'true'], true)) return true;
    if (in_array($value, ['0', 'off', 'false'], true)) return false;
    return null;
}

function metric_map(): array {
    return [
        'current1_mA' => 'current1_mA',
        'current2_mA' => 'current2_mA',
        'current3_mA' => 'current3_mA',
        'power_dW' => 'power_dW',
        'temp1_cC' => 'temp1_cC',
        'temp2_cC' => 'temp2_cC',
        'phase_imbalance_dPct' => 'phase_imbalance_dPct',
        'relay_on' => 'relay_on',
        'heater_on' => 'heater_on',
        'phase_trip' => 'phase_trip',
        'relay_cmd_on' => 'relay_cmd_on',
    ];
}

function aggregation_map(): array {
    return [
        'avg' => 'AVG',
        'max' => 'MAX',
        'min' => 'MIN',
        'sum' => 'SUM',
    ];
}

function device_filter_clause(string $deviceId, array &$params): string {
    if ($deviceId === '') {
        return '';
    }
    $params[] = $deviceId;
    return " AND data.device_id = ? ";
}

try {
    $action = (string)($_REQUEST['action'] ?? '');
    $db = pdo();

    switch ($action) {
        case 'getStats':
            $deviceId = (string)($_GET['device_id'] ?? '');
            $period = max(1, (int)($_GET['period'] ?? 168));
            $metricKey = (string)($_GET['metric'] ?? 'current1_mA');
            $aggKey = (string)($_GET['agg'] ?? 'avg');

            $metrics = metric_map();
            $aggs = aggregation_map();
            $metric = $metrics[$metricKey] ?? 'current1_mA';
            $aggregation = $aggs[$aggKey] ?? 'AVG';

            $params = [];
            $filter = device_filter_clause($deviceId, $params);

            $sql = "
                SELECT
                    strftime('%Y-%m-%d %H:00', datetime(ts, 'unixepoch')) AS hour,
                    {$aggregation}({$metric}) AS value,
                    COUNT(*) AS points_count
                FROM data
                WHERE ts > strftime('%s', 'now', '-{$period} hours')
                {$filter}
                GROUP BY strftime('%Y-%m-%d %H', datetime(ts, 'unixepoch'))
                ORDER BY hour ASC
            ";

            $st = $db->prepare($sql);
            $st->execute($params);
            $chartData = $st->fetchAll(PDO::FETCH_ASSOC);

            $statsSql = "
                SELECT
                    COUNT(DISTINCT device_id) AS active_devices,
                    AVG(current1_mA) AS avg_current1,
                    AVG(current2_mA) AS avg_current2,
                    AVG(current3_mA) AS avg_current3,
                    AVG(power_dW) AS avg_power,
                    AVG(temp1_cC) AS avg_temp1,
                    AVG(temp2_cC) AS avg_temp2,
                    AVG(phase_imbalance_dPct) AS avg_phase_imbalance,
                    MAX(relay_on) AS relay_on,
                    MAX(heater_on) AS heater_on,
                    MAX(phase_trip) AS phase_trip,
                    MAX(relay_cmd_on) AS relay_cmd_on,
                    MAX(ts) AS last_ts
                FROM data
                WHERE ts > strftime('%s', 'now', '-{$period} hours')
                {$filter}
            ";

            $st = $db->prepare($statsSql);
            $st->execute($params);
            $stats = $st->fetch(PDO::FETCH_ASSOC) ?: [];

            if ($deviceId !== '') {
                $stats['desired_relay_on'] = get_relay_desired($deviceId) ? 1 : 0;
            } else {
                $stats['desired_relay_on'] = null;
            }

            json_response([
                'success' => true,
                'chart' => $chartData,
                'stats' => $stats,
                'last_update' => time(),
            ]);
            break;

        case 'getRecentData':
            $deviceId = (string)($_GET['device_id'] ?? '');
            $limit = max(1, min(500, (int)($_GET['limit'] ?? 100)));

            $params = [];
            $where = '';
            if ($deviceId !== '') {
                $where = "WHERE device_id = ?";
                $params[] = $deviceId;
            }

            $sql = "
                SELECT
                    device_id,
                    datetime(ts, 'unixepoch', 'localtime') AS time_str,
                    ts,
                    current1_mA,
                    current2_mA,
                    current3_mA,
                    power_dW,
                    temp1_cC,
                    temp2_cC,
                    phase_imbalance_dPct,
                    relay_on,
                    heater_on,
                    phase_trip,
                    relay_cmd_on
                FROM data
                {$where}
                ORDER BY ts DESC
                LIMIT {$limit}
            ";

            $st = $db->prepare($sql);
            $st->execute($params);
            json_response([
                'success' => true,
                'data' => $st->fetchAll(PDO::FETCH_ASSOC),
            ]);
            break;

        case 'getHeatmap':
            $deviceId = (string)($_GET['device_id'] ?? '');
            $days = max(1, min(60, (int)($_GET['days'] ?? 7)));
            $params = [];
            $filter = $deviceId !== '' ? " AND device_id = ? " : '';
            if ($deviceId !== '') {
                $params[] = $deviceId;
            }

            $sql = "
                SELECT
                    strftime('%w', datetime(ts, 'unixepoch')) AS day_of_week,
                    strftime('%H', datetime(ts, 'unixepoch')) AS hour,
                    AVG(temp1_cC) AS avg_temp
                FROM data
                WHERE ts > strftime('%s', 'now', '-{$days} days')
                {$filter}
                GROUP BY day_of_week, hour
                ORDER BY day_of_week, hour
            ";

            $st = $db->prepare($sql);
            $st->execute($params);
            $data = $st->fetchAll(PDO::FETCH_ASSOC);

            $heatmap = array_fill(0, 24, array_fill(0, 7, null));
            foreach ($data as $row) {
                $hour = (int)$row['hour'];
                $day = (int)$row['day_of_week'];
                $heatmap[$hour][$day] = $row['avg_temp'] !== null ? round(((float)$row['avg_temp']) / 100, 1) : null;
            }

            json_response([
                'success' => true,
                'heatmap' => $heatmap,
            ]);
            break;

        case 'exportCSV':
            $deviceId = (string)($_GET['device_id'] ?? '');
            $hours = max(1, min(24 * 365, (int)($_GET['hours'] ?? 168)));
            $params = [];

            $whereParts = ["ts > strftime('%s', 'now', '-{$hours} hours')"];
            if ($deviceId !== '') {
                $whereParts[] = "device_id = ?";
                $params[] = $deviceId;
            }
            $where = 'WHERE ' . implode(' AND ', $whereParts);

            $sql = "
                SELECT
                    device_id,
                    datetime(ts, 'unixepoch', 'localtime') AS timestamp,
                    current1_mA,
                    current2_mA,
                    current3_mA,
                    power_dW,
                    temp1_cC,
                    temp2_cC,
                    phase_imbalance_dPct,
                    relay_on,
                    heater_on,
                    phase_trip,
                    relay_cmd_on
                FROM data
                {$where}
                ORDER BY ts DESC
            ";

            $st = $db->prepare($sql);
            $st->execute($params);
            $data = $st->fetchAll(PDO::FETCH_ASSOC);

            header('Content-Type: text/csv; charset=utf-8');
            header('Content-Disposition: attachment; filename="export_' . date('Y-m-d_H-i') . '.csv"');

            $output = fopen('php://output', 'w');
            fputcsv($output, [
                'Device ID', 'Timestamp', 'Current 1 (mA)', 'Current 2 (mA)', 'Current 3 (mA)',
                'Power (dW)', 'Temp 1 (cC)', 'Temp 2 (cC)', 'Phase Imbalance (0.1%)',
                'Relay On', 'Heater On', 'Phase Trip', 'Relay Cmd On'
            ]);

            foreach ($data as $row) {
                fputcsv($output, [
                    $row['device_id'],
                    $row['timestamp'],
                    $row['current1_mA'],
                    $row['current2_mA'],
                    $row['current3_mA'],
                    $row['power_dW'],
                    $row['temp1_cC'],
                    $row['temp2_cC'],
                    $row['phase_imbalance_dPct'],
                    $row['relay_on'],
                    $row['heater_on'],
                    $row['phase_trip'],
                    $row['relay_cmd_on'],
                ]);
            }
            fclose($output);
            exit;

        case 'getRelayState':
            $deviceId = (string)($_GET['device_id'] ?? '');
            if ($deviceId === '') {
                json_response(['success' => false, 'error' => 'device_id is required'], 400);
            }

            json_response([
                'success' => true,
                'device_id' => $deviceId,
                'relay_on' => get_relay_desired($deviceId),
            ]);
            break;

        case 'setRelayState':
            $deviceId = (string)($_REQUEST['device_id'] ?? '');
            $state = bool_from_request($_REQUEST['state'] ?? null);
            if ($deviceId === '' || $state === null) {
                json_response(['success' => false, 'error' => 'device_id and valid state are required'], 400);
            }

            set_relay_desired($deviceId, $state);
            json_response([
                'success' => true,
                'device_id' => $deviceId,
                'relay_on' => $state,
            ]);
            break;

        default:
            json_response(['success' => false, 'error' => 'Unknown action'], 404);
    }
} catch (Throwable $e) {
    json_response(['success' => false, 'error' => $e->getMessage()], 500);
}
