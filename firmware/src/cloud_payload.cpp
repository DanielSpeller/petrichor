#include "cloud_payload.h"

#include <cstdarg>
#include <cstdio>

namespace {
bool appendf(char* buffer, size_t bufferSize, size_t& offset, const char* format, ...) {
    if (offset >= bufferSize) return false;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer + offset, bufferSize - offset, format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= bufferSize - offset) return false;
    offset += static_cast<size_t>(written);
    return true;
}
}

size_t buildIngestPayload(char* outBuffer, size_t outBufferSize, const char* deviceId,
                          const ReadingBuffer& readings, const WateringEventBuffer& events,
                          int wifiRssiDbm, float batteryVoltageV, uint32_t uptimeSec,
                          uint32_t timestamp) {
    size_t offset = 0;
    if (!appendf(outBuffer, outBufferSize, offset,
                 "{\"device_id\":\"%s\",\"timestamp\":%lu,\"readings\":[",
                 deviceId, static_cast<unsigned long>(timestamp))) return 0;
    for (size_t i = 0; i < readings.count; ++i) {
        if (!appendf(outBuffer, outBufferSize, offset,
                     "%s{\"moisture_pct\":%.1f,\"timestamp\":%lu}", i ? "," : "",
                     readings.moisturePct[i], static_cast<unsigned long>(readings.timestampSec[i]))) return 0;
    }
    if (!appendf(outBuffer, outBufferSize, offset, "],\"watering_events\":[")) return 0;
    for (size_t i = 0; i < events.count; ++i) {
        const WateringEventRecord& event = events.events[i];
        if (!appendf(outBuffer, outBufferSize, offset,
                     "%s{\"request_id\":\"%s\",\"trigger\":\"%s\",\"result\":\"%s\","
                     "\"requested_duration_sec\":%lu,\"actual_duration_sec\":%lu,"
                     "\"moisture_before_pct\":%.1f,\"timestamp\":%lu}",
                     i ? "," : "", event.requestId, event.trigger, event.result,
                     static_cast<unsigned long>(event.requestedDurationSec),
                     static_cast<unsigned long>(event.actualDurationSec), event.moistureBeforePct,
                     static_cast<unsigned long>(event.timestampSec))) return 0;
    }
    if (!appendf(outBuffer, outBufferSize, offset,
                 "],\"device_status\":{\"wifi_rssi_dbm\":%d,\"battery_voltage_v\":%.2f,"
                 "\"uptime_sec\":%lu}}", wifiRssiDbm, batteryVoltageV,
                 static_cast<unsigned long>(uptimeSec))) return 0;
    return offset;
}
