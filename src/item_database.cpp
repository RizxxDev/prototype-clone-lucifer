#include "item_database.hpp"
#include "logger.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

static std::string decryptItemName(const char* encrypted, size_t len, int item_id) {
    static const std::string secret = "PBGHF892FASDLFDND";
    std::string decrypted(len, '\0');
    for (size_t i = 0; i < len; ++i) {
        decrypted[i] = encrypted[i] ^ (secret[(i + item_id) % secret.length()]);
    }
    return decrypted;
}

static std::string getActionTypeName(uint8_t action, uint8_t clothing) {
    switch (action) {
    case 0: return "Fist / Tool";
    case 1: return "Wrench";
    case 2: return "Door";
    case 3: return "Lock";
    case 4: return "Gems";
    case 5: return "Treasure";
    case 6: return "Deadly Block";
    case 7: return "Trampoline";
    case 8: return "Consumable";
    case 9: return "Gateway";
    case 10: return "Sign";
    case 11: return "SFX";
    case 12: return "Main Door";
    case 13: return "Platform";
    case 14: return "Bedrock";
    case 15: return "Lava";
    case 16: return "Foreground";
    case 17: return "Background";
    case 18: return "Seed";
    case 19: return (clothing > 0) ? "Clothing" : "Equipment";
    case 20: return "Animated";
    case 21: return "Scoreboard";
    case 22: return "Checkpoint";
    case 23: return "Music Note";
    case 24: return "Security Camera";
    case 26: return "Dice";
    case 27: return "Chemical";
    case 28: return "Display Block";
    case 30: return "Storage / Magplant";
    default: return "Type #" + std::to_string(action);
    }
}

ItemDatabase& ItemDatabase::instance() {
    static ItemDatabase s_instance;
    return s_instance;
}

ItemDatabase::ItemDatabase() {
    loadBuiltinFallback();
}

void ItemDatabase::loadBuiltinFallback() {
    std::lock_guard<std::mutex> lock(mtx_);
    items_.clear();
    id_to_index_.clear();

    struct BuiltinDef {
        int id;
        const char* name;
        const char* type;
        int grow;
        int rarity;
        int hits;
    };

    static const BuiltinDef defaults[] = {
        { 2, "Dirt", "Foreground", 31, 1, 3 },
        { 3, "Dirt Seed", "Seed", 31, 1, 0 },
        { 4, "Granite", "Foreground", 100, 2, 4 },
        { 5, "Granite Seed", "Seed", 100, 2, 0 },
        { 8, "Bedrock", "Bedrock", 0, 999, 0 },
        { 10, "Rock Background", "Background", 45, 1, 2 },
        { 11, "Rock Background Seed", "Seed", 45, 1, 0 },
        { 18, "Wooden Platform", "Platform", 60, 2, 2 },
        { 19, "Wooden Platform Seed", "Seed", 60, 2, 0 },
        { 26, "Door", "Door", 120, 2, 3 },
        { 27, "Door Seed", "Seed", 120, 2, 0 },
        { 32, "Sign", "Sign", 150, 3, 3 },
        { 98, "Pickaxe", "Clothing", 0, 25, 0 },
        { 242, "World Lock", "Lock", 0, 100, 0 },
        { 456, "Laser Grid", "Foreground", 345600, 48, 6 },
        { 457, "Laser Grid Seed", "Seed", 345600, 48, 0 },
        { 5666, "Pepper Tree", "Foreground", 259200, 40, 5 },
        { 5667, "Pepper Tree Seed", "Seed", 259200, 40, 0 },
        { 8270, "Chand", "Foreground", 604800, 75, 7 },
        { 8271, "Chand Seed", "Seed", 604800, 75, 0 },
        { 5640, "Magplant 5000", "Storage / Magplant", 0, 999, 0 },
        { 9556, "Rayman's Fist", "Clothing", 0, 999, 0 },
        { 1796, "Diamond Lock", "Lock", 0, 100, 0 }
    };

    for (const auto& d : defaults) {
        ItemInfo itm;
        itm.id = d.id;
        itm.name = d.name;
        itm.type = d.type;
        itm.grow_time = d.grow;
        itm.rarity = d.rarity;
        itm.break_hits = d.hits;
        id_to_index_[itm.id] = items_.size();
        items_.push_back(itm);
    }
}

bool ItemDatabase::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
    if (buffer.size() < 6) {
        return false;
    }

    if (parseBinary(buffer.data(), buffer.size())) {
        std::lock_guard<std::mutex> lock(mtx_);
        is_loaded_ = true;
        loaded_path_ = filepath;
        Logger::log("[DATABASE] 🗄️ Berhasil memuat ", items_.size(), " item dari: ", filepath);
        return true;
    }

    return false;
}

bool ItemDatabase::saveRawData(const uint8_t* data, size_t size, const std::string& filepath) {
    if (!data || size < 6) return false;

    try {
        fs::path p(filepath);
        if (p.has_parent_path() && !fs::exists(p.parent_path())) {
            fs::create_directories(p.parent_path());
        }

        std::ofstream out(filepath, std::ios::binary);
        if (out.is_open()) {
            out.write(reinterpret_cast<const char*>(data), size);
            out.close();
            Logger::log("[DATABASE] 💾 Menyimpan items.dat mentah ke: ", filepath, " (", size, " bytes)");
            return loadFromFile(filepath);
        }
    } catch (const std::exception& e) {
        Logger::log("[DATABASE] ⚠️ Gagal simpan items.dat: ", e.what());
    }
    return false;
}

bool ItemDatabase::parseBinary(const uint8_t* data, size_t size) {
    if (size < 6) return false;

    size_t offset = 0;
    auto read16 = [&](uint16_t& val) -> bool {
        if (offset + 2 > size) return false;
        std::memcpy(&val, data + offset, 2);
        offset += 2;
        return true;
    };
    auto read32 = [&](uint32_t& val) -> bool {
        if (offset + 4 > size) return false;
        std::memcpy(&val, data + offset, 4);
        offset += 4;
        return true;
    };
    auto read8 = [&](uint8_t& val) -> bool {
        if (offset + 1 > size) return false;
        val = data[offset++];
        return true;
    };
    auto skip = [&](size_t bytes) -> bool {
        if (offset + bytes > size) return false;
        offset += bytes;
        return true;
    };

    uint16_t version = 0;
    uint32_t item_count = 0;

    if (!read16(version)) return false;
    if (!read32(item_count)) return false;

    if (item_count > 100000) {
        return false; // Angka tidak wajar / file rusak
    }

    std::vector<ItemInfo> parsed_items;
    std::unordered_map<int, size_t> parsed_index;
    parsed_items.reserve(std::min<size_t>(item_count, 30000));

    for (uint32_t i = 0; i < item_count && offset < size; ++i) {
        ItemInfo itm;
        uint32_t raw_id = 0;
        uint8_t editable = 0, cat = 0, action = 0, hit_sound = 0;
        uint16_t name_len = 0;

        if (!read32(raw_id)) break;
        if (!read8(editable)) break;
        if (!read8(cat)) break;
        if (!read8(action)) break;
        if (!read8(hit_sound)) break;
        if (!read16(name_len)) break;

        if (offset + name_len > size) break;
        std::string enc_name(reinterpret_cast<const char*>(data + offset), name_len);
        offset += name_len;

        itm.id = static_cast<int>(raw_id);
        itm.name = decryptItemName(enc_name.c_str(), enc_name.length(), itm.id);
        itm.action_type = action;

        // Texture
        uint16_t tex_len = 0;
        if (!read16(tex_len)) break;
        if (!skip(tex_len)) break;

        uint32_t tex_hash = 0;
        if (!read32(tex_hash)) break;

        uint8_t item_kind = 0;
        if (!read8(item_kind)) break;

        uint32_t val1 = 0;
        if (!read32(val1)) break;

        uint8_t tex_x = 0, tex_y = 0, spread = 0, stripey = 0, collision = 0, break_hits = 0;
        if (!read8(tex_x) || !read8(tex_y) || !read8(spread) ||
            !read8(stripey) || !read8(collision) || !read8(break_hits)) break;

        itm.break_hits = break_hits / 6; // Growtopia format (hits = value / 6)

        uint32_t drop_chance = 0;
        if (!read32(drop_chance)) break;

        uint8_t clothing = 0;
        if (!read8(clothing)) break;
        itm.clothing_type = clothing;

        uint16_t rarity = 0;
        if (!read16(rarity)) break;
        itm.rarity = rarity;

        uint8_t max_amount = 0;
        if (!read8(max_amount)) break;

        // Extra file
        uint16_t extra_len = 0;
        if (!read16(extra_len)) break;
        if (!skip(extra_len)) break;

        uint32_t extra_hash = 0, audio_vol = 0;
        if (!read32(extra_hash) || !read32(audio_vol)) break;

        // Pet strings
        uint16_t pet_len = 0;
        for (int p = 0; p < 4; ++p) {
            if (!read16(pet_len)) break;
            if (!skip(pet_len)) break;
        }

        uint8_t sbase = 0, soverlay = 0, tbase = 0, tleaves = 0;
        if (!read8(sbase) || !read8(soverlay) || !read8(tbase) || !read8(tleaves)) break;

        uint32_t scolor = 0, sovercolor = 0, grow_time = 0;
        if (!read32(scolor) || !read32(sovercolor) || !read32(grow_time)) break;
        itm.grow_time = static_cast<int>(grow_time);

        itm.type = getActionTypeName(itm.action_type, itm.clothing_type);

        parsed_index[itm.id] = parsed_items.size();
        parsed_items.push_back(itm);

        // Sisanya adalah field dinamis sesuai version (skip ke item berikutnya jika diketahui panjang tetap)
        // Pada versi Growtopia standar, terdapat sisa ~30-60 bytes per item
        // Kita cukup mencari signature / batas jika perlu, atau toleran terhadap parser dasar.
    }

    if (!parsed_items.empty()) {
        std::lock_guard<std::mutex> lock(mtx_);
        items_ = std::move(parsed_items);
        id_to_index_ = std::move(parsed_index);
        return true;
    }

    return false;
}

const ItemInfo* ItemDatabase::getItem(int id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = id_to_index_.find(id);
    if (it != id_to_index_.end() && it->second < items_.size()) {
        return &items_[it->second];
    }
    return nullptr;
}

std::vector<ItemInfo> ItemDatabase::findItems(const std::string& query, size_t max_results) const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<ItemInfo> results;
    if (query.empty()) {
        size_t count = std::min(max_results, items_.size());
        for (size_t i = 0; i < count; ++i) results.push_back(items_[i]);
        return results;
    }

    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);

    for (const auto& item : items_) {
        std::string n = item.name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        std::string ids = std::to_string(item.id);

        if (n.find(q) != std::string::npos || ids.find(q) != std::string::npos) {
            results.push_back(item);
            if (results.size() >= max_results) break;
        }
    }
    return results;
}

const std::vector<ItemInfo>& ItemDatabase::getAllItems() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return items_;
}

size_t ItemDatabase::getItemCount() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return items_.size();
}

bool ItemDatabase::isLoaded() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return is_loaded_;
}

std::string ItemDatabase::getLoadedPath() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return loaded_path_;
}
