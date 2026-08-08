#include "sync_scheduler.h"

bool syncDueThisWake(uint32_t& checksSinceLastSync, uint32_t checksPerSync) {
    checksSinceLastSync++;
    if (checksSinceLastSync >= checksPerSync) {
        checksSinceLastSync = 0;
        return true;
    }
    return false;
}
