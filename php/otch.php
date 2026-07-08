<?php
declare(strict_types=1);

require_once __DIR__ . '/db.php';

init_db();
$db = pdo();
$devices = $db->query("SELECT id, created FROM devices ORDER BY created DESC")->fetchAll(PDO::FETCH_ASSOC);
?>
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=yes">
    <title>Hi-Tech Dashboard | IoT Analytics</title>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0-alpha1/dist/css/bootstrap.min.css" rel="stylesheet">
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap-icons@1.11.1/font/bootstrap-icons.css">
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap" rel="stylesheet">
    <link rel="stylesheet" href="style.css">
</head>
<body class="dark-theme">
    <div class="container-fluid py-3">
        <div class="row mb-4">
            <div class="col-12">
                <div class="card glass-card">
                    <div class="card-body">
                        <div class="d-flex flex-column flex-md-row justify-content-between align-items-md-center gap-3">
                            <div>
                                <h1 class="display-6 fw-bold gradient-text mb-0">
                                    <i class="bi bi-cpu me-2"></i>Hi-Tech Dashboard
                                </h1>
                                <p class="text-secondary-emphasis mt-2 mb-0">
                                    <i class="bi bi-activity me-1"></i>Мониторинг GSM/ESP32 устройств
                                </p>
                            </div>
                            <div class="d-flex gap-2">
                                <button class="btn btn-outline-primary glass-button" id="refreshData">
                                    <i class="bi bi-arrow-repeat me-1"></i>Обновить
                                </button>
                                <button class="btn btn-outline-success glass-button" id="exportCSV">
                                    <i class="bi bi-file-earmark-spreadsheet me-1"></i>Экспорт
                                </button>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <div class="row mb-4">
            <div class="col-12 col-md-4 mb-3 mb-md-0">
                <div class="card glass-card">
                    <div class="card-body">
                        <label class="form-label text-secondary-emphasis">
                            <i class="bi bi-devices me-1"></i>Устройство
                        </label>
                        <select class="form-select glass-select" id="deviceSelect">
                            <option value="">Все устройства</option>
                            <?php foreach ($devices as $device): ?>
                                <option value="<?= htmlspecialchars($device['id']) ?>">
                                    <?= htmlspecialchars(substr($device['id'], 0, 12)) ?>...
                                    (<?= date('d.m.Y', (int)$device['created']) ?>)
                                </option>
                            <?php endforeach; ?>
                        </select>
                    </div>
                </div>
            </div>
            <div class="col-12 col-md-8">
                <div class="card glass-card">
                    <div class="card-body">
                        <div class="row g-3">
                            <div class="col-6 col-md-3">
                                <label class="form-label text-secondary-emphasis">
                                    <i class="bi bi-calendar-range me-1"></i>Период
                                </label>
                                <select class="form-select glass-select" id="periodSelect">
                                    <option value="24">24 часа</option>
                                    <option value="168" selected>7 дней</option>
                                    <option value="720">30 дней</option>
                                    <option value="custom">Свой</option>
                                </select>
                            </div>
                            <div class="col-6 col-md-3">
                                <label class="form-label text-secondary-emphasis">
                                    <i class="bi bi-motherboard me-1"></i>Параметр
                                </label>
                                <select class="form-select glass-select" id="metricSelect">
                                    <option value="current1_mA">Ток L1 (mA)</option>
                                    <option value="current2_mA">Ток L2 (mA)</option>
                                    <option value="current3_mA">Ток L3 (mA)</option>
                                    <option value="power_dW">Мощность (дВт)</option>
                                    <option value="temp1_cC">Температура 1 (°C)</option>
                                    <option value="temp2_cC">Температура 2 (°C)</option>
                                    <option value="phase_imbalance_dPct">Перекос фаз (0.1%)</option>
                                    <option value="relay_on">Реле насоса</option>
                                    <option value="heater_on">Обогрев</option>
                                    <option value="phase_trip">Авария перекоса</option>
                                </select>
                            </div>
                            <div class="col-6 col-md-3">
                                <label class="form-label text-secondary-emphasis">
                                    <i class="bi bi-graph-up me-1"></i>Агрегация
                                </label>
                                <select class="form-select glass-select" id="aggSelect">
                                    <option value="avg">Среднее</option>
                                    <option value="max">Максимум</option>
                                    <option value="min">Минимум</option>
                                    <option value="sum">Сумма</option>
                                </select>
                            </div>
                            <div class="col-6 col-md-3 d-flex align-items-end">
                                <button class="btn btn-primary w-100 glass-button-primary" id="applyFilters">
                                    <i class="bi bi-funnel me-1"></i>Применить
                                </button>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <div class="row mb-4 g-3">
            <div class="col-6 col-xl-3">
                <div class="card glass-card stat-card">
                    <div class="card-body">
                        <div class="d-flex align-items-center">
                            <div class="stat-icon"><i class="bi bi-cpu"></i></div>
                            <div class="ms-3">
                                <p class="text-secondary-emphasis mb-0">Активных устройств</p>
                                <h3 class="mb-0 fw-bold" id="activeDevices">-</h3>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
            <div class="col-6 col-xl-3">
                <div class="card glass-card stat-card">
                    <div class="card-body">
                        <div class="d-flex align-items-center">
                            <div class="stat-icon"><i class="bi bi-lightning-charge"></i></div>
                            <div class="ms-3">
                                <p class="text-secondary-emphasis mb-0">Ср. мощность</p>
                                <h3 class="mb-0 fw-bold" id="avgPower">-</h3>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
            <div class="col-6 col-xl-3">
                <div class="card glass-card stat-card">
                    <div class="card-body">
                        <div class="d-flex align-items-center">
                            <div class="stat-icon"><i class="bi bi-thermometer-half"></i></div>
                            <div class="ms-3">
                                <p class="text-secondary-emphasis mb-0">Ср. температура</p>
                                <h3 class="mb-0 fw-bold" id="avgTemp">-</h3>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
            <div class="col-6 col-xl-3">
                <div class="card glass-card stat-card">
                    <div class="card-body">
                        <div class="d-flex align-items-center">
                            <div class="stat-icon"><i class="bi bi-clock-history"></i></div>
                            <div class="ms-3">
                                <p class="text-secondary-emphasis mb-0">Последнее обновление</p>
                                <h6 class="mb-0 fw-bold" id="lastUpdate">-</h6>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <div class="row mb-4 g-3">
            <div class="col-12 col-xl-6">
                <div class="card glass-card">
                    <div class="card-body">
                        <div class="d-flex flex-column flex-md-row justify-content-between align-items-md-center gap-3">
                            <div>
                                <h5 class="mb-1"><i class="bi bi-toggle-on me-2"></i>Управление реле насоса</h5>
                                <div class="text-secondary-emphasis" id="relayStatusText">Выберите устройство для управления</div>
                            </div>
                            <div class="d-flex gap-2">
                                <button class="btn btn-success glass-button-primary" id="relayOnBtn">Включить</button>
                                <button class="btn btn-outline-danger glass-button" id="relayOffBtn">Выключить</button>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
            <div class="col-12 col-xl-6">
                <div class="card glass-card">
                    <div class="card-body">
                        <h5 class="mb-3"><i class="bi bi-shield-exclamation me-2"></i>Статус защиты</h5>
                        <div class="row g-3">
                            <div class="col-6">
                                <div class="text-secondary-emphasis">Команда реле</div>
                                <div class="fw-bold" id="relayDesiredText">-</div>
                            </div>
                            <div class="col-6">
                                <div class="text-secondary-emphasis">Авария перекоса</div>
                                <div class="fw-bold" id="phaseTripText">-</div>
                            </div>
                            <div class="col-6">
                                <div class="text-secondary-emphasis">Факт реле</div>
                                <div class="fw-bold" id="relayActualText">-</div>
                            </div>
                            <div class="col-6">
                                <div class="text-secondary-emphasis">Обогрев</div>
                                <div class="fw-bold" id="heaterText">-</div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <div class="row mb-4">
            <div class="col-12">
                <div class="card glass-card">
                    <div class="card-header bg-transparent border-0 pt-3">
                        <ul class="nav nav-tabs card-header-tabs" id="chartTabs">
                            <li class="nav-item">
                                <a class="nav-link active" data-bs-toggle="tab" href="#lineChart">
                                    <i class="bi bi-graph-up me-1"></i>График
                                </a>
                            </li>
                            <li class="nav-item">
                                <a class="nav-link" data-bs-toggle="tab" href="#barChart">
                                    <i class="bi bi-bar-chart me-1"></i>Столбцы
                                </a>
                            </li>
                            <li class="nav-item">
                                <a class="nav-link" data-bs-toggle="tab" href="#heatmap">
                                    <i class="bi bi-grid-3x3-gap-fill me-1"></i>Тепловая карта
                                </a>
                            </li>
                        </ul>
                    </div>
                    <div class="card-body">
                        <div class="tab-content">
                            <div class="tab-pane fade show active" id="lineChart">
                                <canvas id="mainChart" class="chart-canvas"></canvas>
                            </div>
                            <div class="tab-pane fade" id="barChart">
                                <canvas id="barChartCanvas" class="chart-canvas"></canvas>
                            </div>
                            <div class="tab-pane fade" id="heatmap">
                                <div class="table-responsive">
                                    <table class="table table-dark table-hover" id="heatmapTable">
                                        <thead>
                                            <tr>
                                                <th>Час/День</th>
                                                <th>Пн</th>
                                                <th>Вт</th>
                                                <th>Ср</th>
                                                <th>Чт</th>
                                                <th>Пт</th>
                                                <th>Сб</th>
                                                <th>Вс</th>
                                            </tr>
                                        </thead>
                                        <tbody id="heatmapBody"></tbody>
                                    </table>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <div class="row">
            <div class="col-12">
                <div class="card glass-card">
                    <div class="card-header bg-transparent border-0 d-flex justify-content-between align-items-center">
                        <h5 class="mb-0">
                            <i class="bi bi-table me-2"></i>Детальные данные
                        </h5>
                        <div class="form-check form-switch">
                            <input class="form-check-input" type="checkbox" id="autoRefresh">
                            <label class="form-check-label" for="autoRefresh">Автообновление</label>
                        </div>
                    </div>
                    <div class="card-body">
                        <div class="table-responsive">
                            <table class="table table-dark table-hover align-middle" id="dataTable">
                                <thead>
                                    <tr>
                                        <th>Устройство</th>
                                        <th>Время</th>
                                        <th>L1 (mA)</th>
                                        <th>L2 (mA)</th>
                                        <th>L3 (mA)</th>
                                        <th>Мощность (дВт)</th>
                                        <th>T1 (°C)</th>
                                        <th>T2 (°C)</th>
                                        <th>Перекос</th>
                                        <th>Реле</th>
                                        <th>Обогрев</th>
                                        <th>Авария</th>
                                    </tr>
                                </thead>
                                <tbody id="tableBody">
                                    <tr>
                                        <td colspan="12" class="text-center">Загрузка данных...</td>
                                    </tr>
                                </tbody>
                            </table>
                        </div>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <div class="loading-overlay" id="loadingOverlay">
        <div class="spinner">
            <div class="cube1"></div>
            <div class="cube2"></div>
        </div>
    </div>

    <script src="https://code.jquery.com/jquery-3.7.1.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0-alpha1/dist/js/bootstrap.bundle.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
    <script src="charts.js"></script>
</body>
</html>
