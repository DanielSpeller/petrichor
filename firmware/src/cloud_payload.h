#pragma once

#include <cstddef>
#include <cstdint>
#include "reading_buffer.h"
#include "watering_event_buffer.h"

constexpr size_t CLOUD_PAYLOAD_MAX_LEN = 4096;

size_t buildIngestPayload(char* outBuffer, size_t outBufferSize, const char* deviceId,
                          const ReadingBuffer& readings, const WateringEventBuffer& events,
                          int wifiRssiDbm, float batteryVoltageV, uint32_t uptimeSec,
                          uint32_t timestamp);
