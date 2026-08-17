# Data analysis and publication

Petrichor will treat the data record as an engineering dataset first. The first release
will describe sensor behaviour, watering response, and device reliability. It will not
claim that the controller improves plant health until the project has replicated trials
with defined plants, controls, and outcomes.

## Data status

`data/garden.db` is a reproducible fake-data fixture for development. It must not appear
in a results table or be described as an indoor soil or indoor hydroponic observation. Real V1
observations begin only after the sensor has passed potting-mix calibration, the liquid path
has passed containment tests, and the device has completed a supervised indoor watering
test.

## Analysis dataset

The publication export will contain one row per moisture reading and one row per watering
event. Keep the raw SQLite database unchanged as the local source record, then generate
versioned CSV files and a machine-readable metadata file from it.

Required fields are:

- Readings: `device_id`, `moisture_pct`, `timestamp`, `received_at`.
- Watering events: `device_id`, `request_id`, `trigger_type`, `status`, requested and
  actual duration, moisture before and after, start time, and completion time.
- Device health: optional supply voltage, Wi-Fi signal, uptime, and last-seen time.

Record the firmware commit, schema version, deployment profile, sensor calibration endpoints,
soil or growing medium, plant or crop, containment test result, timezone, missing-data
periods, and any manual interventions with each deployment. Hydroponic records must also
identify reservoir volume, nutrient solution, water temperature, and the chemistry sensors
used.

## Statistical analysis

### 1. Data quality

Report observation count, time coverage, sampling interval, duplicate timestamps, missing
periods, out-of-range values, delayed uploads, failed watering events, and sensor resets.
Keep invalid source rows in the raw record, flag them in the analysis export, and exclude
them from calculations with the exclusion reason recorded.

### 2. Sensor behaviour

Plot moisture over time and report median, interquartile range, minimum, maximum, and the
distribution of first differences. Compare readings from the calibration states and flag
drift, clipping, abrupt steps, and implausibly fast changes. Summarise readings by day and
by watering cycle rather than treating every 10-second firmware loop as an independent
biological observation.

### 3. Watering response

For each completed event, calculate the change from the pre-event reading to the first
valid post-event reading in a declared window. Report the response median, interquartile
range, time to peak response, and recovery time to the configured threshold. Report these
by event and by deployment, not as a pooled estimate that hides repeated measurements from
the same device.

### 4. Reliability and resource use

Report watering completion rate, command acknowledgement rate, offline buffering and
replay count, resets, uptime, supply-voltage changes when measured, and data-ingestion
latency. Relate power and reliability summaries to operating conditions and firmware
versions.

### 5. Inference limits

The first device is a time series from one deployment. Use descriptive statistics and
plots. Do not report p-values, confidence intervals as if observations were independent,
or causal claims about plant growth. Add comparisons, mixed-effects models, or interrupted
time-series analysis only after the project has repeated deployments and records a defined
control or baseline.

## Reproducible publication workflow

1. Freeze a release date and record the firmware, schema, analysis-code, and configuration
   commits.
2. Export the raw observations, a cleaned analysis table, a data dictionary, deployment
   metadata, and the generated figures.
3. Remove secrets and private information. Do not publish credentials, exact private garden
   coordinates, Wi-Fi identifiers, or unreviewed device identifiers.
4. Run the export and analysis from a clean checkout. Record row counts, date ranges,
   exclusions, and checksums in the release notes.
5. Publish the code and non-sensitive data in a versioned GitHub release. Archive the same
   release in a DOI-backed repository when a citable research release is needed.
6. Label fake, calibration, bench, indoor-soil, indoor-hydroponic, and indoor-multi-module data separately. Never mix them in one
   result without a source label.

The public status page may show current device summaries. It is not the authoritative
scientific archive. A released dataset and its metadata remain the citable record.
