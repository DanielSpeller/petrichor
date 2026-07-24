#pragma once

#include <cstdint>

enum class WifiState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED
};

// HARDWARE SWAP POINT: these two functions are the only place WiFi.begin()/
// WiFi.status() need to be called from. Currently mocked: wifiConnect()
// sets a mock "connected" flag, wifiIsConnected() reads it.
void wifiConnect();
bool wifiIsConnected();

// Test-only hook: forces wifiConnect() to simulate connection failure (or
// restores the default "always succeeds" mock behavior).
void setMockWifiShouldConnect(bool shouldConnect);

// Connection state machine with exponential backoff between attempts. Has
// no Arduino dependency itself (time is passed in), so it's testable
// under `native` even though real WiFi doesn't exist to test against yet.
class WifiManager {
public:
    WifiManager(uint32_t initialBackoffMs, uint32_t maxBackoffMs);

    // Call every loop() iteration with the current millis(). Drives the
    // DISCONNECTED -> CONNECTING -> CONNECTED state machine.
    void update(uint32_t nowMs);

    WifiState state() const;

private:
    void attemptConnect(uint32_t nowMs);

    WifiState state_ = WifiState::DISCONNECTED;
    uint32_t initialBackoffMs_;
    uint32_t maxBackoffMs_;
    uint32_t currentBackoffMs_;
    uint32_t nextAttemptAtMs_ = 0;
};
