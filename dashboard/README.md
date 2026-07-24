# dashboard

Raspberry Pi side: MQTT broker client, SQLite logging, web dashboard.

See `/SPEC.md` before changing any MQTT payload or DB field.

## Remote-prep phase (no real MQTT data yet)

This phase runs against a fake-data fixture instead of live MQTT data -- see
`data/generate_fake_data.py`. The dashboard's data-access layer (`dashboard/data_access.py`)
only reads from SQLite and has no idea who wrote the rows, so swapping the fixture for a real
MQTT-subscriber later is a drop-in change; nothing in `dashboard/` needs to change when real
data starts flowing in.

## Setup

```bash
python -m pip install -r dashboard/requirements.txt
python data/generate_fake_data.py   # one-time: builds data/garden.db
```

## Run

```bash
python dashboard/app.py
```

Then open http://127.0.0.1:5000 in a browser.

## Tests

```bash
cd dashboard
python -m pytest
```

## Regenerating the fixture

Re-run `python data/generate_fake_data.py` any time -- it deletes and rebuilds
`data/garden.db` from scratch with a fresh 7-day history (fixed random seed, so the shape of
the data is reproducible run-to-run).
