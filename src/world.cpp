#include "world.hpp"
#include "packet.hpp"
#include "logger.hpp"
#include "item_database.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <queue>
#include <cmath>

WorldState::WorldState() {
    local_player_.is_local = true;
    local_player_.x = 640.0f;
    local_player_.y = 736.0f;
    tiles_.resize(width_ * height_);
}

std::string WorldState::stripColorCodes(const std::string& text) {
    std::string clean;
    clean.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '`') {
            if (i + 1 < text.size()) {
                i++;
            }
            continue;
        }
        if (text[i] != '\r') {
            clean.push_back(text[i]);
        }
    }
    return clean;
}

void WorldState::clearWorld() {
    std::lock_guard<std::mutex> lock(mtx_);
    world_name_.clear();
    tiles_.assign(width_ * height_, Tile{});
    players_.clear();
    dropped_items_.clear();
    npcs_.clear();
}

void WorldState::parseSpawn(const std::string& spawn_str, const std::string& my_name) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::istringstream iss(spawn_str);
    std::string line;

    Player p;
    bool is_local_type = false;

    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back();

        size_t pipe = line.find('|');
        if (pipe == std::string::npos) continue;

        std::string key = line.substr(0, pipe);
        std::string val = line.substr(pipe + 1);

        if (key == "netID") {
            try { p.net_id = std::stoi(val); } catch (...) {}
        } else if (key == "userID") {
            try { p.user_id = std::stoi(val); } catch (...) {}
        } else if (key == "level") {
            try { p.level = std::stoi(val); } catch (...) {}
        } else if (key == "name") {
            p.name = val;
        } else if (key == "country") {
            p.country = val;
        } else if (key == "type") {
            if (val == "local") is_local_type = true;
        } else if (key == "posXY") {
            size_t second_pipe = val.find('|');
            if (second_pipe != std::string::npos) {
                try {
                    p.x = std::stof(val.substr(0, second_pipe));
                    p.y = std::stof(val.substr(second_pipe + 1));
                } catch (...) {}
            }
        }
    }

    std::string clean_pname = stripColorCodes(p.name);
    std::string clean_myname = stripColorCodes(my_name);

    std::string p_lower = clean_pname;
    std::string my_lower = clean_myname;
    std::transform(p_lower.begin(), p_lower.end(), p_lower.begin(), ::tolower);
    std::transform(my_lower.begin(), my_lower.end(), my_lower.begin(), ::tolower);

    if (is_local_type || (!my_lower.empty() && p_lower.find(my_lower) != std::string::npos)) {
        p.is_local = true;
        local_player_ = p;
        Logger::log("[WORLD] ⭐ Karakter lokal terdaftar: NetID=", p.net_id, " Nama=", clean_pname, " Pos=(", p.x, ", ", p.y, ")");
    }

    if (p.net_id >= 0) {
        players_[p.net_id] = p;
    }
}

void WorldState::parseRemove(const std::string& remove_str) {
    std::lock_guard<std::mutex> lock(mtx_);
    size_t pos = remove_str.find("netID|");
    if (pos != std::string::npos) {
        std::string val = remove_str.substr(pos + 6);
        size_t end = val.find('\n');
        if (end != std::string::npos) val = val.substr(0, end);
        try {
            int net_id = std::stoi(val);
            players_.erase(net_id);
            Logger::log("[WORLD] Pemain meninggalkan world: NetID=", net_id);
        } catch (...) {}
    }
}

void WorldState::parseInventory(const uint8_t* extra_data, size_t size) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (size < 10) return;

    ByteReader reader(extra_data, size);
    uint32_t header = reader.u32(); // 1 byte version + 3 bytes padding
    (void)header;

    uint32_t slots = reader.u32();
    uint16_t item_count = reader.u16();
    (void)item_count;

    if (slots > 0 && slots <= 300) {
        inventory_slots_ = slots;
    } else {
        inventory_slots_ = 40;
    }

    inventory_.clear();
    while (reader.ok() && reader.remaining() >= 4) {
        InventoryItem item;
        item.id = reader.u16();
        item.count = reader.u8();
        item.flags = reader.u8();

        if (item.id > 0 && item.count > 0 && item.id < 20000) {
            inventory_.push_back(item);
        }
    }

    Logger::log("[WORLD] 🎒 Inventory dimuat: ", inventory_.size(), " item di tas.");
}

void WorldState::parseMapData(const uint8_t* extra_data, size_t size) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (size < 16) return;

    try {
        ByteReader reader(extra_data, size);
        reader.u16(); // unknown 2 bytes

        uint16_t name_len = reader.u16();
        if (name_len > 0 && name_len < 100 && reader.remaining() >= name_len) {
            world_name_ = reader.str(name_len);
        }

        uint32_t width = 0, height = 0, tile_count = 0;
        size_t tile_start_offset = 0;

        // 1. Direct read width, height, tile_count
        if (reader.remaining() >= 12) {
            uint32_t w = reader.u32();
            uint32_t h = reader.u32();
            uint32_t tc = reader.u32();
            if (w > 0 && w <= 200 && h > 0 && h <= 200 && tc == w * h) {
                width = w;
                height = h;
                tile_count = tc;
                tile_start_offset = reader.position();
            }
        }

        // 2. Adaptive signature scan jika direct read tidak cocok (width * height == tile_count)
        if (tile_count == 0) {
            for (size_t off = 4; off + 12 <= std::min(size, (size_t)160); ++off) {
                uint32_t w = *reinterpret_cast<const uint32_t*>(extra_data + off);
                uint32_t h = *reinterpret_cast<const uint32_t*>(extra_data + off + 4);
                uint32_t tc = *reinterpret_cast<const uint32_t*>(extra_data + off + 8);
                if (w >= 20 && w <= 200 && h >= 20 && h <= 200 && tc == w * h) {
                    width = w;
                    height = h;
                    tile_count = tc;
                    tile_start_offset = off + 12;
                    break;
                }
            }
        }

        if (width > 0 && height > 0 && tile_count > 0 && tile_start_offset > 0) {
            width_ = width;
            height_ = height;
            tiles_.clear();
            tiles_.resize(tile_count);

            reader.seek(tile_start_offset);
            for (uint32_t i = 0; i < tile_count && reader.ok() && reader.remaining() >= 8; ++i) {
                Tile t;
                t.fg = reader.u16();
                t.bg = reader.u16();
                t.locktile = reader.u16();
                uint8_t flags1 = reader.u8();
                uint8_t flags2 = reader.u8();
                t.flags = static_cast<uint16_t>(flags1 | (flags2 << 8));
                if (i < tiles_.size()) {
                    tiles_[i] = t;
                }

                // Lewati tile extra data jika bit 0 (0x0001) aktif
                if ((t.flags & 0x0001) && reader.remaining() >= 1) {
                    uint8_t extra_type = reader.u8();
                    switch (extra_type) {
                        case 0: break;
                        case 1: { // Lock
                            if (reader.remaining() >= 9) {
                                reader.skip(1); // settings flags
                                reader.skip(4); // owner uid
                                uint32_t access_count = reader.u32();
                                if (reader.remaining() >= access_count * 4) {
                                    reader.skip(access_count * 4);
                                }
                                if (reader.remaining() >= 4) {
                                    reader.skip(4); // min_level / extra
                                }
                            }
                            break;
                        }
                        case 2: { // Door
                            if (reader.remaining() >= 2) {
                                uint16_t label_len = reader.u16();
                                if (reader.remaining() >= label_len + 1) {
                                    reader.skip(label_len + 1);
                                }
                            }
                            break;
                        }
                        case 3: { // Sign
                            if (reader.remaining() >= 2) {
                                uint16_t label_len = reader.u16();
                                if (reader.remaining() >= label_len) {
                                    reader.skip(label_len);
                                }
                            }
                            break;
                        }
                        case 4: { // Tree / Seed
                            if (reader.remaining() >= 5) {
                                reader.skip(5); // age (u32) + fruit (u8)
                            }
                            break;
                        }
                        case 8: { // Dice
                            if (reader.remaining() >= 1) reader.skip(1);
                            break;
                        }
                        case 9: { // Provider
                            if (reader.remaining() >= 4) reader.skip(4);
                            break;
                        }
                        case 10: { // Achievement
                            if (reader.remaining() >= 5) reader.skip(5);
                            break;
                        }
                        case 11: { // Heart monitor
                            if (reader.remaining() >= 6) {
                                reader.skip(4);
                                uint16_t name_len = reader.u16();
                                if (reader.remaining() >= name_len) reader.skip(name_len);
                            }
                            break;
                        }
                        case 14: { // Mannequin
                            if (reader.remaining() >= 2) {
                                uint16_t text_len = reader.u16();
                                if (reader.remaining() >= text_len + 23) {
                                    reader.skip(text_len + 23);
                                }
                            }
                            break;
                        }
                        case 15: { // Magic egg
                            if (reader.remaining() >= 4) reader.skip(4);
                            break;
                        }
                        case 16: case 17: { // Game block / generator
                            if (reader.remaining() >= 1) reader.skip(1);
                            break;
                        }
                        case 18: { // Xenonite
                            if (reader.remaining() >= 5) reader.skip(5);
                            break;
                        }
                        case 19: case 20: { // Phone / Crystal
                            if (reader.remaining() >= 2) {
                                uint16_t text_len = reader.u16();
                                if (reader.remaining() >= text_len) reader.skip(text_len);
                            }
                            break;
                        }
                        case 23: { // Shelf
                            if (reader.remaining() >= 16) reader.skip(16);
                            break;
                        }
                        case 24: { // VIP entrance
                            if (reader.remaining() >= 9) {
                                reader.skip(5);
                                uint32_t access_count = reader.u32();
                                if (reader.remaining() >= access_count * 4) {
                                    reader.skip(access_count * 4);
                                }
                            }
                            break;
                        }
                        case 27: { // Weather machine
                            if (reader.remaining() >= 4) reader.skip(4);
                            break;
                        }
                        default:
                            break;
                    }
                }
            }

            Logger::log("[WORLD] 🌍 World Data dimuat: ", world_name_, " (", width_, "x", height_, ", total tiles: ", tile_count, ")");

            // ============================================================
            // Parsing Dropped Items (Floating Objects)
            // ============================================================
            dropped_items_.clear();
            bool parsed_dropped = false;

            // Jalur A: Forward parsing jika pembacaan tile tepat berakhir di header dropped items
            if (reader.ok() && reader.remaining() >= 8) {
                uint32_t d_count = reader.u32();
                uint32_t last_uid = reader.u32();
                (void)last_uid;
                if (d_count > 0 && d_count <= 4000 && reader.remaining() >= d_count * 16) {
                    bool all_valid = true;
                    std::vector<DroppedItem> temp_items;
                    temp_items.reserve(d_count);
                    for (uint32_t d = 0; d < d_count; ++d) {
                        uint16_t item_id = reader.u16();
                        float ox = reader.f32();
                        float oy = reader.f32();
                        uint8_t count = reader.u8();
                        uint8_t flags = reader.u8();
                        (void)flags;
                        uint32_t uid = reader.u32();

                        if (item_id == 0 || item_id > 25000 ||
                            std::isnan(ox) || std::isinf(ox) || ox < 0.0f || ox > 6400.0f ||
                            std::isnan(oy) || std::isinf(oy) || oy < 0.0f || oy > 6400.0f ||
                            count == 0 || uid == 0) {
                            all_valid = false;
                            break;
                        }
                        temp_items.push_back({uid, item_id, count, ox, oy});
                    }
                    if (all_valid && !temp_items.empty()) {
                        dropped_items_ = std::move(temp_items);
                        parsed_dropped = true;
                    }
                }
            }

            // Jalur B: Signature / Heuristic Scanner memindai dari akhir buffer mundur ke depan
            // Sangat andal menjamin dropped items ditemukan meskipun ada tile extra GTPS custom.
            if (!parsed_dropped && size >= 24) {
                size_t min_scan = (tile_start_offset > 0) ? (tile_start_offset + tile_count * 4) : 64;
                size_t max_scan = (size >= 24) ? (size - 24) : 0;
                if (min_scan > max_scan) min_scan = 64;

                for (size_t offset = max_scan; offset >= min_scan; --offset) {
                    uint32_t cand_count = *reinterpret_cast<const uint32_t*>(extra_data + offset);
                    uint32_t cand_last_uid = *reinterpret_cast<const uint32_t*>(extra_data + offset + 4);

                    if (cand_count == 0 || cand_count > 4000) continue;
                    if (cand_last_uid == 0) continue;

                    size_t block_bytes = static_cast<size_t>(cand_count) * 16;
                    if (offset + 8 + block_bytes > size) continue;

                    size_t trailing_bytes = size - (offset + 8 + block_bytes);
                    if (trailing_bytes > 512) continue;

                    bool valid = true;
                    std::vector<DroppedItem> temp_items;
                    temp_items.reserve(cand_count);

                    const uint8_t* p = extra_data + offset + 8;
                    for (uint32_t i = 0; i < cand_count; ++i) {
                        uint16_t item_id = *reinterpret_cast<const uint16_t*>(p);
                        float ox = *reinterpret_cast<const float*>(p + 2);
                        float oy = *reinterpret_cast<const float*>(p + 6);
                        uint8_t count = *(p + 10);
                        uint8_t flags = *(p + 11);
                        (void)flags;
                        uint32_t uid = *reinterpret_cast<const uint32_t*>(p + 12);

                        if (item_id == 0 || item_id > 25000 ||
                            std::isnan(ox) || std::isinf(ox) || ox < 0.0f || ox > 6400.0f ||
                            std::isnan(oy) || std::isinf(oy) || oy < 0.0f || oy > 6400.0f ||
                            count == 0 || uid == 0) {
                            valid = false;
                            break;
                        }

                        temp_items.push_back({uid, item_id, count, ox, oy});
                        p += 16;
                    }

                    if (valid && !temp_items.empty()) {
                        dropped_items_ = std::move(temp_items);
                        parsed_dropped = true;
                        Logger::log("[WORLD] 🔍 Signature Scanner menemukan ", dropped_items_.size(), " floating items pada offset ", offset, " (trailing=", trailing_bytes, " bytes)");
                        break;
                    }
                }
            }

            if (!dropped_items_.empty()) {
                Logger::log("[WORLD] 📦 Memuat ", dropped_items_.size(), " floating items aktif di world:");
                for (const auto& item : dropped_items_) {
                    Logger::log("       - Item ID=", item.id, " UID=", item.uid, " Count=", (int)item.count, " Pos=(", item.x, ", ", item.y, ")");
                }
            } else {
                Logger::log("[WORLD] 📦 World bersih: tidak ada floating items di world saat ini.");
            }
        }
    } catch (...) {}
}

void WorldState::updatePlayerPos(int net_id, float x, float y) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (local_player_.net_id == net_id) {
        local_player_.x = x;
        local_player_.y = y;
    }
    auto it = players_.find(net_id);
    if (it != players_.end()) {
        it->second.x = x;
        it->second.y = y;
    }
}

void WorldState::addOrUpdateDroppedItem(uint32_t uid, uint16_t id, uint8_t count, float x, float y) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& item : dropped_items_) {
        if (item.uid == uid) {
            item.id = id;
            item.count = count;
            item.x = x;
            item.y = y;
            return;
        }
    }
    dropped_items_.push_back({uid, id, count, x, y});
}

void WorldState::removeDroppedItem(uint32_t uid) {
    std::lock_guard<std::mutex> lock(mtx_);
    dropped_items_.erase(
        std::remove_if(dropped_items_.begin(), dropped_items_.end(),
                       [uid](const DroppedItem& item) { return item.uid == uid; }),
        dropped_items_.end()
    );
}

void WorldState::removeDroppedItemByPos(float x, float y, float tolerance) {
    std::lock_guard<std::mutex> lock(mtx_);
    float tol_sq = tolerance * tolerance;
    dropped_items_.erase(
        std::remove_if(dropped_items_.begin(), dropped_items_.end(),
                       [x, y, tol_sq](const DroppedItem& item) {
                           float dx = item.x - x;
                           float dy = item.y - y;
                           return (dx * dx + dy * dy) <= tol_sq;
                       }),
        dropped_items_.end()
    );
}

void WorldState::setLocalPos(float x, float y) {
    std::lock_guard<std::mutex> lock(mtx_);
    local_player_.x = x;
    local_player_.y = y;
    if (local_player_.net_id >= 0) {
        players_[local_player_.net_id].x = x;
        players_[local_player_.net_id].y = y;
    }
}

void WorldState::getLocalPos(float& out_x, float& out_y, int& out_tile_x, int& out_tile_y) {
    std::lock_guard<std::mutex> lock(mtx_);
    out_x = local_player_.x;
    out_y = local_player_.y;
    out_tile_x = static_cast<int>(out_x / 32.0f);
    out_tile_y = static_cast<int>(out_y / 32.0f);
}

int WorldState::getLocalNetId() {
    std::lock_guard<std::mutex> lock(mtx_);
    return local_player_.net_id;
}

void WorldState::setLocalNetId(int net_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    local_player_.net_id = net_id;
}

int WorldState::getLocalLevel() {
    std::lock_guard<std::mutex> lock(mtx_);
    return local_player_.level;
}

std::string WorldState::getWorldName() {
    std::lock_guard<std::mutex> lock(mtx_);
    return world_name_;
}

void WorldState::setWorldName(const std::string& name) {
    std::lock_guard<std::mutex> lock(mtx_);
    world_name_ = name;
}

int WorldState::getWidth() {
    std::lock_guard<std::mutex> lock(mtx_);
    return width_;
}

int WorldState::getHeight() {
    std::lock_guard<std::mutex> lock(mtx_);
    return height_;
}

bool WorldState::getTile(int x, int y, uint16_t& out_fg, uint16_t& out_bg) {
    uint16_t locktile = 0, flags = 0;
    return getTile(x, y, out_fg, out_bg, locktile, flags);
}

bool WorldState::getTile(int x, int y, uint16_t& out_fg, uint16_t& out_bg, uint16_t& out_locktile, uint16_t& out_flags) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return false;
    if (tiles_.size() < static_cast<size_t>(width_ * height_)) {
        tiles_.resize(width_ * height_);
    }
    size_t idx = y * width_ + x;
    if (idx < tiles_.size()) {
        out_fg = tiles_[idx].fg;
        out_bg = tiles_[idx].bg;
        out_locktile = tiles_[idx].locktile;
        out_flags = tiles_[idx].flags;
        return true;
    }
    return false;
}

void WorldState::setTileFlags(int x, int y, uint16_t flags) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    if (tiles_.size() < static_cast<size_t>(width_ * height_)) {
        tiles_.resize(width_ * height_);
    }
    size_t idx = y * width_ + x;
    if (idx < tiles_.size()) {
        tiles_[idx].flags = flags;
    }
}

std::vector<Tile> WorldState::getAllTiles() {
    std::lock_guard<std::mutex> lock(mtx_);
    return tiles_;
}

Player WorldState::getLocalPlayer() {
    std::lock_guard<std::mutex> lock(mtx_);
    return local_player_;
}

std::vector<Player> WorldState::getPlayers() {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<Player> result;
    for (const auto& pair : players_) {
        result.push_back(pair.second);
    }
    return result;
}

std::vector<InventoryItem> WorldState::getInventory() {
    std::lock_guard<std::mutex> lock(mtx_);
    return inventory_;
}

std::vector<DroppedItem> WorldState::getDroppedItems() {
    std::lock_guard<std::mutex> lock(mtx_);
    return dropped_items_;
}

uint32_t WorldState::getInventorySlots() {
    std::lock_guard<std::mutex> lock(mtx_);
    return inventory_slots_;
}

int WorldState::getItemCount(uint16_t item_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& item : inventory_) {
        if (item.id == item_id) return item.count;
    }
    return 0;
}

bool WorldState::canPickupItem(uint16_t item_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    // Gems (ID 112) tidak pernah memakai slot tas
    if (item_id == 112) return true;

    // Jika data inventory belum diterima atau slot 0, izinkan mencoba pickup
    if (inventory_slots_ == 0 || inventory_.empty()) return true;

    // Jika slot tas masih tersedia
    if (inventory_.size() < inventory_slots_) return true;

    // Jika tas penuh, periksa apakah item ID sudah ada di tas dengan count < 250 (GTPS/CPS stack hingga 250)
    for (const auto& item : inventory_) {
        if (item.id == item_id && item.count < 250) {
            return true;
        }
    }
    return false;
}

void WorldState::setCanSeeGhost(bool enable) {
    can_see_ghost_ = enable;
}

bool WorldState::canSeeGhost() const {
    return can_see_ghost_.load();
}

void WorldState::setIsDarkened(bool enable) {
    is_darkened_ = enable;
}

bool WorldState::isDarkened() const {
    return is_darkened_.load();
}

bool WorldState::isTileSolid(int x, int y) const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return true;
    size_t idx = y * width_ + x;
    if (idx >= tiles_.size()) return false;
    uint16_t fg = tiles_[idx].fg;
    if (fg == 0) return false;
    return ItemDatabase::instance().isCollidable(fg);
}

bool WorldState::isTileDoor(int x, int y) const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return false;
    size_t idx = y * width_ + x;
    if (idx >= tiles_.size()) return false;
    uint16_t fg = tiles_[idx].fg;
    if (fg == 0) return false;
    return ItemDatabase::instance().isDoor(fg);
}

bool WorldState::hasPath(int start_x, int start_y, int end_x, int end_y, bool can_fly) const {
    if (start_x == end_x && start_y == end_y) {
        return !isTileSolid(end_x, end_y);
    }
    auto path = findPath(start_x, start_y, end_x, end_y, can_fly);
    return !path.empty();
}

std::vector<std::pair<int, int>> WorldState::findPath(int start_x, int start_y, int end_x, int end_y, bool can_fly) const {
    std::vector<std::pair<int, int>> path;
    if (start_x == end_x && start_y == end_y) {
        if (!isTileSolid(end_x, end_y)) {
            path.push_back({end_x, end_y});
        }
        return path;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (start_x < 0 || start_x >= width_ || start_y < 0 || start_y >= height_) return path;
    if (end_x < 0 || end_x >= width_ || end_y < 0 || end_y >= height_) return path;

    auto isSolidLocked = [this](int x, int y) -> bool {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return true;
        size_t idx = y * width_ + x;
        if (idx >= tiles_.size()) return false;
        uint16_t fg = tiles_[idx].fg;
        if (fg == 0) return false;
        return ItemDatabase::instance().isCollidable(fg);
    };

    // Jika tile target solid, cari tile tetangga non-solid terdekat
    if (isSolidLocked(end_x, end_y)) {
        static const int adj_dx[4] = { 0, 0, -1, 1 };
        static const int adj_dy[4] = { -1, 1, 0, 0 };
        int best_x = -1, best_y = -1;
        int best_dist = 999999;
        for (int i = 0; i < 4; ++i) {
            int nx = end_x + adj_dx[i];
            int ny = end_y + adj_dy[i];
            if (!isSolidLocked(nx, ny)) {
                int d = std::abs(nx - start_x) + std::abs(ny - start_y);
                if (d < best_dist) {
                    best_dist = d;
                    best_x = nx;
                    best_y = ny;
                }
            }
        }
        if (best_x != -1) {
            end_x = best_x;
            end_y = best_y;
        } else {
            return path;
        }
    }

    if (start_x == end_x && start_y == end_y) return path;

    struct AStarNode {
        int x, y;
        float g_cost;
        float h_cost;
        int parent_x, parent_y;
        float f_cost() const { return g_cost + h_cost; }
    };

    auto heuristic = [](int x1, int y1, int x2, int y2) -> float {
        return static_cast<float>(std::abs(x1 - x2) + std::abs(y1 - y2));
    };

    auto cmp = [](const AStarNode& a, const AStarNode& b) {
        return a.f_cost() > b.f_cost();
    };

    std::priority_queue<AStarNode, std::vector<AStarNode>, decltype(cmp)> open_set(cmp);
    std::unordered_map<int, float> g_scores;
    std::unordered_map<int, std::pair<int, int>> came_from;

    auto coordKey = [this](int x, int y) -> int {
        return y * width_ + x;
    };

    AStarNode start_node{ start_x, start_y, 0.0f, heuristic(start_x, start_y, end_x, end_y), -1, -1 };
    open_set.push(start_node);
    g_scores[coordKey(start_x, start_y)] = 0.0f;

    bool reached = false;
    int max_iterations = width_ * height_ * 2;
    int iterations = 0;

    static const int dirs[4][2] = { {0, -1}, {0, 1}, {-1, 0}, {1, 0} };

    while (!open_set.empty() && iterations++ < max_iterations) {
        AStarNode current = open_set.top();
        open_set.pop();

        if (current.x == end_x && current.y == end_y) {
            reached = true;
            break;
        }

        int cur_key = coordKey(current.x, current.y);
        if (g_scores.find(cur_key) != g_scores.end() && current.g_cost > g_scores[cur_key]) {
            continue;
        }

        for (int i = 0; i < 4; ++i) {
            int nx = current.x + dirs[i][0];
            int ny = current.y + dirs[i][1];

            if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
            if (isSolidLocked(nx, ny)) continue;

            float move_cost = 1.0f;
            if (!can_fly) {
                bool has_ground = (ny + 1 >= height_) || isSolidLocked(nx, ny + 1);
                if (has_ground) move_cost = 1.0f;
                else move_cost = 1.25f;
            }

            float tentative_g = current.g_cost + move_cost;
            int next_key = coordKey(nx, ny);

            if (g_scores.find(next_key) == g_scores.end() || tentative_g < g_scores[next_key]) {
                g_scores[next_key] = tentative_g;
                came_from[next_key] = { current.x, current.y };
                AStarNode neighbor{ nx, ny, tentative_g, heuristic(nx, ny, end_x, end_y), current.x, current.y };
                open_set.push(neighbor);
            }
        }
    }

    if (reached) {
        int curr_x = end_x;
        int curr_y = end_y;
        while (!(curr_x == start_x && curr_y == start_y)) {
            path.push_back({ curr_x, curr_y });
            int k = coordKey(curr_x, curr_y);
            if (came_from.find(k) == came_from.end()) break;
            auto parent = came_from[k];
            curr_x = parent.first;
            curr_y = parent.second;
        }
        std::reverse(path.begin(), path.end());
    }

    return path;
}

void WorldState::addOrUpdateNPC(int id, int type, float x, float y, float target_x, float target_y) {
    std::lock_guard<std::mutex> lock(mtx_);
    npcs_[id] = ClientNPC{ id, type, x, y, target_x, target_y };
}

void WorldState::removeNPC(int id) {
    std::lock_guard<std::mutex> lock(mtx_);
    npcs_.erase(id);
}

bool WorldState::getNPC(int id, ClientNPC& out_npc) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = npcs_.find(id);
    if (it != npcs_.end()) {
        out_npc = it->second;
        return true;
    }
    return false;
}

std::vector<ClientNPC> WorldState::getNPCs() {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<ClientNPC> result;
    result.reserve(npcs_.size());
    for (const auto& pair : npcs_) {
        result.push_back(pair.second);
    }
    return result;
}

