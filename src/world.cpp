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
                    t.locktile = reader.u16(); // tile data / extra
                    uint8_t flags1 = reader.u8();  // flags 1
                    uint8_t flags2 = reader.u8();  // flags 2
                    t.flags = static_cast<uint16_t>(flags1 | (flags2 << 8));
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

