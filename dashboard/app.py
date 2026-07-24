import os
from pathlib import Path

from flask import Flask, jsonify, render_template

from data_access import get_readings, get_watering_events, get_device_status

REPO_ROOT = Path(__file__).resolve().parent.parent
DB_PATH = os.environ.get("GARDEN_DB_PATH", str(REPO_ROOT / "data" / "garden.db"))
DEVICE_ID = "zone_1"

app = Flask(__name__)


@app.route("/")
def index():
    return render_template("index.html", device_id=DEVICE_ID)


@app.route("/api/readings")
def api_readings():
    readings = get_readings(DB_PATH, DEVICE_ID, limit=2000)
    return jsonify(readings)


@app.route("/api/watering-events")
def api_watering_events():
    events = get_watering_events(DB_PATH, DEVICE_ID, limit=200)
    return jsonify(events)


@app.route("/api/device-status")
def api_device_status():
    status = get_device_status(DB_PATH, DEVICE_ID)
    return jsonify(status)


if __name__ == "__main__":
    app.run(debug=True, port=5000)
