#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <utility>

// ============================================================
// World State & Entity Models (Lucifer Architecture)
// ============================================================

struct InventoryItem {
    uint16_t id = 0;
    uint8_t count = 0;
    uint8_t flags = 0;
};

struct Player {
    int net_id = -1;
    int user_id = 0;
    std::string name;
    std::string country;
    float x = 640.0f;
    float y = 736.0f;
    bool is_local = false;
    int level = 1;
};

struct DroppedItem {
    uint32_t uid = 0;
    uint16_t id = 0;
    uint8_t count = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct Tile {
    uint16_t fg = 0;
    uint16_t bg = 0;
    uint16_t locktile = 0;
    uint16_t flags = 0;
};

struct ClientNPC {
    int id = 0;
    int type = 0;
    float x = 0.0f;
    float y = 0.0f;
    float target_x = 0.0f;
    float target_y = 0.0f;
};

class WorldState {
public:
    WorldState();

    void clearWorld();

    // Entity & World Parsing
    void parseSpawn(const std::string& spawn_str, const std::string& my_name);
    void parseRemove(const std::string& remove_str);
    void parseInventory(const uint8_t* extra_data, size_t size);
    void parseMapData(const uint8_t* extra_data, size_t size);
    void updatePlayerPos(int net_id, float x, float y);
    void addOrUpdateDroppedItem(uint32_t uid, uint16_t id, uint8_t count, float x, float y);
    void removeDroppedItem(uint32_t uid);
    void removeDroppedItemByPos(float x, float y, float tolerance = 32.0f);

    // Local Player
    void setLocalPos(float x, float y);
    void getLocalPos(float& out_x, float& out_y, int& out_tile_x, int& out_tile_y);
    int getLocalNetId();
    void setLocalNetId(int net_id);
    int getLocalLevel();

    // Getters (Thread-Safe)
    std::string getWorldName();
    void setWorldName(const std::string& name);
    int getWidth();
    int getHeight();
    bool getTile(int x, int y, uint16_t& out_fg, uint16_t& out_bg);
    bool getTile(int x, int y, uint16_t& out_fg, uint16_t& out_bg, uint16_t& out_locktile, uint16_t& out_flags);
    void setTileFlags(int x, int y, uint16_t flags);
    std::vector<Tile> getAllTiles();
    Player getLocalPlayer();
    std::vector<Player> getPlayers();
    std::vector<InventoryItem> getInventory();
    std::vector<DroppedItem> getDroppedItems();
    uint32_t getInventorySlots();
    int getItemCount(uint16_t item_id);

    // NPC Management (Bothax & Lucifer API)
    void addOrUpdateNPC(int id, int type, float x, float y, float target_x, float target_y);
    void removeNPC(int id);
    bool getNPC(int id, ClientNPC& out_npc);
    std::vector<ClientNPC> getNPCs();

    // State Flags (Offsets Emulation)
    void setCanSeeGhost(bool enable);
    bool canSeeGhost() const;
    void setIsDarkened(bool enable);
    bool isDarkened() const;

    // Collision & Pathfinding (Milestone 6.1)
    bool isTileSolid(int x, int y) const;
    bool isTileDoor(int x, int y) const;
    bool hasPath(int start_x, int start_y, int end_x, int end_y, bool can_fly = false) const;
    std::vector<std::pair<int, int>> findPath(int start_x, int start_y, int end_x, int end_y, bool can_fly = false) const;

    static std::string stripColorCodes(const std::string& text);

private:
    mutable std::mutex mtx_;
    std::string world_name_;
    int width_ = 100;
    int height_ = 60;
    std::vector<Tile> tiles_;
    std::unordered_map<int, Player> players_;
    std::vector<DroppedItem> dropped_items_;
    std::unordered_map<int, ClientNPC> npcs_;
    std::vector<InventoryItem> inventory_;
    uint32_t inventory_slots_ = 16;
    Player local_player_;
    std::atomic<bool> can_see_ghost_{false};
    std::atomic<bool> is_darkened_{false};
};
