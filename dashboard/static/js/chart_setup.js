async function loadMoistureChart() {
  const response = await fetch("/api/readings");
  const readings = await response.json();

  const labels = readings.map((r) => new Date(r.timestamp * 1000));
  const values = readings.map((r) => r.moisture_pct);

  const ctx = document.getElementById("moisture-chart").getContext("2d");
  new Chart(ctx, {
    type: "line",
    data: {
      labels: labels,
      datasets: [
        {
          label: "Moisture %",
          data: values,
          borderColor: "#3987e5",
          backgroundColor: "transparent",
          borderWidth: 2,
          pointRadius: 0,
          pointHoverRadius: 4,
          tension: 0,
        },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: { mode: "index", intersect: false },
      plugins: {
        legend: { display: false },
        tooltip: {
          backgroundColor: "#1a1a19",
          titleColor: "#ffffff",
          bodyColor: "#c3c2b7",
          borderColor: "#2c2c2a",
          borderWidth: 1,
        },
      },
      scales: {
        x: {
          type: "time",
          time: { unit: "hour" },
          grid: { color: "#2c2c2a" },
          ticks: { color: "#898781" },
        },
        y: {
          min: 0,
          max: 100,
          grid: { color: "#2c2c2a" },
          ticks: { color: "#898781" },
          title: { display: true, text: "Moisture %", color: "#c3c2b7" },
        },
      },
    },
  });
}

async function loadWateringEvents() {
  const response = await fetch("/api/watering-events");
  const events = await response.json();
  const tbody = document.getElementById("events-body");
  tbody.innerHTML = "";

  if (events.length === 0) {
    tbody.innerHTML = '<tr><td colspan="6">No watering events yet.</td></tr>';
    return;
  }

  events.forEach((event) => {
    const row = document.createElement("tr");
    const started = new Date(event.started_at * 1000).toLocaleString();
    const before = event.moisture_before_pct != null ? event.moisture_before_pct.toFixed(1) + "%" : "—";
    const after = event.moisture_after_pct != null ? event.moisture_after_pct.toFixed(1) + "%" : "—";
    const duration = event.actual_duration_sec != null ? event.actual_duration_sec + "s" : "—";

    row.innerHTML = `
      <td class="primary">${started}</td>
      <td>${event.trigger_type}</td>
      <td><span class="badge ${event.status}">${event.status}</span></td>
      <td>${duration}</td>
      <td>${before}</td>
      <td>${after}</td>
    `;
    tbody.appendChild(row);
  });
}

async function loadDeviceStatus() {
  const response = await fetch("/api/device-status");
  const status = await response.json();
  const container = document.getElementById("device-status");

  if (!status) {
    container.innerHTML = '<span class="status-item">No device status recorded yet.</span>';
    return;
  }

  const nowSec = Math.floor(Date.now() / 1000);
  const secondsSinceSeen = nowSec - status.last_seen;
  const isOnline = secondsSinceSeen < 300; // offline if no heartbeat in 5 minutes
  const lastSeen = new Date(status.last_seen * 1000).toLocaleString();

  container.innerHTML = `
    <span class="status-item">
      <span class="status-dot ${isOnline ? "online" : "offline"}"></span>
      <strong>${isOnline ? "Online" : "Offline"}</strong>
    </span>
    <span class="status-item">Last seen: <strong>${lastSeen}</strong></span>
    <span class="status-item">RSSI: <strong>${status.wifi_rssi_dbm} dBm</strong></span>
    <span class="status-item">Battery: <strong>${status.battery_voltage_v.toFixed(2)} V</strong></span>
  `;
}

loadMoistureChart();
loadWateringEvents();
loadDeviceStatus();
