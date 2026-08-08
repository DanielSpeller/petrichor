# Petrichor watering logic flow chart

These diagrams trace the firmware's behavior from boot through the main loop, including both autonomous moisture-triggered watering and remote MQTT commands.

---

## 1. Boot and main loop overview

```mermaid
flowchart TD
    START([Device boots]) --> SAFE["Force pump relay OFF<br/>safe default"]
    SAFE --> INIT["Initialize:<br/>watchdog, clock, OTA,<br/>load persisted config"]
    INIT --> LOOP["Main loop"]

    LOOP --> WATCHDOG["Feed watchdog<br/>Handle OTA"]
    WATCHDOG --> WIFI["Update WiFi manager"]
    WIFI --> MQTT_NEED{"WiFi connected<br/>AND MQTT disconnected?"}
    MQTT_NEED -->|Yes| MQTT_CONN["Connect MQTT<br/>Subscribe to pump commands"]
    MQTT_NEED -->|No| MQTT_OK{"MQTT connected?"}
    MQTT_CONN --> MQTT_OK
    MQTT_OK -->|Yes| MQTT_LOOP["Process MQTT loop"]
    MQTT_OK -->|No| PENDING_STATUS{"Pending pump/status<br/>message?"}
    MQTT_LOOP --> PENDING_STATUS
    PENDING_STATUS -->|Yes| RETRY["Try publishing<br/>buffered pump/status"]
    PENDING_STATUS -->|No| PUMP_UPDATE["Update pump runner"]
    RETRY --> PUMP_UPDATE

    PUMP_UPDATE --> RUN_DONE{"Run completed or<br/>stopped early?"}
    RUN_DONE -->|Yes| COMPLETE["Notify watering controller<br/>Increment daily counter<br/>Publish pump/status"]
    RUN_DONE -->|No| NEW_DAY{"New UTC day?"}
    COMPLETE --> NEW_DAY
    NEW_DAY -->|Yes| RESET["Reset wateringsToday"]
    NEW_DAY -->|No| MOISTURE_TIMER{"10 s elapsed since<br/>last moisture read?"}
    RESET --> MOISTURE_TIMER

    MOISTURE_TIMER -->|No| HEARTBEAT_TIMER{"60 s elapsed since<br/>last heartbeat?"}
    MOISTURE_TIMER -->|Yes| READ["Read moisture sensor<br/>Clamp to 0-100 %"]
    READ --> PUB_MQTT{"MQTT connected?"}
    PUB_MQTT -->|Yes| PUB_READING["Publish garden/sensor/moisture"]
    PUB_MQTT -->|No| EVAL["Evaluate watering decision"]
    PUB_READING --> EVAL

    EVAL --> DECISION{"Decision"}
    DECISION -->|Hysteresis lockout| LOOP_END["End of loop"]
    DECISION -->|Above threshold| LOOP_END
    DECISION -->|Cooldown active| LOOP_END
    DECISION -->|WATER TRIGGERED| SCHEDULE{"Schedule allows watering?<br/>06:00-20:00 local AND<br/>wateringsToday < 4 AND<br/>pump not running"}

    SCHEDULE -->|No| LOOP_END
    SCHEDULE -->|Yes| START_PUMP["Start pump run<br/>duration = 10 s<br/>trigger = moisture<br/>request_id = local_N"]
    START_PUMP --> LOOP_END

    HEARTBEAT_TIMER -->|No| LOOP_END
    HEARTBEAT_TIMER -->|Yes| HB_MQTT{"MQTT connected?"}
    HB_MQTT -->|Yes| PUB_HB["Publish garden/device/status<br/>heartbeat"]
    HB_MQTT -->|No| LOOP_END
    PUB_HB --> LOOP_END

    LOOP_END --> LOOP
```

---

## 2. Watering decision detail

```mermaid
flowchart TD
    EVAL["Evaluate moisture & time"] --> ARMED{"Armed?"}
    ARMED -->|No| RECOVER{"Moisture >= 35 %?<br/>threshold + hysteresis"}
    RECOVER -->|Yes| ARM["Re-arm controller"]
    RECOVER -->|No| SKIP["NO_WATER_HYSTERESIS_LOCKOUT"]
    ARMED -->|Yes| THRESHOLD{"Moisture <= 30 %?"}
    ARM --> THRESHOLD
    THRESHOLD -->|No| SKIP_ABOVE["NO_WATER_ABOVE_THRESHOLD"]
    THRESHOLD -->|Yes| COOLED{">= 15 min since<br/>last watering ended?"}
    COOLED -->|No| SKIP_COOL["NO_WATER_COOLDOWN"]
    COOLED -->|Yes| WATER["WATER_TRIGGERED"]
```

---

## 3. MQTT pump command handler

```mermaid
flowchart TD
    CMD(["Receive garden/pump/command"]) --> PARSE{"Valid JSON AND<br/>device_id matches zone_1?"}
    PARSE -->|No| IGNORE["Ignore silently"]
    PARSE -->|Yes| REQ["Normalize request_id<br/>generate local_N if missing"]
    REQ --> ACK["Publish garden/pump/ack"]
    ACK --> DUP{"Duplicate<br/>request_id?"}
    DUP -->|Yes| IGNORE
    DUP -->|No| TYPE{"command"}
    TYPE -->|run| RUN["Clamp duration >= 10 s<br/>Read current moisture"]
    TYPE -->|stop| STOP["Stop active pump run<br/>Publish pump/status = completed"]
    RUN --> RUNNING{"Pump already running?"}
    RUNNING -->|Yes| SKIP_RUN["Publish pump/status = skipped"]
    RUNNING -->|No| START["Start pump run<br/>with provided trigger & duration"]
```

---

## Key constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `MOISTURE_THRESHOLD_PCT` | 30 % | Water when at or below this. |
| `MOISTURE_HYSTERESIS_PCT` | 5 % | Re-arm only after moisture rises to 35 %. |
| `COOLDOWN_PERIOD_SEC` | 900 | 15 minutes between runs. |
| `MIN_WATERING_DURATION_SEC` | 10 | Shortest automatic or clamped run. |
| `SCHEDULE_WINDOW_START_HOUR` | 6 | Earliest watering, 06:00 local. |
| `SCHEDULE_WINDOW_END_HOUR` | 20 | Latest watering, before 20:00 local. |
| `MAX_WATERINGS_PER_DAY` | 4 | Daily cap. |
| `MOISTURE_READ_INTERVAL_MS` | 10 000 | Sensor read/publish interval. |
| `HEARTBEAT_INTERVAL_MS` | 60 000 | Device health publish interval. |
