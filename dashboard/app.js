const $ = (id) => document.getElementById(id);

function format(value, suffix = '') {
  return value === null || value === undefined ? '—' : `${value}${suffix}`;
}

function renderSummary(summary) {
  $('device').textContent = summary.device;
  $('sample-count').textContent = `${summary.sample_count} 条采样`;
  $('max-temperature').textContent = format(summary.max_temperature_c, '°C');
  $('max-current').textContent = format(summary.max_current_ma, 'mA');
  $('alert-count').textContent = summary.alert_count;
  const limits = summary.thresholds || {};
  $('limit-temperature').textContent = `阈值 ${limits.temperature_c}°C`;
  $('limit-current').textContent = `阈值 ${limits.current_ma}mA`;
  $('limit-vibration').textContent = `振动阈值 ${limits.vibration_rms_mg}mg · 离线 ${summary.online_timeout_s}s`;
  $('connection').textContent = summary.online ? '设备在线' : '等待设备';
  $('connection').className = `connection ${summary.online ? 'online' : 'offline'}`;
  if (summary.latest) $('last-update').textContent = `最新序号 #${summary.latest.sequence}`;
}

function renderChart(items) {
  const values = items.slice(-24);
  const max = Math.max(...values.map((item) => item.temperature_c), 80);
  $('chart').innerHTML = values.map((item) => {
    const height = Math.max(4, Math.round((item.temperature_c / max) * 100));
    const hot = item.temperature_c > 80 ? ' hot' : '';
    return `<div class="bar${hot}" style="height:${height}%"><em>#${item.sequence} ${item.temperature_c.toFixed(2)}°C</em></div>`;
  }).join('');
}

function renderAlerts(alerts) {
  const recent = alerts.slice(-8).reverse();
  $('alerts').innerHTML = recent.length ? recent.map((alert) =>
    `<div class="alert ${alert.level}"><span>${alert.message}</span><small>#${alert.sequence}</small></div>`
  ).join('') : '<p class="empty">暂无告警</p>';
}

function renderTable(items) {
  $('telemetry').innerHTML = items.slice(-12).reverse().map((item) => {
    const fault = item.status !== 0 ? ' class="fault"' : '';
    return `<tr><td>#${item.sequence}</td><td>${item.temperature_c.toFixed(2)}°C</td><td>${item.humidity_pct.toFixed(2)}%</td><td>${item.current_ma}mA</td><td>${item.vibration_rms_mg}mg</td><td${fault}>${item.status === 0 ? '正常' : '异常'}</td><td>${item.uptime_s}s</td></tr>`;
  }).join('') || '<tr><td colspan="7" class="empty">等待 C++ 网关写入数据</td></tr>';
}

async function refresh() {
  try {
    const [summaryResponse, telemetryResponse] = await Promise.all([fetch('/api/summary'), fetch('/api/telemetry')]);
    if (!summaryResponse.ok || !telemetryResponse.ok) throw new Error('dashboard API unavailable');
    const summary = await summaryResponse.json();
    const telemetry = await telemetryResponse.json();
    renderSummary(summary);
    renderChart(telemetry.items);
    renderAlerts(telemetry.alerts);
    renderTable(telemetry.items);
  } catch (error) {
    $('connection').textContent = '服务未连接';
    $('connection').className = 'connection offline';
  }
}

refresh();
setInterval(refresh, 2000);
