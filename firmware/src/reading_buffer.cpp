#include "reading_buffer.h"

void readingBufferPush(ReadingBuffer& buffer, float moisturePct, uint32_t timestampSec) {
    if (buffer.count < READING_BUFFER_CAPACITY) {
        buffer.moisturePct[buffer.count] = moisturePct;
        buffer.timestampSec[buffer.count++] = timestampSec;
        return;
    }
    for (size_t i = 1; i < READING_BUFFER_CAPACITY; ++i) {
        buffer.moisturePct[i - 1] = buffer.moisturePct[i];
        buffer.timestampSec[i - 1] = buffer.timestampSec[i];
    }
    buffer.moisturePct[READING_BUFFER_CAPACITY - 1] = moisturePct;
    buffer.timestampSec[READING_BUFFER_CAPACITY - 1] = timestampSec;
}

void readingBufferClear(ReadingBuffer& buffer) { buffer.count = 0; }
