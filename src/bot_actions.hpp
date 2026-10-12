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

    // Inventory & Item Interaction
    void collect(int object_uid = -1);
    int  collectNearby(float radius_tiles = 4.0f);
    bool checkPassThroughCollect(float current_x, float current_y);
    bool collectObject(uint32_t uid, float range_tiles = -1.0f);
    int  collectByID(uint16_t item_id, float range_tiles = -1.0f);
    void drop(uint16_t item_id, int count);
    void trash(uint16_t item_id, int count);

    // Network & Social
    void say(const std::string& text);
    void warp(const std::string& world_name);
    void respawn();

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
    void setPassiveEnabled(bool en) { passive_enabled_.store(en); }
    bool isPassiveEnabled() const { return passive_enabled_.load(); }

    // Hitbox pass-through check (otomatis menyerap item saat melangkah/berada dekat)
    bool triggerPassThroughCollect(float current_x, float current_y);

private:
    void collectLoop(uint16_t filter_item_id, int delay_ms, float max_range_tiles);

    BotActions& actions_;
    WorldState& world_;
    std::atomic<bool> running_{false};
    std::atomic<bool> passive_enabled_{true}; // Always ON by default as base feature
    std::atomic<uint16_t> filter_item_id_{0};
    std::atomic<int> delay_ms_{600};
    std::thread worker_;

    std::mutex retry_mtx_;
    std::unordered_map<uint32_t, int> retry_counts_;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> collect_cooldowns_;
};

