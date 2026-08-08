export interface IngestReading { moisture_pct: number; timestamp: number }
export interface IngestWateringEvent {
  request_id: string; trigger: 'moisture' | 'schedule' | 'manual';
  result: 'completed' | 'failed' | 'skipped'; requested_duration_sec: number;
  actual_duration_sec: number; moisture_before_pct: number; timestamp: number;
}
export interface IngestDeviceStatus { wifi_rssi_dbm: number; battery_voltage_v: number; uptime_sec: number }
export interface IngestBody {
  device_id: string; timestamp: number; readings: IngestReading[];
  watering_events: IngestWateringEvent[]; device_status: IngestDeviceStatus;
}

const object = (value: unknown): value is Record<string, unknown> => typeof value === 'object' && value !== null;
const finite = (value: unknown): value is number => typeof value === 'number' && Number.isFinite(value);
const integer = (value: unknown): value is number => finite(value) && Number.isInteger(value) && value >= 0;
const percent = (value: unknown): value is number => finite(value) && value >= 0 && value <= 100;

export function isValidIngestBody(body: unknown): body is IngestBody {
  if (!object(body) || typeof body.device_id !== 'string' || !body.device_id || body.device_id.length > 64) return false;
  if (!integer(body.timestamp) || !Array.isArray(body.readings) || body.readings.length > 40) return false;
  if (!Array.isArray(body.watering_events) || body.watering_events.length > 8) return false;
  for (const reading of body.readings) {
    if (!object(reading) || !percent(reading.moisture_pct) || !integer(reading.timestamp)) return false;
  }
  for (const event of body.watering_events) {
    if (!object(event) || typeof event.request_id !== 'string' || !event.request_id || event.request_id.length > 36) return false;
    if (!['moisture', 'schedule', 'manual'].includes(String(event.trigger))) return false;
    if (!['completed', 'failed', 'skipped'].includes(String(event.result))) return false;
    if (!integer(event.requested_duration_sec) || !integer(event.actual_duration_sec)) return false;
    if (!percent(event.moisture_before_pct) || !integer(event.timestamp)) return false;
  }
  if (!object(body.device_status)) return false;
  return finite(body.device_status.wifi_rssi_dbm) && finite(body.device_status.battery_voltage_v) &&
    body.device_status.battery_voltage_v >= 0 && integer(body.device_status.uptime_sec);
}
