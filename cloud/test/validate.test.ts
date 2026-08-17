import { describe, expect, it } from 'vitest';
import { isValidIngestBody } from '../src/validate';

const valid = {
  device_id: 'zone_1', timestamp: 1753277970,
  readings: [{ moisture_pct: 42.5, timestamp: 1753277940 }],
  watering_events: [{ request_id: 'local_7', trigger: 'moisture', result: 'completed', requested_duration_sec: 10, actual_duration_sec: 10, moisture_before_pct: 28, timestamp: 1753277961 }],
  device_status: { wifi_rssi_dbm: -62, supply_voltage_v: null, uptime_sec: 86412 },
};

describe('isValidIngestBody', () => {
  it('accepts the contract shape', () => expect(isValidIngestBody(valid)).toBe(true));
  it('accepts empty batches', () => expect(isValidIngestBody({ ...valid, readings: [], watering_events: [] })).toBe(true));
  it.each([
    null,
    { ...valid, device_id: '' },
    { ...valid, readings: [{ moisture_pct: 101, timestamp: 1 }] },
    { ...valid, readings: 'bad' },
    { ...valid, watering_events: [{ ...valid.watering_events[0], trigger: 'bad' }] },
    { ...valid, device_status: { ...valid.device_status, supply_voltage_v: Number.NaN } },
  ])('rejects malformed input', (body) => expect(isValidIngestBody(body)).toBe(false));
});
