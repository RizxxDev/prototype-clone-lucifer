#pragma once
#include <string>
#include <atomic>
#include <thread>
#include <cstdint>
#include <memory>
#include <chrono>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <algorithm>

class EnetClient;
class WorldState;

// ============================================================
// Lucifer Bot Actions & Auto-Farm Engine
// ============================================================

class BotActions {
public:
    BotActions(EnetClient& enet, WorldState& world);

    // Movement & World Interaction
    void moveTo(int tile_x, int tile_y, int step_delay_ms = 35);
    void hit(int tile_x, int tile_y);
    void place(uint16_t item_id, int tile_x, int tile_y);
    void wrench(int tile_x, int tile_y);
    void wrenchPlayer(int net_id);
    void enter();

    // Offsets Emulation (RequestTileChange & HandleTouch)
    void requestTileChange(int tile_x, int tile_y, uint16_t item_id);
    void handleTouch(float x, float y, uint16_t item_id = 0);

    // Feature Flags & Config (Offsets Emulation: ModFly, NoName, etc.)
    void setModFly(bool enable);
    bool isModFly() const;
    void setNoName(bool enable);
    bool isNoName() const;
    void setSpeed(int speed_delay_ms);
    int  getSpeed() const;
    void setPunchRange(int range);
    int  getPunchRange() const;
    void setBuildRange(int range);
    int  getBuildRange() const;
    void setItemSelected(uint16_t item_id);
    uint16_t getItemSelected() const;

    void collect(int object_uid = -1, float px = 0.0f, float py = 0.0f, float item_x = 0.0f, float item_y = 0.0f);
    int  collectNearby(float radius_tiles = 4.0f);
    bool checkPassThroughCollect(float current_x, float current_y, float radius_tiles = -1.0f);
    void triggerMovementCollect(float target_x, float target_y);
    bool collectObject(uint32_t uid, float range_tiles = -1.0f);
    void clearCollectCooldown(uint32_t uid);
    int  collectByID(uint16_t item_id, float range_tiles = -1.0f);
    void drop(uint16_t item_id, int count);
    void trash(uint16_t item_id, int count);

    // Movement-Integrated Auto-Collect Configuration
    void setPickupDelay(int ms) { pickup_delay_ms_.store(std::clamp(ms, 10, 1000)); }
    int  getPickupDelay() const { return pickup_delay_ms_.load(); }
    void setMovementCollectRadius(float radius_tiles) { movement_collect_radius_.store(std::max(0.5f, radius_tiles)); }
    float getMovementCollectRadius() const { return movement_collect_radius_.load(); }
    void setAutoCollectOnMove(bool enable) { auto_collect_on_move_.store(enable); }
    bool isAutoCollectOnMove() const { return auto_collect_on_move_.load(); }

    // Network & Social
    void say(const std::string& text);
    void warp(const std::string& world_name);
    void respawn();

    // Telemetry & Monitoring
    uint32_t getLastCollectedUid() const { return last_collected_uid_.load(); }
    int getTotalCollectedCount() const { return total_collected_count_.load(); }

    // Low level
    void sendTankPacket(const struct GameUpdatePacket& pkt, const uint8_t* extra = nullptr, size_t extra_len = 0);
    void setOnSendTankPacketHook(std::function<bool(const struct GameUpdatePacket&)> hook) {
        on_send_tank_hook_ = hook;
    }

private:
    EnetClient& enet_;
    WorldState& world_;
    std::mutex say_mtx_;
    std::chrono::steady_clock::time_point last_say_time_{};

    std::mutex collect_mtx_;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> collect_cooldowns_;
    std::unordered_map<uint32_t, int> collect_attempts_;

    // Movement-integrated Auto Collect parameters
    std::atomic<int> pickup_delay_ms_{20}; // Delay interval 20ms
    std::atomic<float> movement_collect_radius_{2.5f}; // Radius default 2.5 tile (80 px)
    std::atomic<bool> auto_collect_on_move_{true};
    std::atomic<uint32_t> last_collected_uid_{0};
    std::atomic<int> total_collected_count_{0};

    // Flags
    std::atomic<bool> mod_fly_{false};
    std::atomic<bool> no_name_{false};
    std::atomic<int> speed_delay_{35};
    std::atomic<int> punch_range_{3};
    std::atomic<int> build_range_{3};
    std::atomic<uint16_t> selected_item_{18}; // default 18 (Fist)
    std::function<bool(const struct GameUpdatePacket&)> on_send_tank_hook_{nullptr};
};

class AutoFarmController {
public:
    AutoFarmController(BotActions& actions, WorldState& world);
    ~AutoFarmController();

    bool start(uint16_t item_id, int tile_x, int tile_y, int delay_ms = 180, int hits_per_block = 4);
    void stop();
    bool isRunning() const;

private:
    void farmLoop(uint16_t item_id, int tile_x, int tile_y, int delay_ms, int hits_per_block);

    BotActions& actions_;
    WorldState& world_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

// ============================================================
// AutoCollectController: Native C++ Base Auto-Collect System
// ============================================================
class AutoCollectController {
public:
    AutoCollectController(BotActions& actions, WorldState& world);
    ~AutoCollectController();

    // Active pathfinding sweeper (menavigasi ke dropped items di world)
    bool start(uint16_t filter_item_id = 0, int delay_ms = 600, float max_range_tiles = 0.0f);
    void stop();
    bool isRunning() const;

    // Konfigurasi Runtime
    void setFilterItemId(uint16_t id) { filter_item_id_.store(id); }
    uint16_t getFilterItemId() const { return filter_item_id_.load(); }
    void setDelayMs(int ms) { delay_ms_.store(ms); }
    int getDelayMs() const { return delay_ms_.load(); }
    void setPassiveEnabled(bool en);
    bool isPassiveEnabled() const { return passive_enabled_.load(); }
    void setProximityRadius(float radius_tiles) { proximity_radius_.store(std::max(0.5f, radius_tiles)); }
    float getProximityRadius() const { return proximity_radius_.load(); }

    // Telemetry & Monitoring
    uint32_t getLastCollectedUid() const { return last_collected_uid_.load(); }
    int getLastScanCount() const { return last_scan_count_.load(); }
    int getTotalCollectedCount() const { return total_collected_count_.load(); }
    void recordCollected(uint32_t uid) {
        last_collected_uid_.store(uid);
        total_collected_count_.fetch_add(1);
    }

    // Passive Proximity Daemon (otomatis berjalan di latar belakang saat online)
    void startPassiveProximity();
    void stopPassiveProximity();
    bool isPassiveProximityRunning() const { return passive_loop_running_.load(); }

    // Hitbox pass-through check (otomatis menyerap item saat melangkah/berada dekat)
    bool triggerPassThroughCollect(float current_x, float current_y);

private:
    void collectLoop(uint16_t filter_item_id, int delay_ms, float max_range_tiles);
    void passiveProximityWorker();

    BotActions& actions_;
    WorldState& world_;
    std::atomic<bool> running_{false};
    std::atomic<bool> passive_enabled_{true}; // Always ON by default as base feature
    std::atomic<float> proximity_radius_{2.5f}; // Radius default 2.5 tile (80 px)
    std::atomic<uint16_t> filter_item_id_{0};
    std::atomic<int> delay_ms_{600};
    std::thread worker_;

    std::atomic<bool> passive_loop_running_{false};
    std::thread passive_thread_;

    std::atomic<uint32_t> last_collected_uid_{0};
    std::atomic<int> last_scan_count_{0};
    std::atomic<int> total_collected_count_{0};

    std::mutex retry_mtx_;
    std::unordered_map<uint32_t, int> retry_counts_;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> collect_cooldowns_;
};

