#include "wifi_manager.h"

namespace {
bool g_mockConnected = false;
bool g_mockShouldConnect = true;
}

void setMockWifiShouldConnect(bool shouldConnect) {
    g_mockShouldConnect = shouldConnect;
    if (!shouldConnect) {
        g_mockConnected = false;
    }
}

void wifiConnect() {
    // HARDWARE SWAP POINT: replace with WiFi.begin(ssid, password).
    g_mockConnected = g_mockShouldConnect;
}

bool wifiIsConnected() {
    // HARDWARE SWAP POINT: replace with (WiFi.status() == WL_CONNECTED).
    return g_mockConnected;
}

WifiManager::WifiManager(uint32_t initialBackoffMs, uint32_t maxBackoffMs)
    : initialBackoffMs_(initialBackoffMs),
      maxBackoffMs_(maxBackoffMs),
      currentBackoffMs_(initialBackoffMs) {}

void WifiManager::attemptConnect(uint32_t nowMs) {
    state_ = WifiState::CONNECTING;
    wifiConnect();
    if (wifiIsConnected()) {
        state_ = WifiState::CONNECTED;
        currentBackoffMs_ = initialBackoffMs_; // reset backoff on success
    } else {
        state_ = WifiState::DISCONNECTED;
        nextAttemptAtMs_ = nowMs + currentBackoffMs_;
        currentBackoffMs_ = currentBackoffMs_ * 2 < maxBackoffMs_ ? currentBackoffMs_ * 2 : maxBackoffMs_;
    }
}

void WifiManager::update(uint32_t nowMs) {
    if (state_ == WifiState::CONNECTED && !wifiIsConnected()) {
        state_ = WifiState::DISCONNECTED; // dropped connection
    }

    if (state_ == WifiState::DISCONNECTED && nowMs >= nextAttemptAtMs_) {
        attemptConnect(nowMs);
    }
}

WifiState WifiManager::state() const {
    return state_;
}
