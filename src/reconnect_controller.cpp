#include "reconnect_controller.hpp"
#include "anti_detection.hpp"
#include "logger.hpp"
#include <cmath>
#include <algorithm>

ReconnectController::ReconnectController(int initial_delay_ms,
                                         float multiplier,
                                         int max_delay_ms,
                                         float jitter_ratio)
    : initial_delay_ms_(initial_delay_ms),
      multiplier_(multiplier),
      max_delay_ms_(max_delay_ms),
      jitter_ratio_(jitter_ratio) {}

void ReconnectController::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mtx_);
    enabled_ = enabled;
    if (!enabled && state_ == ReconnectState::WaitingReconnect) {
        state_ = ReconnectState::Disabled;
        Logger::log("[RECONNECT] ⏸️ Auto-reconnect dinonaktifkan.");
    }
}

bool ReconnectController::isEnabled() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return enabled_;
}

void ReconnectController::onConnected() {
    std::lock_guard<std::mutex> lock(mtx_);
    retry_count_ = 0;
    next_delay_ms_ = 0;
    state_ = ReconnectState::Connected;
    Logger::log("[RECONNECT] ✅ Koneksi aktif. Counter exponential backoff di-reset ke 0.");
}

void ReconnectController::onDisconnected(const std::string& reason) {
    std::lock_guard<std::mutex> lock(mtx_);
    last_reason_ = reason;

    if (!enabled_) {
        state_ = ReconnectState::Disabled;
        Logger::log("[RECONNECT] ⚠️ Terputus: ", reason, " (Auto-reconnect nonaktif)");
        return;
    }

    if (state_ == ReconnectState::WaitingReconnect) {
        return; // Sudah dalam antrean hitung mundur
    }

    next_delay_ms_ = calculateNextDelay();
    target_time_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(next_delay_ms_);
    state_ = ReconnectState::WaitingReconnect;
    retry_count_++;

    float sec = next_delay_ms_ / 1000.0f;
    Logger::log("[RECONNECT] ⏳ Terputus: ", (reason.empty() ? "Koneksi berakhir" : reason));
    Logger::log("             Percobaan reconnect ke-", retry_count_, " dijadwalkan dalam ", sec, " detik (Exponential Backoff)...");
}

int ReconnectController::calculateNextDelay() {
    // delay = initial * (multiplier ^ retry_count)
    double base = initial_delay_ms_ * std::pow(static_cast<double>(multiplier_), retry_count_);
    int base_int = static_cast<int>(std::min(static_cast<double>(max_delay_ms_), base));

    // Jitter acak untuk menghindari thundering herd
    int jitter = static_cast<int>(base_int * jitter_ratio_);
    return AntiDetection::getRandomDelay(base_int, jitter, 1000);
}

void ReconnectController::update(const std::function<void()>& reconnect_fn) {
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!enabled_ || state_ != ReconnectState::WaitingReconnect) {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= target_time_) {
            state_ = ReconnectState::Reconnecting;
            cb = reconnect_fn;
        }
    }

    if (cb) {
        Logger::log("[RECONNECT] 🚀 Menjalankan percobaan koneksi ulang...");
        cb();
    }
}

void ReconnectController::triggerNow(const std::function<void()>& reconnect_fn) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        state_ = ReconnectState::Reconnecting;
    }
    Logger::log("[RECONNECT] ⚡ Reconnect manual dipicu!");
    if (reconnect_fn) {
        reconnect_fn();
    }
}

void ReconnectController::cancel() {
    std::lock_guard<std::mutex> lock(mtx_);
    state_ = ReconnectState::Disconnected;
    Logger::log("[RECONNECT] 🛑 Countdown reconnect dibatalkan.");
}

ReconnectState ReconnectController::getState() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return state_;
}

std::string ReconnectController::getStateString() const {
    std::lock_guard<std::mutex> lock(mtx_);
    switch (state_) {
        case ReconnectState::Connected: return "Connected (Online)";
        case ReconnectState::WaitingReconnect: return "Waiting Reconnect (Backoff)";
        case ReconnectState::Reconnecting: return "Reconnecting...";
        case ReconnectState::Disconnected: return "Disconnected";
        case ReconnectState::Disabled: return "Disabled";
    }
    return "Unknown";
}

int ReconnectController::getRetryCount() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return retry_count_;
}

int ReconnectController::getNextDelayMs() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return next_delay_ms_;
}

int ReconnectController::getSecondsRemaining() const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (state_ != ReconnectState::WaitingReconnect) return 0;
    auto now = std::chrono::steady_clock::now();
    if (now >= target_time_) return 0;
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(target_time_ - now).count();
    return static_cast<int>(diff);
}
