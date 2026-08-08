import { renderPage, type EventRow, type ReadingRow, type StatusRow } from './status_page';
import { isValidIngestBody } from './validate';

export interface Env { DB: D1Database; CLOUD_SHARED_SECRET: string }

const json = (body: object, status = 200) => Response.json(body, { status });

async function ingest(request: Request, env: Env): Promise<Response> {
  if (request.headers.get('Authorization') !== `Bearer ${env.CLOUD_SHARED_SECRET}`) return json({ error: 'unauthorized' }, 401);
  let body: unknown;
  try { body = await request.json(); } catch { return json({ error: 'invalid json' }, 400); }
  if (!isValidIngestBody(body)) return json({ error: 'malformed body' }, 400);

  const receivedAt = Math.floor(Date.now() / 1000);
  const statements: D1PreparedStatement[] = body.readings.map((reading) => env.DB.prepare(
    'INSERT INTO readings (device_id, moisture_pct, timestamp, received_at) SELECT ?, ?, ?, ? WHERE NOT EXISTS (SELECT 1 FROM readings WHERE device_id = ? AND timestamp = ?)'
  ).bind(body.device_id, reading.moisture_pct, reading.timestamp, receivedAt, body.device_id, reading.timestamp));
  for (const event of body.watering_events) statements.push(env.DB.prepare(
    `INSERT OR IGNORE INTO watering_events (device_id,request_id,trigger_type,status,requested_duration_sec,actual_duration_sec,moisture_before_pct,started_at,completed_at)
     VALUES (?,?,?,?,?,?,?,?,?)`
  ).bind(body.device_id, event.request_id, event.trigger, event.result, event.requested_duration_sec, event.actual_duration_sec, event.moisture_before_pct, event.timestamp, event.timestamp));
  statements.push(env.DB.prepare(
    `INSERT INTO device_status (device_id,wifi_rssi_dbm,battery_voltage_v,uptime_sec,last_seen) VALUES (?,?,?,?,?)
     ON CONFLICT(device_id) DO UPDATE SET wifi_rssi_dbm=excluded.wifi_rssi_dbm,battery_voltage_v=excluded.battery_voltage_v,uptime_sec=excluded.uptime_sec,last_seen=excluded.last_seen`
  ).bind(body.device_id, body.device_status.wifi_rssi_dbm, body.device_status.battery_voltage_v, body.device_status.uptime_sec, body.timestamp));
  await env.DB.batch(statements);
  return json({ ok: true });
}

async function statusPage(env: Env): Promise<Response> {
  const status = await env.DB.prepare('SELECT device_id,wifi_rssi_dbm,battery_voltage_v,uptime_sec,last_seen FROM device_status ORDER BY last_seen DESC LIMIT 1').first<StatusRow>();
  let readings: ReadingRow[] = [];
  let events: EventRow[] = [];
  if (status) {
    readings = (await env.DB.prepare('SELECT moisture_pct,timestamp FROM readings WHERE device_id=? ORDER BY timestamp DESC LIMIT 100').bind(status.device_id).all<ReadingRow>()).results.reverse();
    events = (await env.DB.prepare('SELECT request_id,trigger_type,status,requested_duration_sec,actual_duration_sec,moisture_before_pct,started_at FROM watering_events WHERE device_id=? ORDER BY started_at DESC LIMIT 20').bind(status.device_id).all<EventRow>()).results;
  }
  return new Response(renderPage(status, readings, events), { headers: {
    'Content-Type': 'text/html; charset=utf-8',
    'Content-Security-Policy': "default-src 'none'; style-src 'unsafe-inline'; img-src 'self'; base-uri 'none'; frame-ancestors 'none'",
    'X-Content-Type-Options': 'nosniff',
  }});
}

export default { async fetch(request: Request, env: Env): Promise<Response> {
  const path = new URL(request.url).pathname;
  if (request.method === 'POST' && path === '/ingest') return ingest(request, env);
  if (request.method === 'GET' && path === '/') return statusPage(env);
  return new Response('Not found', { status: 404 });
}};
