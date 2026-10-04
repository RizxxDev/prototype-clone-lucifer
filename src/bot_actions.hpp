#pragma once
#include <string>
#include <atomic>
#include <thread>
#include <cstdint>
#include <memory>
#include <chrono>
#include <mutex>

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

    // Inventory & Item Interaction
    void collect(int object_uid = -1);
    int  collectNearby(float radius_tiles = 4.0f);
    void drop(uint16_t item_id, int count);
    void trash(uint16_t item_id, int count);

    // Network & Social
    void say(const std::string& text);
    void warp(const std::string& world_name);
    void respawn();

    // Low level
    void sendTankPacket(const struct GameUpdatePacket& pkt, const uint8_t* extra = nullptr, size_t extra_len = 0);

private:
    EnetClient& enet_;
    WorldState& world_;
    std::mutex say_mtx_;
    std::chrono::steady_clock::time_point last_say_time_{};
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
