export interface StatusRow { device_id: string; wifi_rssi_dbm: number; supply_voltage_v: number | null; uptime_sec: number; last_seen: number }
export interface ReadingRow { moisture_pct: number; timestamp: number }
export interface EventRow { request_id: string; trigger_type: string; status: string; requested_duration_sec: number; actual_duration_sec: number; moisture_before_pct: number; started_at: number }

export function escapeHtml(value: string): string {
  return value.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

export function formatTimestamp(unixSec: number): string {
  return new Date(unixSec * 1000).toISOString().replace('T', ' ').slice(0, 19) + ' UTC';
}

export function buildSparklinePoints(readings: ReadingRow[]): string {
  if (!readings.length) return '';
  const first = readings[0].timestamp;
  const range = Math.max(readings[readings.length - 1].timestamp - first, 1);
  return readings.map((reading) => {
    const x = ((reading.timestamp - first) / range) * 600;
    const y = 120 - reading.moisture_pct * 1.2;
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  }).join(' ');
}

export function renderPage(status: StatusRow | null, readings: ReadingRow[], events: EventRow[]): string {
  const latest = readings.at(-1)?.moisture_pct.toFixed(1) ?? '--';
  const rows = events.map((event) => `<tr><td>${formatTimestamp(event.started_at)}</td><td>${escapeHtml(event.trigger_type)}</td><td>${escapeHtml(event.status)}</td><td>${event.actual_duration_sec}s / ${event.requested_duration_sec}s</td><td>${event.moisture_before_pct.toFixed(1)}%</td></tr>`).join('');
  const content = status ? `<section class="stats"><article><strong>${latest}%</strong><span>moisture</span></article><article><strong>${status.supply_voltage_v === null ? '--' : status.supply_voltage_v.toFixed(2) + 'V'}</strong><span>supply</span></article><article><strong>${status.wifi_rssi_dbm} dBm</strong><span>signal</span></article></section><p class="seen">Last seen ${formatTimestamp(status.last_seen)}</p><svg viewBox="0 0 600 120" role="img" aria-label="Moisture history"><polyline points="${buildSparklinePoints(readings)}" fill="none" stroke="currentColor" stroke-width="3" vector-effect="non-scaling-stroke"/></svg><h2>Recent watering</h2><table><thead><tr><th>Time</th><th>Trigger</th><th>Result</th><th>Duration</th><th>Before</th></tr></thead><tbody>${rows || '<tr><td colspan="5">no watering events yet</td></tr>'}</tbody></table>` : '<p>no data yet</p>';
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Petrichor status</title><style>:root{color-scheme:dark}body{font:16px system-ui,sans-serif;background:#0b1611;color:#e8f2ec;max-width:760px;margin:0 auto;padding:2rem}h1{font-size:1.3rem}.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:1rem}.stats article,svg{background:#12251b;border:1px solid #244432;border-radius:12px;padding:1rem}.stats strong,.stats span{display:block}.stats strong{font-size:1.7rem}.stats span,.seen{color:#a7b9ae}svg{box-sizing:border-box;width:100%;height:auto;color:#67d391}table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:.65rem;border-bottom:1px solid #244432}@media(max-width:600px){.stats{grid-template-columns:1fr}table{font-size:.8rem}}</style></head><body><h1>Petrichor${status ? ` · ${escapeHtml(status.device_id)}` : ''}</h1>${content}</body></html>`;
}
