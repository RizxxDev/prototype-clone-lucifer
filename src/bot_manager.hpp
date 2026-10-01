#pragma once
#include <vector>
#include <memory>
#include <string>
#include <mutex>

#include "bot_instance.hpp"
#include "thread_pool.hpp"

// ============================================================
// Lucifer Bot Manager & Thread Pool Orchestrator
// Coordinates concurrent multi-bot execution, proxies, and actions
// ============================================================

class BotManager {
public:
    explicit BotManager(size_t worker_threads = 4);
    ~BotManager();

    // Manajemen Koleksi Bot
    std::shared_ptr<BotInstance> addBot(const std::string& name,
                                        const std::string& pass,
                                        const std::string& target_world = "",
                                        const Socks5Config& proxy = {});
    bool removeBot(int bot_id);
    void clearBots();

    // Kontrol Kolektif
    void startAll(int stagger_delay_ms = 1500);
    void stopAll();
    void warpAll(const std::string& world_name);
    void sayAll(const std::string& text);
    void pollAll();

    // Import / Export Profil Akun
    bool loadAccountsFromFile(const std::string& filepath);
    bool saveAccountsToFile(const std::string& filepath, bool verbose_log = false);

    // Getters & Statistik
    size_t getBotCount() const;
    size_t getOnlineCount() const;
    std::shared_ptr<BotInstance> getBotById(int id);
    std::vector<std::shared_ptr<BotInstance>> getBots();
    ThreadPool& getThreadPool() { return pool_; }

    // Konfigurasi Server Default untuk Bot Baru
    std::string default_login_url = "http://127.0.0.1/growtopia/server_data.php";
    bool default_use_https = false;
    std::string default_game_host = "127.0.0.1";
    int default_game_port = 17091;
    std::string default_version = "5.58";
    std::string default_platform = "windows";
    int default_protocol = 225;

private:
    ThreadPool pool_;
    mutable std::mutex mtx_;
    std::vector<std::shared_ptr<BotInstance>> bots_;
    int next_bot_id_ = 1;
};
