#include "hal/moisture_sensor.h"

#include <cstdlib>

namespace {
const float* g_scriptedValues = nullptr;
int g_scriptedCount = 0;
int g_scriptedIndex = 0;
bool g_seeded = false;
}

void setScriptedMoistureSequence(const float* values, int count) {
    g_scriptedValues = values;
    g_scriptedCount = count;
    g_scriptedIndex = 0;
}

float readMoisturePercent() {
    if (g_scriptedValues != nullptr && g_scriptedCount > 0) {
        int index = g_scriptedIndex < g_scriptedCount ? g_scriptedIndex : g_scriptedCount - 1;
        float value = g_scriptedValues[index];
        if (g_scriptedIndex < g_scriptedCount) {
            g_scriptedIndex++;
        }
        return value;
    }

    if (!g_seeded) {
        std::srand(42); // fixed seed: deterministic mock run-to-run
        g_seeded = true;
    }
    return static_cast<float>(std::rand() % 10001) / 100.0f; // 0.00-100.00
}
