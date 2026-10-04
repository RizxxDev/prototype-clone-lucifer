#pragma once
#include <string>
#include <cstdint>
#include <chrono>
#include <mutex>

// ============================================================
// Lucifer Bot Account Stats Manager (Level, Gems, Played, Age)
// ============================================================

struct BotStats {
    int level = 1;
    int gems = 0;
    float played_hours = 0.0f;
    int age_days = 0;
    int64_t total_played_seconds = 0;
    int64_t first_seen_epoch = 0;

    // Transient session tracking
    std::chrono::steady_clock::time_point session_start{};
    bool is_session_active = false;
    mutable std::mutex mtx;

    BotStats() = default;
    BotStats(const BotStats& other);
    BotStats& operator=(const BotStats& other);

    void startSession();
    void stopSession();

    float getEffectivePlayedHours() const;
    int getEffectiveAgeDays() const;
    std::string getFormattedGems() const;
    std::string getFormattedActiveTime() const;

    // Parser dari teks respon server GTPS (dialog /status, OnConsoleMessage, dll.)
    bool parseFromText(const std::string& raw_text);

    // Persistensi ke cache/stats/<account_name>.json
    bool saveToFile(const std::string& account_name, const std::string& cache_dir = "cache/stats") const;
    bool loadFromFile(const std::string& account_name, const std::string& cache_dir = "cache/stats");
};
