import { describe, expect, it } from 'vitest';
import { buildSparklinePoints, escapeHtml, formatTimestamp, renderPage } from '../src/status_page';

describe('status page', () => {
  it('escapes stored text', () => expect(escapeHtml('<script>&"')).toBe('&lt;script&gt;&amp;&quot;'));
  it('formats UTC timestamps', () => expect(formatTimestamp(0)).toBe('1970-01-01 00:00:00 UTC'));
  it('maps the chart bounds', () => expect(buildSparklinePoints([{ moisture_pct: 0, timestamp: 1 }, { moisture_pct: 100, timestamp: 2 }])).toBe('0.0,120.0 600.0,0.0'));
  it('renders empty state', () => expect(renderPage(null, [], [])).toContain('no data yet'));
  it('renders status and event empty state', () => {
    const html = renderPage({ device_id: 'zone_1', wifi_rssi_dbm: -62, battery_voltage_v: 3.98, uptime_sec: 10, last_seen: 0 }, [{ moisture_pct: 42.5, timestamp: 0 }], []);
    expect(html).toContain('zone_1');
    expect(html).toContain('42.5%');
    expect(html).toContain('no watering events yet');
  });
});
