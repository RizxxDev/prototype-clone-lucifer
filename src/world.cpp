#include "world.hpp"
#include "packet.hpp"
#include "logger.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>

WorldState::WorldState() {
    local_player_.is_local = true;
    local_player_.x = 640.0f;
    local_player_.y = 736.0f;
}

std::string WorldState::stripColorCodes(const std::string& text) {
    std::string clean;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '`' && i + 1 < text.size()) {
            i++; // skip '`' and color code character
            continue;
        }
        clean.push_back(text[i]);
    }
    return clean;
}

void WorldState::clearWorld() {
    std::lock_guard<std::mutex> lock(mtx_);
    world_name_.clear();
    tiles_.clear();
    players_.clear();
    dropped_items_.clear();
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

    if (is_local_type || (!clean_myname.empty() && clean_pname.find(clean_myname) != std::string::npos)) {
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
    if (size < 8) return;

    ByteReader reader(extra_data, size);
    uint8_t version = reader.u8(); // version
    uint8_t flags = reader.u8();   // flags
    (void)version;
    (void)flags;

    inventory_slots_ = reader.u32();
    uint16_t item_count = reader.u16();

    inventory_.clear();
    for (uint16_t i = 0; i < item_count && reader.ok() && reader.remaining() >= 4; ++i) {
        InventoryItem item;
        item.id = reader.u16();
        item.count = reader.u8();
        item.flags = reader.u8();
        inventory_.push_back(item);
    }

    Logger::log("[WORLD] 🎒 Inventory diperbarui: ", inventory_.size(), " item / ", inventory_slots_, " slot.");
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

        if (reader.remaining() >= 12) {
            width_ = reader.u32();
            height_ = reader.u32();
            uint32_t tile_count = reader.u32();

            if (width_ > 0 && width_ <= 200 && height_ > 0 && height_ <= 200) {
                tiles_.clear();
                tiles_.resize(width_ * height_);

                for (uint32_t i = 0; i < tile_count && reader.ok() && reader.remaining() >= 8; ++i) {
                    Tile t;
                    t.fg = reader.u16();
                    t.bg = reader.u16();
                    reader.u16(); // tile data / extra
                    reader.u8();  // flags 1
                    reader.u8();  // flags 2
                    if (i < tiles_.size()) {
                        tiles_[i] = t;
                    }
                }
                Logger::log("[WORLD] 🌍 World Data dimuat: ", world_name_, " (", width_, "x", height_, ", total tiles: ", tile_count, ")");
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
    std::lock_guard<std::mutex> lock(mtx_);
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return false;
    size_t idx = y * width_ + x;
    if (idx < tiles_.size()) {
        out_fg = tiles_[idx].fg;
        out_bg = tiles_[idx].bg;
        return true;
    }
    return false;
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
