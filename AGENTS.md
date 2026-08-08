# petrichor

Petrichor is the foundation for a modular, distributed autonomous agriculture platform. The current implementation is an autonomous garden-watering module: ESP32 firmware reads soil moisture and drives a relay pump, reporting over MQTT to a Raspberry Pi that logs to SQLite and serves a Flask dashboard. Status: remote-prep, no hardware yet. Both halves run against mocks. Treat this as foundational infrastructure, not a throwaway prototype.

## Long-term direction

- Growing modules are autonomous robotic systems. Each should support independent sensing, local decision making, local safety, local actuation, communication, capability discovery, and software updates. Modules may differ in hardware.
- Local autonomy and safety must not depend on a central controller. A central controller may coordinate optimisation and learning, but safe operation must continue during disconnection or degraded service.
- Preserve modular architecture, hardware abstraction, explicit interface contracts, testability, fault tolerance, graceful degradation, deterministic behaviour, and safety-first design.
- Evolve control toward plant-aware autonomy by combining environmental measurements, plant observations, and system health. Optimise plant health, not isolated environmental thresholds.
- Introduce machine learning only for a defined engineering problem, progressing from prediction to anomaly detection, adaptive parameter tuning, and constrained policy optimisation. Safety constraints always override learned behaviour.
- Prefer designs that remain useful from one plant through hydroponic modules, multiple modules, computer vision, adaptive control, and distributed autonomous vertical farming. Structured, validated longitudinal data is an engineering asset because it improves future deployments.

## Read first

- `SPEC.md` is the contract between firmware and dashboard: MQTT topics, JSON payload shapes, units, SQLite schema. Change spec and code in the same commit. Treat drift between them as a bug.
- `README.md` has the full architecture and watering logic (threshold, hysteresis, cooldown, schedule window, safe default off).

## Build and test

```bash
cd firmware && python -m platformio test -e native     # host unit tests (31 cases)
cd firmware && python -m platformio run -e esp32dev    # compile check for target
python data/generate_fake_data.py                      # rebuild data/garden.db
python -m venv .venv && .venv/Scripts/python.exe -m pip install -r dashboard/requirements.txt
cd dashboard && ../.venv/Scripts/python.exe -m pytest  # dashboard tests (18 cases)
python dashboard/app.py                                # serve on 127.0.0.1:5000
```

End-to-end remote-prep (no hardware):

```bash
# Start an MQTT broker on localhost:1883, then:
python dashboard/subscriber.py                    # writes live MQTT data to data/garden.db
python data/fake_esp32.py                         # simulates the ESP32 firmware
python dashboard/commander.py --command run --duration 15  # sends a command with retry/ack
```

## Conventions

- Every real-hardware call sits behind one named function marked `HARDWARE SWAP POINT` in its header comment. Keep decision logic pure C++ and host-testable.
- The dashboard only reads SQLite. It must never care who wrote the rows.
- Known deliberate gaps: `sleep_manager.cpp` is not yet wired into the main loop. Application-level QoS 1 (command deduplication + `garden/pump/ack`) is implemented, but true MQTT QoS 1 would still require switching MQTT libraries.

## Tooling

- The `graphify` skill has been migrated into the Codex skill tree at
  `..\.agents\skills\graphify`. Use it for architecture and file-relationship
  questions about this repo, and to build a persistent knowledge graph as the
  project grows.
- The `esp32` skill has been migrated into the Codex skill tree at
  `..\.agents\skills\esp32`. Use it for GPIO, deep sleep, ADC, PlatformIO, and
  protocol questions.
- Obsidian MCP access is available from the real user Codex config. Use it for
  relevant project notes when available.
- Historical Claude CLI commands in old settings are not part of the Codex
  workflow.
