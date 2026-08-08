"""Fake ESP32 for end-to-end remote-prep testing.

Connects to an MQTT broker and behaves like the real firmware:
- Publishes moisture readings on garden/sensor/moisture
- Publishes heartbeats on garden/device/status
- Subscribes to garden/pump/command and responds on garden/pump/status
- Triggers local watering when simulated soil is dry enough

Run directly: `python data/fake_esp32.py`
"""

import json
import os
import random
import time
import uuid

import paho.mqtt.client as mqtt

DEVICE_ID = os.environ.get("DEVICE_ID", "zone_1")
BROKER = os.environ.get("MQTT_BROKER", "127.0.0.1")
PORT = int(os.environ.get("MQTT_PORT", "1883"))

MOISTURE_THRESHOLD_PCT = 30.0
MOISTURE_HYSTERESIS_PCT = 5.0
COOLDOWN_PERIOD_SEC = 900
MIN_WATERING_DURATION_SEC = 10
SCHEDULE_WINDOW_START_HOUR = 6
SCHEDULE_WINDOW_END_HOUR = 20
MAX_WATERINGS_PER_DAY = 4

MOISTURE_READ_INTERVAL_SEC = 10
HEARTBEAT_INTERVAL_SEC = 60


def current_hour(now_sec):
    return int((now_sec / 3600) % 24)


def within_schedule(hour):
    return SCHEDULE_WINDOW_START_HOUR <= hour < SCHEDULE_WINDOW_END_HOUR


class FakeWateringController:
    def __init__(self):
        self.armed = True
        self.last_watered_end = 0
        self.has_watered = False

    def evaluate(self, moisture_pct, now_sec):
        if not self.armed:
            if moisture_pct >= MOISTURE_THRESHOLD_PCT + MOISTURE_HYSTERESIS_PCT:
                self.armed = True
            else:
                return False
        if moisture_pct > MOISTURE_THRESHOLD_PCT:
            return False
        if self.has_watered and (now_sec - self.last_watered_end) < COOLDOWN_PERIOD_SEC:
            return False
        return True

    def notify_complete(self, end_sec):
        self.last_watered_end = end_sec
        self.has_watered = True
        self.armed = False


class FakePumpRunner:
    def __init__(self, client, device_id):
        self.client = client
        self.device_id = device_id
        self.running = False
        self.start_sec = 0
        self.duration_sec = 0
        self.request_id = None
        self.trigger = None
        self.moisture_before = None

    def _publish_status(self, now_sec, result, actual_duration_sec):
        payload = {
            "device_id": self.device_id,
            "request_id": self.request_id,
            "trigger": self.trigger,
            "result": result,
            "requested_duration_sec": self.duration_sec,
            "actual_duration_sec": actual_duration_sec,
            "moisture_before_pct": self.moisture_before,
            "timestamp": now_sec,
        }
        self.client.publish("garden/pump/status", json.dumps(payload), qos=1)
        print(f"[pump] {result.upper()} {self.request_id} actual={actual_duration_sec}s")

    def start(self, duration_sec, request_id, trigger, moisture_before, now_sec):
        if self.running:
            self._publish_status(now_sec, "skipped", 0)
            return False
        self.running = True
        self.start_sec = now_sec
        self.duration_sec = duration_sec
        self.request_id = request_id
        self.trigger = trigger
        self.moisture_before = moisture_before
        print(f"[pump] START {request_id} {trigger} {duration_sec}s")
        return True

    def stop(self, now_sec):
        if not self.running:
            return
        actual = min(self.duration_sec, max(0, now_sec - self.start_sec))
        self._publish_status(now_sec, "completed", actual)
        self.running = False

    def update(self, now_sec):
        if self.running and (now_sec - self.start_sec) >= self.duration_sec:
            self._publish_status(now_sec, "completed", self.duration_sec)
            self.running = False
            return True
        return False


class FakeEsp32:
    def __init__(self):
        self.client = mqtt.Client()
        self.pump = FakePumpRunner(self.client, DEVICE_ID)
        self.controller = FakeWateringController()
        self.rng = random.Random(42)
        self.moisture = 65.0
        self.waterings_today = 0
        self.last_day = -1
        self.last_reading_sec = 0
        self.last_heartbeat_sec = 0
        self.local_request_counter = 0

    def _publish_moisture(self, now_sec):
        self.client.publish(
            "garden/sensor/moisture",
            json.dumps(
                {
                    "device_id": DEVICE_ID,
                    "moisture_pct": round(self.moisture, 1),
                    "timestamp": now_sec,
                }
            ),
            qos=0,
        )

    def _publish_heartbeat(self, now_sec):
        self.client.publish(
            "garden/device/status",
            json.dumps(
                {
                    "device_id": DEVICE_ID,
                    "wifi_rssi_dbm": -58,
                    "battery_voltage_v": 4.01,
                    "uptime_sec": now_sec,
                    "timestamp": now_sec,
                }
            ),
            qos=0,
            retain=True,
        )

    def _try_local_watering(self, now_sec):
        hour = current_hour(now_sec)
        if not within_schedule(hour):
            return
        if self.waterings_today >= MAX_WATERINGS_PER_DAY:
            return
        if self.pump.running:
            return
        if not self.controller.evaluate(self.moisture, now_sec):
            return

        self.local_request_counter += 1
        request_id = f"local_{self.local_request_counter}"
        if self.pump.start(MIN_WATERING_DURATION_SEC, request_id, "moisture", self.moisture, now_sec):
            self.waterings_today += 1

    def _publish_ack(self, request_id, now_sec):
        self.client.publish(
            "garden/pump/ack",
            json.dumps(
                {
                    "device_id": DEVICE_ID,
                    "request_id": request_id,
                    "timestamp": now_sec,
                }
            ),
            qos=0,
        )

    def _on_message(self, _client, _userdata, msg):
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return
        if payload.get("device_id") != DEVICE_ID:
            return

        now_sec = int(time.time())
        request_id = payload.get("request_id") or str(uuid.uuid4())
        self._publish_ack(request_id, now_sec)

        cmd = payload.get("command")
        if cmd == "run":
            duration = max(payload.get("duration_sec", MIN_WATERING_DURATION_SEC), MIN_WATERING_DURATION_SEC)
            request_id = payload.get("request_id") or str(uuid.uuid4())
            trigger = payload.get("trigger", "manual")
            if self.pump.start(duration, request_id, trigger, self.moisture, now_sec):
                self.waterings_today += 1
        elif cmd == "stop":
            self.pump.stop(now_sec)

    def run(self):
        self.client.on_message = self._on_message
        self.client.connect(BROKER, PORT, 60)
        self.client.subscribe("garden/pump/command", qos=1)
        print(f"Fake ESP32 {DEVICE_ID} connected to {BROKER}:{PORT}")

        while True:
            now_sec = int(time.time())
            self.client.loop(timeout=0.1)

            completed = self.pump.update(now_sec)
            if completed:
                self.controller.notify_complete(now_sec)
                # Simulate soil saturation after a watering.
                self.moisture = self.rng.uniform(60.0, 80.0)

            day = now_sec // 86400
            if day != self.last_day:
                self.waterings_today = 0
                self.last_day = day

            if now_sec - self.last_reading_sec >= MOISTURE_READ_INTERVAL_SEC:
                self.last_reading_sec = now_sec
                self.moisture = max(5.0, self.moisture - self.rng.uniform(0.6, 1.0))
                self._publish_moisture(now_sec)
                self._try_local_watering(now_sec)

            if now_sec - self.last_heartbeat_sec >= HEARTBEAT_INTERVAL_SEC:
                self.last_heartbeat_sec = now_sec
                self._publish_heartbeat(now_sec)

            if self.pump.running:
                self.moisture = min(100.0, self.moisture + self.rng.uniform(5.0, 10.0))

            time.sleep(0.1)


def main():
    FakeEsp32().run()


if __name__ == "__main__":
    main()
