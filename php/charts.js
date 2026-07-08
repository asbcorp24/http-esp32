$(document).ready(function() {
    let mainChart = null;
    let barChart = null;
    let autoRefreshInterval = null;
    let currentMetric = 'current1_mA';

    const metricMeta = {
        current1_mA: { label: 'Ток L1', unit: 'mA', factor: 1 },
        current2_mA: { label: 'Ток L2', unit: 'mA', factor: 1 },
        current3_mA: { label: 'Ток L3', unit: 'mA', factor: 1 },
        power_dW: { label: 'Мощность', unit: 'Вт', factor: 0.1 },
        temp1_cC: { label: 'Температура 1', unit: '°C', factor: 0.01 },
        temp2_cC: { label: 'Температура 2', unit: '°C', factor: 0.01 },
        phase_imbalance_dPct: { label: 'Перекос фаз', unit: '%', factor: 0.1 },
        relay_on: { label: 'Реле насоса', unit: '', factor: 1 },
        heater_on: { label: 'Обогрев', unit: '', factor: 1 },
        phase_trip: { label: 'Авария перекоса', unit: '', factor: 1 }
    };

    initCharts();
    loadData();
    setupEventListeners();

    function initCharts() {
        const ctx = document.getElementById('mainChart').getContext('2d');
        mainChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'Значения',
                    data: [],
                    borderColor: '#00ff9d',
                    backgroundColor: 'rgba(0, 255, 157, 0.1)',
                    borderWidth: 2,
                    pointRadius: 3,
                    pointHoverRadius: 6,
                    tension: 0.4,
                    fill: true
                }]
            },
            options: chartOptions()
        });

        const barCtx = document.getElementById('barChartCanvas').getContext('2d');
        barChart = new Chart(barCtx, {
            type: 'bar',
            data: {
                labels: [],
                datasets: [{
                    label: 'Значения',
                    data: [],
                    backgroundColor: 'rgba(0, 255, 157, 0.7)',
                    borderColor: '#00ff9d',
                    borderWidth: 1
                }]
            },
            options: chartOptions()
        });
    }

    function chartOptions() {
        return {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: { labels: { color: '#e0e0e0' } },
                tooltip: {
                    mode: 'index',
                    intersect: false,
                    backgroundColor: '#1a1a2e',
                    titleColor: '#00ff9d',
                    bodyColor: '#e0e0e0',
                    borderColor: '#00ff9d',
                    borderWidth: 1
                }
            },
            scales: {
                x: {
                    grid: { color: 'rgba(255,255,255,0.1)' },
                    ticks: { color: '#e0e0e0' }
                },
                y: {
                    grid: { color: 'rgba(255,255,255,0.1)' },
                    ticks: { color: '#e0e0e0' }
                }
            }
        };
    }

    function setupEventListeners() {
        $('#applyFilters').click(loadData);
        $('#refreshData').click(loadData);
        $('#deviceSelect').change(loadData);
        $('#periodSelect').change(function() {
            if ($(this).val() === 'custom') {
                alert('Произвольный период пока не реализован');
                $(this).val('168');
            }
            loadData();
        });
        $('#metricSelect').change(function() {
            currentMetric = $(this).val();
            loadData();
        });
        $('#aggSelect').change(loadData);
        $('#autoRefresh').change(function() {
            if ($(this).is(':checked')) {
                autoRefreshInterval = setInterval(loadData, 30000);
            } else {
                clearInterval(autoRefreshInterval);
            }
        });
        $('#exportCSV').click(function() {
            const deviceId = $('#deviceSelect').val();
            const hours = $('#periodSelect').val();
            window.location.href = `api.php?action=exportCSV&device_id=${encodeURIComponent(deviceId)}&hours=${encodeURIComponent(hours)}`;
        });
        $('#relayOnBtn').click(function() {
            setRelayState(true);
        });
        $('#relayOffBtn').click(function() {
            setRelayState(false);
        });
    }

    function loadData() {
        showLoading();

        const deviceId = $('#deviceSelect').val();
        const period = $('#periodSelect').val();
        const metric = $('#metricSelect').val();
        const agg = $('#aggSelect').val();

        $.ajax({
            url: 'api.php',
            method: 'GET',
            dataType: 'json',
            data: {
                action: 'getStats',
                device_id: deviceId,
                period: period,
                metric: metric,
                agg: agg
            },
            success: function(response) {
                if (response.success) {
                    updateCharts(response.chart, metric);
                    updateStats(response.stats);
                    updateLastUpdate(response.last_update);
                }
            },
            complete: function() {
                loadRecentData();
            }
        });

        $.ajax({
            url: 'api.php',
            method: 'GET',
            dataType: 'json',
            data: {
                action: 'getHeatmap',
                device_id: deviceId
            },
            success: function(response) {
                if (response.success) {
                    updateHeatmap(response.heatmap);
                }
            }
        });
    }

    function loadRecentData() {
        const deviceId = $('#deviceSelect').val();

        $.ajax({
            url: 'api.php',
            method: 'GET',
            dataType: 'json',
            data: {
                action: 'getRecentData',
                device_id: deviceId,
                limit: 50
            },
            success: function(response) {
                if (response.success) {
                    updateTable(response.data);
                }
            },
            complete: function() {
                hideLoading();
            }
        });
    }

    function updateCharts(data, metric) {
        const meta = metricMeta[metric] || metricMeta.current1_mA;
        const labels = data.map(item => item.hour);
        const rawValues = data.map(item => parseFloat(item.value || 0));
        const values = rawValues.map(v => Number((v * meta.factor).toFixed(2)));

        mainChart.data.labels = labels;
        mainChart.data.datasets[0].data = values;
        mainChart.data.datasets[0].label = `${meta.label}${meta.unit ? ' (' + meta.unit + ')' : ''}`;
        mainChart.update();

        barChart.data.labels = labels.slice(-24);
        barChart.data.datasets[0].data = values.slice(-24);
        barChart.data.datasets[0].label = `${meta.label}${meta.unit ? ' (' + meta.unit + ')' : ''}`;
        barChart.update();
    }

    function updateStats(stats) {
        $('#activeDevices').text(stats.active_devices || '0');
        $('#avgPower').text(stats.avg_power ? (stats.avg_power * 0.1).toFixed(1) + ' Вт' : '-');

        const temp1 = stats.avg_temp1 ? stats.avg_temp1 * 0.01 : null;
        const temp2 = stats.avg_temp2 ? stats.avg_temp2 * 0.01 : null;
        const tempText = temp1 !== null || temp2 !== null
            ? `${temp1 !== null ? temp1.toFixed(1) : '-'} / ${temp2 !== null ? temp2.toFixed(1) : '-'} °C`
            : '-';
        $('#avgTemp').text(tempText);

        const desiredRelay = formatState(stats.desired_relay_on);
        const actualRelay = formatState(stats.relay_on);
        const heater = formatState(stats.heater_on);
        const phaseTrip = formatState(stats.phase_trip);

        $('#relayDesiredText').text(desiredRelay);
        $('#relayActualText').text(actualRelay);
        $('#heaterText').text(heater);
        $('#phaseTripText').text(phaseTrip);
        $('#relayStatusText').text(`Команда: ${desiredRelay}, выход: ${actualRelay}`);
    }

    function updateLastUpdate(timestamp) {
        const date = new Date(timestamp * 1000);
        $('#lastUpdate').text(date.toLocaleTimeString());
    }

    function updateTable(data) {
        const tbody = $('#tableBody');
        tbody.empty();

        if (!data.length) {
            tbody.append('<tr><td colspan="12" class="text-center">Нет данных</td></tr>');
            return;
        }

        data.forEach(row => {
            const temp1 = formatScaled(row.temp1_cC, 100);
            const temp2 = formatScaled(row.temp2_cC, 100);
            const power = formatScaled(row.power_dW, 10);
            const phase = formatScaled(row.phase_imbalance_dPct, 10);

            tbody.append(`
                <tr>
                    <td><span class="device-badge">${row.device_id.substring(0, 12)}...</span></td>
                    <td>${row.time_str}</td>
                    <td>${row.current1_mA}</td>
                    <td>${row.current2_mA}</td>
                    <td>${row.current3_mA}</td>
                    <td>${power}</td>
                    <td>${temp1}</td>
                    <td>${temp2}</td>
                    <td>${phase}%</td>
                    <td>${formatState(row.relay_on)}</td>
                    <td>${formatState(row.heater_on)}</td>
                    <td>${formatState(row.phase_trip)}</td>
                </tr>
            `);
        });
    }

    function updateHeatmap(heatmap) {
        const tbody = $('#heatmapBody');
        tbody.empty();

        for (let hour = 0; hour < 24; hour++) {
            const row = $('<tr>');
            row.append(`<td>${hour.toString().padStart(2, '0')}:00</td>`);

            for (let day = 0; day < 7; day++) {
                const value = heatmap[hour]?.[day];
                const intensity = value !== null ? Math.min(100, Math.abs(value) * 8) : 0;
                const bgColor = value !== null ? `rgba(0, 255, 157, ${intensity / 100})` : 'rgba(255,255,255,0.05)';
                row.append(`<td style="background-color: ${bgColor};">${value !== null ? value.toFixed(1) + '°C' : '-'}</td>`);
            }

            tbody.append(row);
        }
    }

    function setRelayState(state) {
        const deviceId = $('#deviceSelect').val();
        if (!deviceId) {
            alert('Сначала выберите устройство');
            return;
        }

        showLoading();
        $.ajax({
            url: 'api.php',
            method: 'POST',
            dataType: 'json',
            data: {
                action: 'setRelayState',
                device_id: deviceId,
                state: state ? 'on' : 'off'
            },
            success: function(response) {
                if (!response.success) {
                    alert(response.error || 'Не удалось отправить команду');
                    return;
                }
                loadData();
            },
            error: function() {
                hideLoading();
                alert('Ошибка отправки команды');
            }
        });
    }

    function formatScaled(value, divider) {
        if (value === null || value === undefined || value === '') return '-';
        return (parseFloat(value) / divider).toFixed(1);
    }

    function formatState(value) {
        if (value === null || value === undefined || value === '') return '-';
        return parseInt(value, 10) ? 'ВКЛ' : 'ВЫКЛ';
    }

    function showLoading() {
        $('#loadingOverlay').css('display', 'flex').hide().fadeIn(200);
    }

    function hideLoading() {
        $('#loadingOverlay').fadeOut(200);
    }
});
