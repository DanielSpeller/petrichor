# Cloudflare Ingest Service Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up a Cloudflare Worker + D1 service that receives batched status pushes from the standalone ESP32 (per `SPEC.md` §5) and serves a public, no-auth status/history page — the ESP32's only remote visibility now that there's no Pi/dashboard running.

**Architecture:** One Worker, two routes. `POST /ingest` (shared-secret authenticated) inserts into a D1 database using the exact table shapes already defined in `SPEC.md` §2 (`readings`, `watering_events`, `device_status`) — same schema the eventual Pi dashboard expects, so nothing about this diverges from that path. `GET /` (public, no auth) queries the same D1 tables and renders a small self-contained HTML status page: latest moisture/battery/RSSI, an inline SVG moisture sparkline, and a recent watering-events table.

**Tech Stack:** Cloudflare Workers, D1 (SQLite-compatible), TypeScript, Wrangler CLI, Vitest for pure-logic unit tests.

## Global Constraints

- Reuse `SPEC.md` §2's table/column names and §5's `/ingest` payload shape exactly — this plan doesn't invent new field names.
- Prepared statements only (`env.DB.prepare(...).bind(...)`) — never string-interpolate user-controlled values into SQL.
- The write path (`/ingest`) requires the `Authorization: Bearer <CLOUD_SHARED_SECRET>` header (`SPEC.md` §5); the read path (`GET /`) is intentionally public, no auth, per the project's explicit design decision.
- Secrets (`CLOUD_SHARED_SECRET`) are never committed — set via `wrangler secret put` for production and a gitignored `.dev.vars` for local dev, matching how `firmware/include/secrets.h` is already handled.
- `compatibility_date` in `wrangler.jsonc` is set to the date this plan was written (2026-08-07), not left as a placeholder.

---

### Task 1: Scaffold the Worker project and D1 database

**Files:**
- Create: `cloud/package.json`
- Create: `cloud/tsconfig.json`
- Create: `cloud/wrangler.jsonc`
- Create: `cloud/migrations/0001_initial_schema.sql`
- Create: `cloud/.dev.vars` (gitignored, not committed)
- Modify: `.gitignore` (repo root)

**Interfaces:**
- Produces: a deployable Worker skeleton with a `DB` D1 binding and a `CLOUD_SHARED_SECRET` env var, ready for Tasks 2-4 to add routes to.

- [ ] **Step 1: Scaffold the directory and install tooling**

```bash
mkdir -p cloud/src cloud/migrations cloud/test
cd cloud
npm init -y
npm install -D wrangler typescript vitest @cloudflare/workers-types
```

- [ ] **Step 2: Add `.gitignore` entries**

Append to the repo-root `.gitignore`:

```
cloud/node_modules/
cloud/.wrangler/
cloud/.dev.vars
```

- [ ] **Step 3: Write `tsconfig.json`**

Create `cloud/tsconfig.json`:

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "lib": ["ES2022"],
    "module": "ES2022",
    "moduleResolution": "Bundler",
    "types": ["@cloudflare/workers-types"],
    "strict": true,
    "skipLibCheck": true,
    "noEmit": true
  },
  "include": ["src/**/*.ts", "test/**/*.ts"]
}
```

- [ ] **Step 4: Create the D1 database**

```bash
npx wrangler d1 create petrichor
```

Note the `database_id` printed in the output — you'll need it in the next step.

- [ ] **Step 5: Write `wrangler.jsonc`**

Create `cloud/wrangler.jsonc`, substituting the real `database_id` from Step 4:

```jsonc
{
  "$schema": "./node_modules/wrangler/config-schema.json",
  "name": "petrichor-ingest",
  "main": "src/index.ts",
  "compatibility_date": "2026-08-07",
  "d1_databases": [
    {
      "binding": "DB",
      "database_name": "petrichor",
      "database_id": "REPLACE_WITH_DATABASE_ID_FROM_WRANGLER_D1_CREATE"
    }
  ]
}
```

- [ ] **Step 6: Write the initial migration (SPEC.md §2 schema, verbatim)**

Create `cloud/migrations/0001_initial_schema.sql`:

```sql
CREATE TABLE readings (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id    TEXT    NOT NULL,
    moisture_pct REAL    NOT NULL CHECK (moisture_pct >= 0 AND moisture_pct <= 100),
    timestamp    INTEGER NOT NULL,           -- unix epoch seconds, UTC; when the sensor was read
    received_at  INTEGER NOT NULL            -- unix epoch seconds, UTC; when the server logged it
);

CREATE INDEX idx_readings_device_timestamp ON readings(device_id, timestamp);


CREATE TABLE watering_events (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id               TEXT    NOT NULL,
    request_id              TEXT,                       -- MQTT request_id; NULL only for pre-MQTT/manual DB rows, if any
    trigger_type            TEXT    NOT NULL CHECK (trigger_type IN ('moisture', 'schedule', 'manual')),
    status                  TEXT    NOT NULL DEFAULT 'pending'
                                    CHECK (status IN ('pending', 'completed', 'failed', 'skipped')),
    requested_duration_sec  INTEGER NOT NULL,
    actual_duration_sec     INTEGER,                    -- NULL until status confirms; 0 if skipped/failed before start
    moisture_before_pct     REAL    CHECK (moisture_before_pct IS NULL OR (moisture_before_pct >= 0 AND moisture_before_pct <= 100)),
    moisture_after_pct      REAL    CHECK (moisture_after_pct  IS NULL OR (moisture_after_pct  >= 0 AND moisture_after_pct  <= 100)),
    started_at              INTEGER NOT NULL,           -- unix epoch seconds, UTC; when the command was issued
    completed_at            INTEGER                     -- unix epoch seconds, UTC; when garden/pump/status arrived
);

CREATE INDEX idx_watering_events_device_started ON watering_events(device_id, started_at);
CREATE UNIQUE INDEX idx_watering_events_request_id ON watering_events(request_id) WHERE request_id IS NOT NULL;


CREATE TABLE device_status (
    device_id         TEXT    PRIMARY KEY,
    wifi_rssi_dbm     INTEGER,
    battery_voltage_v REAL,
    uptime_sec        INTEGER,
    last_seen         INTEGER NOT NULL            -- unix epoch seconds, UTC; timestamp of the last heartbeat received
);
```

- [ ] **Step 7: Apply the migration locally**

```bash
npx wrangler d1 migrations apply petrichor --local
```

Expected: confirms `0001_initial_schema.sql` applied, creates `.wrangler/state/v3/d1/`.

- [ ] **Step 8: Create a local dev secret**

Create `cloud/.dev.vars` (gitignored per Step 2):

```
CLOUD_SHARED_SECRET=local_dev_secret
```

- [ ] **Step 9: Add a placeholder entry point so the project builds**

Create `cloud/src/index.ts`:

```typescript
export interface Env {
  DB: D1Database;
  CLOUD_SHARED_SECRET: string;
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    return new Response('petrichor-ingest: not yet implemented', { status: 501 });
  },
};
```

- [ ] **Step 10: Verify the dev server starts**

```bash
npx wrangler dev
```

Expected: starts without error, prints a local URL. Stop it with Ctrl+C once confirmed.

- [ ] **Step 11: Commit**

```bash
git add cloud/package.json cloud/package-lock.json cloud/tsconfig.json cloud/wrangler.jsonc cloud/migrations cloud/src/index.ts .gitignore
git commit -m "cloud: scaffold Cloudflare Worker + D1 project, apply SPEC.md §2 schema"
```

(`.dev.vars`, `node_modules/`, `.wrangler/` are gitignored — nothing further to add there.)

---

### Task 2: `isValidIngestBody` — request validation

**Files:**
- Create: `cloud/src/validate.ts`
- Test: `cloud/test/validate.test.ts`

**Interfaces:**
- Produces: `interface IngestReading`, `IngestWateringEvent`, `IngestDeviceStatus`, `IngestBody`; `function isValidIngestBody(body: unknown): body is IngestBody`.

- [ ] **Step 1: Write the failing test**

Create `cloud/test/validate.test.ts`:

```typescript
import { describe, expect, it } from 'vitest';
import { isValidIngestBody } from '../src/validate';

const validBody = {
  device_id: 'zone_1',
  timestamp: 1753277970,
  readings: [{ moisture_pct: 42.5, timestamp: 1753277940 }],
  watering_events: [
    {
      request_id: 'local_7',
      trigger: 'moisture',
      result: 'completed',
      requested_duration_sec: 10,
      actual_duration_sec: 10,
      moisture_before_pct: 28.0,
      timestamp: 1753277961,
    },
  ],
  device_status: { wifi_rssi_dbm: -62, battery_voltage_v: 3.98, uptime_sec: 86412 },
};

describe('isValidIngestBody', () => {
  it('accepts a well-formed body', () => {
    expect(isValidIngestBody(validBody)).toBe(true);
  });

  it('accepts empty readings/watering_events arrays', () => {
    expect(isValidIngestBody({ ...validBody, readings: [], watering_events: [] })).toBe(true);
  });

  it('rejects a missing device_id', () => {
    const { device_id, ...rest } = validBody;
    expect(isValidIngestBody(rest)).toBe(false);
  });

  it('rejects a non-array readings field', () => {
    expect(isValidIngestBody({ ...validBody, readings: 'nope' })).toBe(false);
  });

  it('rejects a reading missing moisture_pct', () => {
    expect(isValidIngestBody({ ...validBody, readings: [{ timestamp: 1 }] })).toBe(false);
  });

  it('rejects a watering event missing request_id', () => {
    const badEvent: Record<string, unknown> = { ...validBody.watering_events[0] };
    delete badEvent.request_id;
    expect(isValidIngestBody({ ...validBody, watering_events: [badEvent] })).toBe(false);
  });

  it('rejects a missing device_status', () => {
    const { device_status, ...rest } = validBody;
    expect(isValidIngestBody(rest)).toBe(false);
  });

  it('rejects null and non-object input', () => {
    expect(isValidIngestBody(null)).toBe(false);
    expect(isValidIngestBody('a string')).toBe(false);
    expect(isValidIngestBody(42)).toBe(false);
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd cloud && npx vitest run test/validate.test.ts`
Expected: FAIL/import error — `src/validate.ts` doesn't exist yet.

- [ ] **Step 3: Implement**

Create `cloud/src/validate.ts`:

```typescript
export interface IngestReading {
  moisture_pct: number;
  timestamp: number;
}

export interface IngestWateringEvent {
  request_id: string;
  trigger: string;
  result: string;
  requested_duration_sec: number;
  actual_duration_sec: number;
  moisture_before_pct: number;
  timestamp: number;
}

export interface IngestDeviceStatus {
  wifi_rssi_dbm: number;
  battery_voltage_v: number;
  uptime_sec: number;
}

export interface IngestBody {
  device_id: string;
  timestamp: number;
  readings: IngestReading[];
  watering_events: IngestWateringEvent[];
  device_status: IngestDeviceStatus;
}

export function isValidIngestBody(body: unknown): body is IngestBody {
  if (typeof body !== 'object' || body === null) return false;
  const b = body as Record<string, unknown>;

  if (typeof b.device_id !== 'string' || b.device_id.length === 0) return false;
  if (typeof b.timestamp !== 'number') return false;
  if (!Array.isArray(b.readings) || !Array.isArray(b.watering_events)) return false;

  for (const reading of b.readings) {
    if (typeof reading !== 'object' || reading === null) return false;
    const r = reading as Record<string, unknown>;
    if (typeof r.moisture_pct !== 'number' || typeof r.timestamp !== 'number') return false;
  }

  for (const event of b.watering_events) {
    if (typeof event !== 'object' || event === null) return false;
    const e = event as Record<string, unknown>;
    if (typeof e.request_id !== 'string' || e.request_id.length === 0) return false;
    if (typeof e.trigger !== 'string' || typeof e.result !== 'string') return false;
    if (typeof e.requested_duration_sec !== 'number' || typeof e.actual_duration_sec !== 'number') return false;
    if (typeof e.moisture_before_pct !== 'number' || typeof e.timestamp !== 'number') return false;
  }

  if (typeof b.device_status !== 'object' || b.device_status === null) return false;
  const status = b.device_status as Record<string, unknown>;
  if (typeof status.wifi_rssi_dbm !== 'number') return false;
  if (typeof status.battery_voltage_v !== 'number') return false;
  if (typeof status.uptime_sec !== 'number') return false;

  return true;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd cloud && npx vitest run test/validate.test.ts`
Expected: PASS, 8 tests.

- [ ] **Step 5: Commit**

```bash
git add cloud/src/validate.ts cloud/test/validate.test.ts
git commit -m "cloud: add isValidIngestBody request validation"
```

---

### Task 3: `status_page.ts` — pure rendering helpers

**Files:**
- Create: `cloud/src/status_page.ts`
- Test: `cloud/test/status_page.test.ts`

**Interfaces:**
- Consumes: nothing from earlier tasks (pure, standalone).
- Produces: `function escapeHtml(value: string): string`; `function formatTimestamp(unixSec: number): string`; `function buildSparklinePoints(readings: {moisture_pct: number; timestamp: number}[]): string`; `function renderPage(status: {device_id: string; wifi_rssi_dbm: number; battery_voltage_v: number; uptime_sec: number; last_seen: number} | null, readings: {moisture_pct: number; timestamp: number}[], events: {request_id: string; trigger_type: string; status: string; requested_duration_sec: number; actual_duration_sec: number; moisture_before_pct: number; started_at: number}[]): string`.

- [ ] **Step 1: Write the failing tests**

Create `cloud/test/status_page.test.ts`:

```typescript
import { describe, expect, it } from 'vitest';
import { escapeHtml, buildSparklinePoints, formatTimestamp, renderPage } from '../src/status_page';

describe('escapeHtml', () => {
  it('escapes HTML-significant characters', () => {
    expect(escapeHtml('<script>&"</script>')).toBe('&lt;script&gt;&amp;&quot;&lt;/script&gt;');
  });

  it('leaves plain text unchanged', () => {
    expect(escapeHtml('zone_1')).toBe('zone_1');
  });
});

describe('buildSparklinePoints', () => {
  it('returns an empty string for no readings', () => {
    expect(buildSparklinePoints([])).toBe('');
  });

  it('maps a single reading to the left edge', () => {
    const points = buildSparklinePoints([{ moisture_pct: 50, timestamp: 1000 }]);
    expect(points).toBe('0.0,60.0');
  });

  it('spans the full width across the timestamp range', () => {
    const points = buildSparklinePoints([
      { moisture_pct: 0, timestamp: 1000 },
      { moisture_pct: 100, timestamp: 2000 },
    ]);
    expect(points).toBe('0.0,120.0 600.0,0.0');
  });
});

describe('formatTimestamp', () => {
  it('formats the unix epoch as UTC', () => {
    expect(formatTimestamp(0)).toBe('1970-01-01 00:00:00 UTC');
  });

  it('formats a timestamp with a nonzero time-of-day', () => {
    expect(formatTimestamp(86400 + 3661)).toBe('1970-01-02 01:01:01 UTC');
  });
});

describe('renderPage', () => {
  it('renders a "no data yet" message when status is null', () => {
    const html = renderPage(null, [], []);
    expect(html).toContain('no data yet');
  });

  it('renders the device_id and latest moisture when status is present', () => {
    const html = renderPage(
      { device_id: 'zone_1', wifi_rssi_dbm: -62, battery_voltage_v: 3.98, uptime_sec: 86412, last_seen: 0 },
      [{ moisture_pct: 42.5, timestamp: 0 }],
      []
    );
    expect(html).toContain('zone_1');
    expect(html).toContain('42.5%');
  });

  it('renders "no watering events yet" when the events list is empty', () => {
    const html = renderPage(
      { device_id: 'zone_1', wifi_rssi_dbm: -62, battery_voltage_v: 3.98, uptime_sec: 86412, last_seen: 0 },
      [],
      []
    );
    expect(html).toContain('no watering events yet');
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd cloud && npx vitest run test/status_page.test.ts`
Expected: FAIL/import error.

- [ ] **Step 3: Implement**

Create `cloud/src/status_page.ts`:

```typescript
interface StatusRow {
  device_id: string;
  wifi_rssi_dbm: number;
  battery_voltage_v: number;
  uptime_sec: number;
  last_seen: number;
}

interface ReadingRow {
  moisture_pct: number;
  timestamp: number;
}

interface EventRow {
  request_id: string;
  trigger_type: string;
  status: string;
  requested_duration_sec: number;
  actual_duration_sec: number;
  moisture_before_pct: number;
  started_at: number;
}

export function escapeHtml(value: string): string {
  return value
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

export function formatTimestamp(unixSec: number): string {
  return new Date(unixSec * 1000).toISOString().replace('T', ' ').slice(0, 19) + ' UTC';
}

const SPARKLINE_WIDTH = 600;
const SPARKLINE_HEIGHT = 120;

export function buildSparklinePoints(readings: ReadingRow[]): string {
  if (readings.length === 0) return '';
  const minTs = readings[0].timestamp;
  const maxTs = readings[readings.length - 1].timestamp;
  const tsRange = Math.max(maxTs - minTs, 1);
  return readings
    .map((r) => {
      const x = ((r.timestamp - minTs) / tsRange) * SPARKLINE_WIDTH;
      const y = SPARKLINE_HEIGHT - (r.moisture_pct / 100) * SPARKLINE_HEIGHT;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(' ');
}

export function renderPage(status: StatusRow | null, readings: ReadingRow[], events: EventRow[]): string {
  const points = buildSparklinePoints(readings);
  const latestMoisture = readings.length > 0 ? readings[readings.length - 1].moisture_pct.toFixed(1) : '--';

  const eventRows = events
    .map(
      (e) => `<tr>
        <td>${formatTimestamp(e.started_at)}</td>
        <td>${escapeHtml(e.trigger_type)}</td>
        <td>${escapeHtml(e.status)}</td>
        <td>${e.actual_duration_sec}s / ${e.requested_duration_sec}s</td>
        <td>${e.moisture_before_pct.toFixed(1)}%</td>
      </tr>`
    )
    .join('\n');

  return `<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>petrichor status</title>
  <style>
    body { font-family: -apple-system, sans-serif; background: #0e1116; color: #e6e6e6; margin: 2rem auto; max-width: 700px; padding: 0 1rem; }
    h1 { font-size: 1.25rem; }
    .stat { display: inline-block; margin-right: 2rem; margin-bottom: 1rem; }
    .stat .value { font-size: 1.5rem; font-weight: 600; }
    .stat .label { font-size: 0.75rem; color: #9aa4b2; text-transform: uppercase; }
    svg { background: #171b22; border-radius: 6px; width: 100%; height: auto; }
    table { width: 100%; border-collapse: collapse; margin-top: 1rem; font-size: 0.85rem; }
    th, td { text-align: left; padding: 0.4rem 0.5rem; border-bottom: 1px solid #262b33; }
    th { color: #9aa4b2; font-weight: 500; }
  </style>
</head>
<body>
  <h1>petrichor -- ${status ? escapeHtml(status.device_id) : 'no data yet'}</h1>
  ${
    status
      ? `<div class="stat"><div class="value">${latestMoisture}%</div><div class="label">moisture</div></div>
  <div class="stat"><div class="value">${status.battery_voltage_v.toFixed(2)}V</div><div class="label">battery</div></div>
  <div class="stat"><div class="value">${status.wifi_rssi_dbm} dBm</div><div class="label">wifi rssi</div></div>
  <div class="stat"><div class="value">${formatTimestamp(status.last_seen)}</div><div class="label">last seen</div></div>
  <svg viewBox="0 0 600 120" preserveAspectRatio="none">
    <polyline points="${points}" fill="none" stroke="#4fd1c5" stroke-width="2" />
  </svg>
  <table>
    <thead><tr><th>time</th><th>trigger</th><th>result</th><th>duration</th><th>moisture before</th></tr></thead>
    <tbody>${eventRows || '<tr><td colspan="5">no watering events yet</td></tr>'}</tbody>
  </table>`
      : `<p>Device hasn't reported in yet.</p>`
  }
</body>
</html>`;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd cloud && npx vitest run test/status_page.test.ts`
Expected: PASS, 9 tests.

- [ ] **Step 5: Commit**

```bash
git add cloud/src/status_page.ts cloud/test/status_page.test.ts
git commit -m "cloud: add status page rendering (sparkline, event table, escaping)"
```

---

### Task 4: Wire `/ingest` and `GET /` into `src/index.ts`

**Files:**
- Modify: `cloud/src/index.ts`

**Interfaces:**
- Consumes: `isValidIngestBody`/`IngestBody` (Task 2), `renderPage` (Task 3), `Env` (Task 1).
- Produces: the deployed routing behavior other tasks/docs reference (`POST /ingest`, `GET /`).

Not unit-testable without a running D1 instance (the pure logic underneath — validation and rendering — already has coverage from Tasks 2-3). Verified manually via `wrangler dev` + `curl`, the same category of "structured and documented but not exercised in the automated suite" already used for `firmware/src/mqtt_client.cpp` in this repo.

- [ ] **Step 1: Replace `src/index.ts`**

Replace the contents of `cloud/src/index.ts` with:

```typescript
import { isValidIngestBody } from './validate';
import { renderPage } from './status_page';

export interface Env {
  DB: D1Database;
  CLOUD_SHARED_SECRET: string;
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (request.method === 'POST' && url.pathname === '/ingest') {
      return handleIngest(request, env);
    }
    if (request.method === 'GET' && url.pathname === '/') {
      return handleStatusPage(env);
    }
    return new Response('Not found', { status: 404 });
  },
};

async function handleIngest(request: Request, env: Env): Promise<Response> {
  const authHeader = request.headers.get('Authorization');
  if (authHeader !== `Bearer ${env.CLOUD_SHARED_SECRET}`) {
    return Response.json({ error: 'unauthorized' }, { status: 401 });
  }

  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return Response.json({ error: 'invalid json' }, { status: 400 });
  }

  if (!isValidIngestBody(body)) {
    return Response.json({ error: 'malformed body' }, { status: 400 });
  }

  const receivedAt = Math.floor(Date.now() / 1000);
  const statements: D1PreparedStatement[] = [];

  for (const reading of body.readings) {
    statements.push(
      env.DB.prepare(
        'INSERT INTO readings (device_id, moisture_pct, timestamp, received_at) VALUES (?, ?, ?, ?)'
      ).bind(body.device_id, reading.moisture_pct, reading.timestamp, receivedAt)
    );
  }

  for (const event of body.watering_events) {
    // ON CONFLICT DO NOTHING makes this idempotent: if the device retries a
    // sync (e.g. it never saw the 200 response, so didn't clear its
    // buffer), the same request_id arriving twice doesn't error out the
    // whole batch on the request_id UNIQUE index.
    statements.push(
      env.DB.prepare(
        `INSERT INTO watering_events
          (device_id, request_id, trigger_type, status, requested_duration_sec,
           actual_duration_sec, moisture_before_pct, started_at, completed_at)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
         ON CONFLICT(request_id) DO NOTHING`
      ).bind(
        body.device_id,
        event.request_id,
        event.trigger,
        event.result,
        event.requested_duration_sec,
        event.actual_duration_sec,
        event.moisture_before_pct,
        event.timestamp,
        event.timestamp
      )
    );
  }

  statements.push(
    env.DB.prepare(
      `INSERT INTO device_status (device_id, wifi_rssi_dbm, battery_voltage_v, uptime_sec, last_seen)
       VALUES (?, ?, ?, ?, ?)
       ON CONFLICT(device_id) DO UPDATE SET
         wifi_rssi_dbm = excluded.wifi_rssi_dbm,
         battery_voltage_v = excluded.battery_voltage_v,
         uptime_sec = excluded.uptime_sec,
         last_seen = excluded.last_seen`
    ).bind(
      body.device_id,
      body.device_status.wifi_rssi_dbm,
      body.device_status.battery_voltage_v,
      body.device_status.uptime_sec,
      body.timestamp
    )
  );

  await env.DB.batch(statements);

  return Response.json({ ok: true });
}

interface StatusRow {
  device_id: string;
  wifi_rssi_dbm: number;
  battery_voltage_v: number;
  uptime_sec: number;
  last_seen: number;
}

interface ReadingRow {
  moisture_pct: number;
  timestamp: number;
}

interface EventRow {
  request_id: string;
  trigger_type: string;
  status: string;
  requested_duration_sec: number;
  actual_duration_sec: number;
  moisture_before_pct: number;
  started_at: number;
}

async function handleStatusPage(env: Env): Promise<Response> {
  const status = await env.DB.prepare(
    'SELECT device_id, wifi_rssi_dbm, battery_voltage_v, uptime_sec, last_seen FROM device_status ORDER BY last_seen DESC LIMIT 1'
  ).first<StatusRow>();

  if (!status) {
    return new Response(renderPage(null, [], []), {
      headers: { 'Content-Type': 'text/html; charset=utf-8' },
    });
  }

  const readingsResult = await env.DB.prepare(
    'SELECT moisture_pct, timestamp FROM readings WHERE device_id = ? ORDER BY timestamp DESC LIMIT 100'
  ).bind(status.device_id).all<ReadingRow>();

  const eventsResult = await env.DB.prepare(
    `SELECT request_id, trigger_type, status, requested_duration_sec, actual_duration_sec,
            moisture_before_pct, started_at
     FROM watering_events WHERE device_id = ? ORDER BY started_at DESC LIMIT 20`
  ).bind(status.device_id).all<EventRow>();

  const readingsAsc = [...readingsResult.results].reverse();

  return new Response(renderPage(status, readingsAsc, eventsResult.results), {
    headers: { 'Content-Type': 'text/html; charset=utf-8' },
  });
}
```

- [ ] **Step 2: Run the unit suite to confirm nothing regressed**

Run: `cd cloud && npx vitest run`
Expected: PASS, all tests from Tasks 2-3 (17 total).

- [ ] **Step 3: Manual verification — start the dev server**

```bash
cd cloud
npx wrangler dev --persist-to=./.wrangler/state
```

- [ ] **Step 4: Manual verification — reject a bad auth header**

In another terminal:

```bash
curl -i -X POST http://localhost:8787/ingest \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer wrong_secret" \
  -d '{}'
```

Expected: `401`, body `{"error":"unauthorized"}`.

- [ ] **Step 5: Manual verification — accept a well-formed ingest**

```bash
curl -i -X POST http://localhost:8787/ingest \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer local_dev_secret" \
  -d '{
    "device_id": "zone_1",
    "timestamp": 1753277970,
    "readings": [{"moisture_pct": 42.5, "timestamp": 1753277940}],
    "watering_events": [{
      "request_id": "local_7",
      "trigger": "moisture",
      "result": "completed",
      "requested_duration_sec": 10,
      "actual_duration_sec": 10,
      "moisture_before_pct": 28.0,
      "timestamp": 1753277961
    }],
    "device_status": {"wifi_rssi_dbm": -62, "battery_voltage_v": 3.98, "uptime_sec": 86412}
  }'
```

Expected: `200`, body `{"ok":true}`.

- [ ] **Step 6: Manual verification — status page reflects the data**

```bash
curl -s http://localhost:8787/ | grep -o 'zone_1'
```

Expected: prints `zone_1` (confirms the page rendered with the ingested device's data). Optionally open `http://localhost:8787/` in a browser to see the sparkline and event table.

- [ ] **Step 7: Manual verification — resending the same watering event doesn't error**

Re-run the exact `curl` from Step 5 a second time.

Expected: still `200 {"ok":true}` (the `ON CONFLICT(request_id) DO NOTHING` from Task 4 makes this idempotent — without it, the repeated `request_id` would violate the unique index and fail the whole batch).

- [ ] **Step 8: Stop the dev server and commit**

```bash
git add cloud/src/index.ts
git commit -m "cloud: wire /ingest and GET / routes into the Worker"
```

---

### Task 5: Deploy and configure the production secret

**Files:** None (deployment/configuration only).

**Interfaces:** None — this task makes Task 4's code live.

- [ ] **Step 1: Apply the migration to the remote (production) database**

```bash
cd cloud
npx wrangler d1 migrations apply petrichor --remote
```

- [ ] **Step 2: Set the production shared secret**

```bash
npx wrangler secret put CLOUD_SHARED_SECRET
```

When prompted, paste a newly generated secret (not the `local_dev_secret` placeholder) — e.g. generate one with `openssl rand -hex 32`.

- [ ] **Step 3: Deploy**

```bash
npx wrangler deploy
```

Note the deployed URL printed in the output (e.g. `https://petrichor-ingest.<subdomain>.workers.dev`).

- [ ] **Step 4: Verify the deployed endpoint**

```bash
curl -i https://petrichor-ingest.<subdomain>.workers.dev/
```

Expected: `200`, HTML containing "no data yet" (nothing ingested to production yet).

- [ ] **Step 5: Update firmware secrets with the real values**

In your local, gitignored `firmware/include/secrets.h` (added in the companion firmware plan's Task 6), set:

```cpp
constexpr char CLOUD_INGEST_URL[] = "https://petrichor-ingest.<subdomain>.workers.dev/ingest";
constexpr char CLOUD_SHARED_SECRET[] = "<the secret from Step 2>";
```

This file is gitignored — nothing to commit here, but this is the step that actually connects the firmware to the deployed Worker once real hardware exists.

---

### Task 6: Docs

**Files:**
- Create: `cloud/README.md`
- Modify: `README.md` (repo root)

**Interfaces:** None (documentation only).

- [ ] **Step 1: Write `cloud/README.md`**

Create `cloud/README.md`:

```markdown
# cloud

Cloudflare Worker + D1 service that receives batched status pushes from the standalone
ESP32 (see `/SPEC.md` §5) and serves a public status/history page. This is the ESP32's
only remote visibility in the standalone deployment — see
`docs/superpowers/specs/2026-08-07-standalone-outdoor-design.md`.

## Layout

| Path | What's in it |
|---|---|
| `src/index.ts` | Fetch handler: routes `POST /ingest` and `GET /`. |
| `src/validate.ts` | `isValidIngestBody` — validates the `/ingest` request shape. |
| `src/status_page.ts` | Pure HTML rendering for the status page (sparkline, event table). |
| `migrations/` | D1 schema migrations — `0001_initial_schema.sql` is `SPEC.md` §2 verbatim. |

## Local development

```bash
npm install
npx wrangler d1 migrations apply petrichor --local
echo "CLOUD_SHARED_SECRET=local_dev_secret" > .dev.vars   # gitignored
npx wrangler dev --persist-to=./.wrangler/state
npx vitest run    # 17 cases, pure logic only (no D1 needed)
```

## Deploying

```bash
npx wrangler d1 migrations apply petrichor --remote
npx wrangler secret put CLOUD_SHARED_SECRET
npx wrangler deploy
```

Then point the firmware's `secrets.h` at the deployed URL (`CLOUD_INGEST_URL`) and the
same secret (`CLOUD_SHARED_SECRET`) — see `firmware/README.md`.
```

- [ ] **Step 2: Update the root `README.md` layout table**

In `README.md`'s `## Layout` table, add a row after the `data/` row:

```markdown
| `cloud/` | Cloudflare Worker + D1 — receives standalone ESP32 status pushes, serves the public status page. |
```

- [ ] **Step 3: Commit**

```bash
git add cloud/README.md README.md
git commit -m "docs: document the cloud ingest service"
```

---

## Self-Review Notes

- **Spec coverage:** Task 1 stands up D1 with the exact `SPEC.md` §2 schema. Tasks 2-4 implement the `SPEC.md` §5 `/ingest` contract (auth, validation, insert) and the public status page the design doc calls for. Task 5 covers going from "works locally" to "the firmware can actually reach it." Task 6 covers docs, matching the convention already set by `firmware/README.md` and `dashboard/README.md`.
- **Placeholder scan:** the only literal placeholder text (`REPLACE_WITH_DATABASE_ID_FROM_WRANGLER_D1_CREATE`, `<subdomain>`, `<the secret from Step 2>`) are values that don't exist until Task 1/Task 5's own commands are run — each is immediately followed by the exact command that produces the real value.
- **Type consistency:** `IngestBody`/`IngestReading`/`IngestWateringEvent`/`IngestDeviceStatus` (Task 2) match the field names Task 4's `handleIngest` destructures exactly. `StatusRow`/`ReadingRow`/`EventRow` (Task 3, re-declared identically in Task 4's `index.ts` since Task 3's copies are module-private) match the columns Task 4's SQL `SELECT`s actually return.
- **Idempotency:** confirmed via Task 4 Step 7's manual re-send check — without `ON CONFLICT(request_id) DO NOTHING`, a firmware retry (buffer not cleared because the 200 response was lost in transit) would fail the entire batch on the `watering_events` unique index.
