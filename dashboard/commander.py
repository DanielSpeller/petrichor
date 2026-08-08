"""MQTT command publisher with application-level QoS 1 retry.

Publishes a `garden/pump/command` message and waits for the matching
`garden/pump/ack` from the device. If no ack arrives within a timeout, the
command is retried up to a limit. This compensates for the fact that the
firmware's PubSubClient cannot send true MQTT QoS 1 publishes.

Run directly:
    python dashboard/commander.py --device zone_1 --command run --duration 15
"""

import argparse
import json
import os
import time
import uuid

import paho.mqtt.client as mqtt

BROKER = os.environ.get("MQTT_BROKER", "127.0.0.1")
PORT = int(os.environ.get("MQTT_PORT", "1883"))

COMMAND_TOPIC = "garden/pump/command"
ACK_TOPIC = "garden/pump/ack"
STATUS_TOPIC = "garden/pump/status"

DEFAULT_TIMEOUT_SEC = 2.0
DEFAULT_RETRIES = 3


def build_command_payload(
    device_id: str,
    command: str,
    duration_sec: int | None,
    trigger: str,
    request_id: str,
    timestamp: int,
) -> dict:
    payload = {
        "device_id": device_id,
        "request_id": request_id,
        "command": command,
        "trigger": trigger,
        "timestamp": timestamp,
    }
    if command == "run":
        payload["duration_sec"] = duration_sec or 10
    return payload


def publish_command(
    device_id: str,
    command: str,
    duration_sec: int | None,
    trigger: str,
    timeout_sec: float = DEFAULT_TIMEOUT_SEC,
    max_retries: int = DEFAULT_RETRIES,
):
    request_id = str(uuid.uuid4())
    payload = build_command_payload(
        device_id, command, duration_sec, trigger, request_id, int(time.time())
    )

    ack_received = {"ok": False}
    status_received = {"ok": False, "payload": None}

    def on_message(_client, _userdata, msg):
        try:
            data = json.loads(msg.payload.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return
        if data.get("request_id") != request_id:
            return
        if msg.topic == ACK_TOPIC:
            ack_received["ok"] = True
        elif msg.topic == STATUS_TOPIC:
            status_received["ok"] = True
            status_received["payload"] = data

    client = mqtt.Client()
    client.on_message = on_message
    client.connect(BROKER, PORT, 60)
    client.subscribe([(ACK_TOPIC, 0), (STATUS_TOPIC, 0)])
    client.loop_start()

    try:
        attempt = 0
        while attempt <= max_retries and not ack_received["ok"]:
            print(
                f"Publishing {command} to {device_id} "
                f"(request_id={request_id}, attempt={attempt + 1})"
            )
            client.publish(COMMAND_TOPIC, json.dumps(payload), qos=1)
            deadline = time.time() + timeout_sec
            while time.time() < deadline and not ack_received["ok"]:
                time.sleep(0.05)
            attempt += 1

        if not ack_received["ok"]:
            raise TimeoutError(f"No ack received after {max_retries + 1} attempts")

        print(f"Ack received for {request_id}")

        # Also wait briefly for the final status, but don't fail if it is delayed.
        deadline = time.time() + timeout_sec
        while time.time() < deadline and not status_received["ok"]:
            time.sleep(0.05)

        if status_received["ok"]:
            print(f"Status: {status_received['payload']}")
        else:
            print("Status not yet available; command was accepted.")
    finally:
        client.loop_stop()
        client.disconnect()


def main():
    parser = argparse.ArgumentParser(description="Publish pump commands over MQTT.")
    parser.add_argument("--device", default="zone_1", help="Target device_id")
    parser.add_argument("--command", choices=["run", "stop"], required=True)
    parser.add_argument("--duration", type=int, help="Duration in seconds (run only)")
    parser.add_argument("--trigger", default="manual", choices=["moisture", "schedule", "manual"])
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_SEC, help="Ack timeout")
    parser.add_argument("--retries", type=int, default=DEFAULT_RETRIES, help="Max retries")
    args = parser.parse_args()

    publish_command(
        args.device,
        args.command,
        args.duration,
        args.trigger,
        timeout_sec=args.timeout,
        max_retries=args.retries,
    )


if __name__ == "__main__":
    main()
