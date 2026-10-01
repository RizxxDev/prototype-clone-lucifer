#pragma once
#include <string>
#include <chrono>
#include <functional>
#include <atomic>
#include <mutex>

// ============================================================
// Lucifer Auto-Reconnect Controller with Exponential Backoff
// ============================================================

enum class ReconnectState {
    Disconnected,
    WaitingReconnect,
    Reconnecting,
    Connected,
    Disabled
};

class ReconnectController {
public:
    ReconnectController(int initial_delay_ms = 2000,
                        float multiplier = 2.0f,
                        int max_delay_ms = 30000,
                        float jitter_ratio = 0.15f);

    // Dipanggil saat koneksi berhasil online
    void onConnected();

    // Dipanggil saat koneksi ENet putus / disconnect event
    void onDisconnected(const std::string& reason = "");

    // Polling berkala di main loop untuk mengecek timer backoff
    void update(const std::function<void()>& reconnect_fn);

    // Paksa reconnect sekarang tanpa menunggu timer
    void triggerNow(const std::function<void()>& reconnect_fn);

    // Batalkan countdown reconnect
    void cancel();

    // Pengaturan
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void setInitialDelay(int ms) { initial_delay_ms_ = ms; }
    void setMaxDelay(int ms) { max_delay_ms_ = ms; }
    void setMultiplier(float m) { multiplier_ = m; }

    // Status untuk UI / CLI
    ReconnectState getState() const;
    std::string getStateString() const;
    int getRetryCount() const;
    int getNextDelayMs() const;
    int getSecondsRemaining() const;

private:
    int calculateNextDelay();

    mutable std::mutex mtx_;
    bool enabled_ = true;
    int initial_delay_ms_ = 2000;
    float multiplier_ = 2.0f;
    int max_delay_ms_ = 30000;
    float jitter_ratio_ = 0.15f;

    ReconnectState state_ = ReconnectState::Disconnected;
    int retry_count_ = 0;
    int next_delay_ms_ = 0;
    std::chrono::steady_clock::time_point target_time_;
    std::string last_reason_;
};
