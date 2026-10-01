#include "bot_manager.hpp"
#include "logger.hpp"
#include "anti_detection.hpp"
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <filesystem>

BotManager::BotManager(size_t worker_threads)
    : pool_(worker_threads) {}

BotManager::~BotManager() {
    stopAll();
}

std::shared_ptr<BotInstance> BotManager::addBot(const std::string& name,
                                                const std::string& pass,
                                                const std::string& target_world,
                                                const Socks5Config& proxy) {
    std::lock_guard<std::mutex> lock(mtx_);
    int id = next_bot_id_++;
    auto bot = std::make_shared<BotInstance>(id, name, pass, target_world, proxy);

    bot->login_url = default_login_url;
    bot->use_https = default_use_https;
    bot->game_host = default_game_host;
    bot->game_port = default_game_port;
    bot->version = default_version;
    bot->platform = default_platform;
    bot->protocol = default_protocol;

    bots_.push_back(bot);
    Logger::log("[MANAGER] ➕ Bot ditambahkan: #", id, " (", name, ")");
    return bot;
}

bool BotManager::removeBot(int bot_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = std::find_if(bots_.begin(), bots_.end(), [bot_id](const std::shared_ptr<BotInstance>& b) {
        return b->getId() == bot_id;
    });

    if (it != bots_.end()) {
        (*it)->stop();
        Logger::log("[MANAGER] ➖ Bot dihapus: #", bot_id, " (", (*it)->getAccountName(), ")");
        bots_.erase(it);
        return true;
    }
    return false;
}

void BotManager::clearBots() {
    stopAll();
    std::lock_guard<std::mutex> lock(mtx_);
    bots_.clear();
    Logger::log("[MANAGER] 🧹 Semua bot telah dibersihkan.");
}

void BotManager::startAll(int stagger_delay_ms) {
    std::vector<std::shared_ptr<BotInstance>> bots_copy;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        bots_copy = bots_;
    }

    Logger::log("[MANAGER] 🚀 Memulai ", bots_copy.size(), " bot secara bertahap (Stagger: ", stagger_delay_ms, "ms)...");

    pool_.enqueue([bots_copy, stagger_delay_ms]() {
        for (size_t i = 0; i < bots_copy.size(); ++i) {
            auto& bot = bots_copy[i];
            if (!bot->isConnected()) {
                bot->start();
                if (i + 1 < bots_copy.size() && stagger_delay_ms > 0) {
                    AntiDetection::sleepWithJitter(stagger_delay_ms, stagger_delay_ms / 3);
                }
            }
        }
    });
}

void BotManager::stopAll() {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& bot : bots_) {
        bot->stop();
    }
    Logger::log("[MANAGER] 🛑 Semua bot dihentikan.");
}

void BotManager::warpAll(const std::string& world_name) {
    std::vector<std::shared_ptr<BotInstance>> bots_copy;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        bots_copy = bots_;
    }

    Logger::log("[MANAGER] 🌍 Mengirim semua bot (", bots_copy.size(), ") warp ke: ", world_name);
    pool_.enqueue([bots_copy, world_name]() {
        for (auto& bot : bots_copy) {
            bot->warp(world_name);
            AntiDetection::sleepWithJitter(250, 60);
        }
    });
}

void BotManager::sayAll(const std::string& text) {
    std::vector<std::shared_ptr<BotInstance>> bots_copy;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        bots_copy = bots_;
    }

    pool_.enqueue([bots_copy, text]() {
        for (auto& bot : bots_copy) {
            bot->say(text);
            AntiDetection::sleepWithJitter(200, 50);
        }
    });
}

void BotManager::pollAll() {
    std::vector<std::shared_ptr<BotInstance>> bots_copy;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        bots_copy = bots_;
    }

    for (auto& bot : bots_copy) {
        bot->poll();
    }
}

bool BotManager::loadAccountsFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        Logger::log("[MANAGER] ❌ Gagal membuka file akun: ", filepath);
        return false;
    }

    std::string line;
    int loaded_count = 0;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') continue;

        // Format: GrowID[:Password][:TargetWorld][:ProxyHost:ProxyPort[:ProxyUser:ProxyPass]]
        std::stringstream ss(line);
        std::vector<std::string> parts;
        std::string part;
        while (std::getline(ss, part, ':')) {
            parts.push_back(part);
        }

        if (!parts.empty() && !parts[0].empty()) {
            std::string user = parts[0];
            std::string pass = parts.size() >= 2 ? parts[1] : "";
            std::string world = parts.size() >= 3 ? parts[2] : "";

            // Cek apakah bot dengan GrowID ini sudah ada dalam pool
            bool already_exists = false;
            {
                std::lock_guard<std::mutex> lock(mtx_);
                for (const auto& b : bots_) {
                    if (b->getAccountName() == user) {
                        already_exists = true;
                        break;
                    }
                }
            }
            if (already_exists) continue;

            Socks5Config proxy;
            if (parts.size() >= 5) {
                proxy.host = parts[3];
                try { proxy.port = std::stoi(parts[4]); } catch (...) {}
                if (parts.size() >= 7) {
                    proxy.username = parts[5];
                    proxy.password = parts[6];
                }
                proxy.enabled = true;
            }

            addBot(user, pass, world, proxy);
            loaded_count++;
        }
    }

    Logger::log("[MANAGER] 📄 Berhasil memuat ", loaded_count, " akun dari: ", filepath);
    return loaded_count > 0;
}

bool BotManager::saveAccountsToFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mtx_);
    try {
        std::filesystem::path p(filepath);
        if (p.has_parent_path() && !std::filesystem::exists(p.parent_path())) {
            std::filesystem::create_directories(p.parent_path());
        }
    } catch (...) {}

    // PROTEKSI: Jika memory bots_ kosong (0 bot), JANGAN timpa file jika file sudah ada dan memiliki akun tersimpan!
    if (bots_.empty()) {
        if (std::filesystem::exists(filepath)) {
            std::ifstream check(filepath);
            std::string line;
            bool has_existing = false;
            while (std::getline(check, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
                if (!line.empty() && line[0] != '#') {
                    has_existing = true;
                    break;
                }
            }
            if (has_existing) {
                Logger::log("[MANAGER] ℹ️ File akun ", filepath, " tidak ditimpa karena pool bot di memory kosong.");
                return true;
            }
        }
    }

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << "# Lucifer Multi-Bot Accounts Export\n";
    file << "# Format: GrowID:Password:TargetWorld:ProxyHost:ProxyPort:ProxyUser:ProxyPass\n\n";

    for (const auto& bot : bots_) {
        file << bot->getAccountName() << ":" << bot->getPassword() << ":" << bot->getTargetWorld();
        if (bot->getProxy().enabled) {
            file << ":" << bot->getProxy().host << ":" << bot->getProxy().port
                 << ":" << bot->getProxy().username << ":" << bot->getProxy().password;
        }
        file << "\n";
    }

    Logger::log("[MANAGER] 💾 Daftar akun (", bots_.size(), " bot) disimpan ke: ", filepath);
    return true;
}

size_t BotManager::getBotCount() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return bots_.size();
}

size_t BotManager::getOnlineCount() const {
    std::lock_guard<std::mutex> lock(mtx_);
    size_t count = 0;
    for (const auto& b : bots_) {
        if (b->isOnline()) count++;
    }
    return count;
}

std::shared_ptr<BotInstance> BotManager::getBotById(int id) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& b : bots_) {
        if (b->getId() == id) return b;
    }
    return nullptr;
}

std::vector<std::shared_ptr<BotInstance>> BotManager::getBots() {
    std::lock_guard<std::mutex> lock(mtx_);
    return bots_;
}
