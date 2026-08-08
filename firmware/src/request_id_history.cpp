#include "request_id_history.h"

RequestIdHistory::RequestIdHistory() : nextIndex_(0) {
    for (size_t i = 0; i < HISTORY_SIZE; ++i) {
        history_[i][0] = '\0';
    }
}

bool RequestIdHistory::isDuplicate(const char* requestId) {
    if (!requestId || requestId[0] == '\0') {
        return false;
    }

    for (size_t i = 0; i < HISTORY_SIZE; ++i) {
        if (strncmp(history_[i], requestId, MAX_REQUEST_ID_LEN) == 0) {
            return true;
        }
    }

    strncpy(history_[nextIndex_], requestId, MAX_REQUEST_ID_LEN);
    history_[nextIndex_][MAX_REQUEST_ID_LEN] = '\0';
    nextIndex_ = (nextIndex_ + 1) % HISTORY_SIZE;
    return false;
}

void RequestIdHistory::clear() {
    for (size_t i = 0; i < HISTORY_SIZE; ++i) {
        history_[i][0] = '\0';
    }
    nextIndex_ = 0;
}
