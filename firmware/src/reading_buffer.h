#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t READING_BUFFER_CAPACITY = 40;

struct ReadingBuffer {
    float moisturePct[READING_BUFFER_CAPACITY];
    uint32_t timestampSec[READING_BUFFER_CAPACITY];
    size_t count;
};

void readingBufferPush(ReadingBuffer& buffer, float moisturePct, uint32_t timestampSec);
void readingBufferClear(ReadingBuffer& buffer);
