#pragma once
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <functional>

#include "enet_client.hpp"
#include "world.hpp"
#include "bot_actions.hpp"
#include "lua_engine.hpp"
#include "anti_detection.hpp"
#include "reconnect_controller.hpp"
#include "socks5.hpp"
#include "cli.hpp"
#include "bot_stats.hpp"
#include "console_entry.hpp"

// ============================================================
// Lucifer Bot Instance
// Encapsulates a complete, isolated Growtopia Bot Agent
// ============================================================

class BotInstance {
public:
    BotInstance(int id,
                const std::string& account_name,
                const std::string& password,
                const std::string& target_world = "",
                const Socks5Config& proxy = {});
    ~BotInstance();

    // Kontrol Lifecycle
    bool start();
    void stop();
    void poll();

    // Tindakan Cepat
    void warp(const std::string& world_name);
    void say(const std::string& text);
    void moveTo(int tile_x, int tile_y);
    void collectNearby(float radius = 4.0f);
    void respawn();

    // Getters & Status
    int getId() const { return id_; }
    std::string getAccountName() const { return account_name_; }
    std::string getPassword() const { return password_; }
    void setPassword(const std::string& p) { password_ = p; }
    std::string getTargetWorld() const { return target_world_; }
    void setTargetWorld(const std::string& tw) { target_world_ = tw; }
    std::string getStatusString() const;
    bool isOnline() const { return in_world_.load(); }
    bool isActive() const { return active_.load(); }
    bool isConnected() const;
    int getPing() const;

    // Komponen Akses
    EnetClient& getEnet() { return *enet_; }
    WorldState& getWorld() { return *world_; }
    BotActions& getActions() { return *actions_; }
    AutoFarmController& getAutoFarm() { return *autofarm_; }
    LuaEngine& getLuaEngine() { return *lua_engine_; }
    DeviceFingerprint& getFingerprint() { return fingerprint_; }
    ReconnectController& getReconnectController() { return reconnect_ctrl_; }
    Socks5Config& getProxy() { return proxy_; }
    void setProxy(const Socks5Config& p) { proxy_ = p; }
    Socks5UdpTunnel* getProxyTunnel() { return proxy_tunnel_.get(); }

    // Deteksi IP Terbaca di Game
    std::string getEffectiveIp() const;
    void refreshPublicIpAsync();
    void setServerReportedIp(const std::string& ip) { server_reported_ip_ = ip; }
    std::string getServerReportedIp() const { return server_reported_ip_; }

    // Statistik Pemain (Level, Gems, Played, Age)
    BotStats& getStats() { return stats_; }
    const BotStats& getStats() const { return stats_; }
    void queryServerStats();

    // In-Game Console Messages (Lucifer v2.85)
    void addConsoleMessage(ConsoleMsgType type, const std::string& msg);
    std::vector<ConsoleEntry> getConsoleMessages() const;
    void clearConsoleMessages();

    // Konfigurasi Server
    std::string login_url = "http://127.0.0.1/growtopia/server_data.php";
    bool use_https = false;
    std::string game_host = "127.0.0.1";
    int game_port = 17091;
    std::string version = "5.58";
    std::string platform = "windows";
    int protocol = 225;

private:
    void setupPacketHandlers();
    std::string buildHandshake(const std::string& meta_str, int user_id = 0, int token = 0,
                               const std::string& door_id = "", int lmode = -1);
    void doReconnect();

    int id_ = 0;
    std::string account_name_;
    std::string password_;
    std::string target_world_;
    Socks5Config proxy_;
    std::unique_ptr<Socks5UdpTunnel> proxy_tunnel_;

    DeviceFingerprint fingerprint_;
    ReconnectController reconnect_ctrl_;

    std::unique_ptr<EnetClient> enet_;
    std::unique_ptr<WorldState> world_;
    std::unique_ptr<BotActions> actions_;
    std::unique_ptr<AutoFarmController> autofarm_;
    std::unique_ptr<LuaEngine> lua_engine_;

    std::atomic<bool> active_{false};
    std::atomic<bool> in_world_{false};
    std::atomic<bool> handshake_sent_{false};
    std::atomic<bool> auto_warped_{false};
    std::atomic<bool> warped_to_target_{false};
    std::atomic<bool> redirecting_{false};

    std::string current_handshake_;
    std::string cached_meta_;
    std::string cached_token_;
    std::string cached_server_ip_;
    int cached_server_port_ = 0;

    mutable std::string detected_ip_;
    std::string server_reported_ip_;
    BotStats stats_;
    std::chrono::steady_clock::time_point last_stats_save_{};
    std::chrono::steady_clock::time_point last_stats_query_{};

    mutable std::mutex status_mtx_;
    std::string status_string_ = "Offline";

    mutable std::mutex console_mtx_;
    std::vector<ConsoleEntry> console_entries_;
};
