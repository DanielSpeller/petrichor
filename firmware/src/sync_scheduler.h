#pragma once

#include <cstdint>

bool syncDueThisWake(uint32_t& checksSinceLastSync, uint32_t checksPerSync);
