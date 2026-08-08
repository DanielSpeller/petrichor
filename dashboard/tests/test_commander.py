import json
from unittest.mock import MagicMock, patch

import pytest

import commander as cmd


def test_build_command_payload_includes_duration_for_run():
    payload = cmd.build_command_payload(
        "zone_1", "run", 15, "manual", "req-123", 1000
    )
    assert payload == {
        "device_id": "zone_1",
        "request_id": "req-123",
        "command": "run",
        "trigger": "manual",
        "duration_sec": 15,
        "timestamp": 1000,
    }


def test_build_command_payload_omits_duration_for_stop():
    payload = cmd.build_command_payload(
        "zone_1", "stop", None, "manual", "req-123", 1000
    )
    assert "duration_sec" not in payload
    assert payload["command"] == "stop"


def test_build_command_payload_defaults_duration_when_zero():
    payload = cmd.build_command_payload(
        "zone_1", "run", 0, "manual", "req-123", 1000
    )
    assert payload["duration_sec"] == 10


def test_publish_command_retries_on_missing_ack():
    mock_client = MagicMock()
    factory = MagicMock(return_value=mock_client)

    with patch.object(cmd.mqtt, "Client", factory):
        with pytest.raises(TimeoutError):
            cmd.publish_command(
                "zone_1", "run", 10, "manual", timeout_sec=0.05, max_retries=2
            )

    assert mock_client.publish.call_count == 3
    # Each publish should be on the command topic.
    for call in mock_client.publish.call_args_list:
        assert call[0][0] == "garden/pump/command"
        data = json.loads(call[0][1])
        assert data["device_id"] == "zone_1"
        assert data["command"] == "run"
