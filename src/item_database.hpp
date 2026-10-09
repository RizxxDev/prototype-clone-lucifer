#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <mutex>

// ============================================================
// Growtopia Item Database & Caching (cache/items.dat)
// ============================================================

struct ItemInfo {
    int id = 0;
    std::string name;
    std::string type = "Block";
    int grow_time = 0;
    int rarity = 0;
    int break_hits = 0;
    uint8_t action_type = 0;
    uint8_t clothing_type = 0;
    uint8_t collision_type = 0;
};

class ItemDatabase {
public:
    static ItemDatabase& instance();

    // Memuat items.dat dari file cache
    bool loadFromFile(const std::string& filepath = "cache/items.dat");

    // Menyimpan raw bytes items.dat dari packet server lalu memuatnya
    bool saveRawData(const uint8_t* data, size_t size, const std::string& filepath = "cache/items.dat");

    // Akses data item
    const ItemInfo* getItem(int id) const;
    std::vector<ItemInfo> findItems(const std::string& query, size_t max_results = 500) const;
    const std::vector<ItemInfo>& getAllItems() const;
    size_t getItemCount() const;
    bool isLoaded() const;
    std::string getLoadedPath() const;

    // Helper Collision & Door (Offsets Emulation)
    bool isCollidable(int id) const;
    bool isDoor(int id) const;

private:
    ItemDatabase();
    void loadBuiltinFallback();
    bool parseBinary(const uint8_t* data, size_t size);

    mutable std::mutex mtx_;
    std::vector<ItemInfo> items_;
    std::unordered_map<int, size_t> id_to_index_;
    bool is_loaded_ = false;
    std::string loaded_path_;
};
