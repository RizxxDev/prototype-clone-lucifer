#include "bot_actions.hpp"
#include "game_packet.hpp"
#include "world.hpp"
#include "enet_client.hpp"
#include "packet.hpp"
#include "logger.hpp"
#include "anti_detection.hpp"
#include <cmath>
#include <chrono>

BotActions::BotActions(EnetClient& enet, WorldState& world)
    : enet_(enet), world_(world) {}

void BotActions::sendTankPacket(const GameUpdatePacket& pkt, const uint8_t* extra, size_t extra_len) {
    if (!enet_.isConnected()) return;
    if (on_send_tank_hook_ && on_send_tank_hook_(pkt)) {
        return;
    }
    auto bytes = TankPacketHelper::serialize(pkt, extra, extra_len);
    enet_.send(GtPacket::NET_MESSAGE_GAME_PACKET, bytes);
}

void BotActions::moveTo(int tile_x, int tile_y, int step_delay_ms) {
    if (!enet_.isConnected()) return;

    float cur_x = 0.0f, cur_y = 0.0f;
    int cur_tx = 0, cur_ty = 0;
    world_.getLocalPos(cur_x, cur_y, cur_tx, cur_ty);

    float target_x = static_cast<float>(tile_x * 32);
    float target_y = static_cast<float>(tile_y * 32);

    int net_id = world_.getLocalNetId();
    if (net_id < 0) net_id = -1;

    int dx = tile_x - cur_tx;
    int dy = tile_y - cur_ty;
    int steps = std::max(std::abs(dx), std::abs(dy));

    // Jika sudah berada di tile yang sama, tetap sinkronkan posisi ke server lalu cek collect
    if (steps == 0) {
        uint32_t flags = mod_fly_.load() ? TankFlags::FLYING : TankFlags::ON_GROUND;
        auto pkt = TankPacketHelper::createState(net_id, target_x, target_y, flags);
        sendTankPacket(pkt);
        world_.setLocalPos(target_x, target_y);
        triggerMovementCollect(target_x, target_y);
        return;
    }

    int delay = (step_delay_ms > 0) ? step_delay_ms : speed_delay_.load();

    if (steps <= 1 || delay <= 0) {
        uint32_t flags = mod_fly_.load() ? TankFlags::FLYING : TankFlags::ON_GROUND;
        if (target_x < cur_x) flags |= TankFlags::FACING_LEFT;

        auto pkt = TankPacketHelper::createState(net_id, target_x, target_y, flags);
        sendTankPacket(pkt);
        world_.setLocalPos(target_x, target_y);
        triggerMovementCollect(target_x, target_y);

        Logger::log("[ACTION]   Bergerak ke tile (", tile_x, ", ", tile_y, ") [Pixel: ", target_x, ", ", target_y, "]");
    } else {
        bool flying = mod_fly_.load();
        std::thread([this, cur_x, cur_y, target_x, target_y, tile_x, tile_y, steps, delay, net_id, flying]() {
            for (int i = 1; i <= steps; ++i) {
                if (!enet_.isConnected()) break;

                float progress = static_cast<float>(i) / static_cast<float>(steps);
                float step_x = cur_x + (target_x - cur_x) * progress;
                float step_y = cur_y + (target_y - cur_y) * progress;

                uint32_t flags = flying ? TankFlags::FLYING : TankFlags::ON_GROUND;
                if (target_x < cur_x) flags |= TankFlags::FACING_LEFT;

                auto pkt = TankPacketHelper::createState(net_id, step_x, step_y, flags);
                sendTankPacket(pkt);
                world_.setLocalPos(step_x, step_y);
                triggerMovementCollect(step_x, step_y);

                if (delay > 0 && i < steps) {
                    AntiDetection::sleepWithJitter(delay, std::max(2, delay / 4));
                    AntiDetection::maybeHumanPause(0.015f, 60, 150);
                }
            }
            Logger::log("[ACTION]   Selesai bergerak ke tile (", tile_x, ", ", tile_y, ") [Pixel: ", target_x, ", ", target_y, "]");
        }).detach();
    }
}

void BotActions::hit(int tile_x, int tile_y) {
    if (!enet_.isConnected()) return;

    float px = 0.0f, py = 0.0f;
    int cur_tx = 0, cur_ty = 0;
    world_.getLocalPos(px, py, cur_tx, cur_ty);

    // Punch item ID di Growtopia adalah 18 (Fist)
    auto pkt = TankPacketHelper::createTileChange(world_.getLocalNetId(), tile_x, tile_y, 18, px, py);
    sendTankPacket(pkt);
}

void BotActions::place(uint16_t item_id, int tile_x, int tile_y) {
    if (!enet_.isConnected()) return;

    float px = 0.0f, py = 0.0f;
    int cur_tx = 0, cur_ty = 0;
    world_.getLocalPos(px, py, cur_tx, cur_ty);

    auto pkt = TankPacketHelper::createTileChange(world_.getLocalNetId(), tile_x, tile_y, item_id, px, py);
    sendTankPacket(pkt);
}

void BotActions::wrench(int tile_x, int tile_y) {
    if (!enet_.isConnected()) return;

    float px = 0.0f, py = 0.0f;
    int cur_tx = 0, cur_ty = 0;
    world_.getLocalPos(px, py, cur_tx, cur_ty);

    // Wrench item ID di Growtopia adalah 32
    auto pkt = TankPacketHelper::createTileChange(world_.getLocalNetId(), tile_x, tile_y, 32, px, py);
    sendTankPacket(pkt);
    Logger::log("[ACTION]   Wrench tile (", tile_x, ", ", tile_y, ")");
}

void BotActions::wrenchPlayer(int net_id) {
    if (!enet_.isConnected()) return;
    enet_.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, "action|wrench\nnetid|" + std::to_string(net_id) + "\n");
    Logger::log("[ACTION]   Wrench player NetID=", net_id);
}

void BotActions::enter() {
    if (!enet_.isConnected()) return;

    float px = 0.0f, py = 0.0f;
    int cur_tx = 0, cur_ty = 0;
    world_.getLocalPos(px, py, cur_tx, cur_ty);

    auto pkt = TankPacketHelper::createTileActivate(world_.getLocalNetId(), cur_tx, cur_ty, px, py);
    sendTankPacket(pkt);
    Logger::log("[ACTION]   Mengaktifkan pintu/portal di tile (", cur_tx, ", ", cur_ty, ")");
}

void BotActions::collect(int object_uid, float px, float py, float item_x, float item_y) {
    if (!enet_.isConnected()) return;
    if (object_uid <= 0) return; // Abaikan UID tidak valid / negatif

    if (px == 0.0f && py == 0.0f) {
        int cur_tx = 0, cur_ty = 0;
        world_.getLocalPos(px, py, cur_tx, cur_ty);
    }

    // Resolusi koordinat item jika belum disuplai dan UID valid (> 0)
    if (item_x == 0.0f && item_y == 0.0f) {
        auto dropped = world_.getDroppedItems();
        for (const auto& it : dropped) {
            if (static_cast<int>(it.uid) == object_uid) {
                item_x = it.x;
                item_y = it.y;
                break;
            }
        }
        if (item_x == 0.0f && item_y == 0.0f) {
            item_x = px;
            item_y = py;
        }
    }

    // Kirim paket collect standar Growtopia (Type 11: net_id = -1, target_net_id = 0, main_data = object_uid)
    auto pkt = TankPacketHelper::createCollectObject(object_uid, item_x, item_y);
    sendTankPacket(pkt);

    last_collected_uid_.store(static_cast<uint32_t>(object_uid));
    total_collected_count_.fetch_add(1);
    Logger::log("[ACTION]   Mengirim paket collect UID=", object_uid,
                " PlayerPos=(", px, ", ", py, ") ItemPos=(", item_x, ", ", item_y, ")");
}

void BotActions::triggerMovementCollect(float target_x, float target_y) {
    if (!auto_collect_on_move_.load()) return;
    checkPassThroughCollect(target_x, target_y, movement_collect_radius_.load());
}

bool BotActions::checkPassThroughCollect(float current_x, float current_y, float radius_tiles) {
    if (!enet_.isConnected()) return false;
    if (current_x == 0.0f && current_y == 0.0f) {
        int tx = 0, ty = 0;
        world_.getLocalPos(current_x, current_y, tx, ty);
    }

    auto dropped = world_.getDroppedItems();
    if (dropped.empty()) {
        return false;
    }

    float r_tiles = (radius_tiles >= 0.0f) ? radius_tiles : movement_collect_radius_.load();
    // Gunakan titik tengah entitas (+16px) agar pembulatan tile akurat
    int cur_tx = static_cast<int>((current_x + 16.0f) / 32.0f);
    int cur_ty = static_cast<int>((current_y + 16.0f) / 32.0f);

    float max_dist = r_tiles * 32.0f;
    float max_dist_sq = max_dist * max_dist;

    auto now = std::chrono::steady_clock::now();
    bool collected_any = false;
    int interval_ms = pickup_delay_ms_.load();
    if (interval_ms <= 0) interval_ms = 100;

    std::lock_guard<std::mutex> lock(collect_mtx_);

    // Bersihkan cooldown yang sudah lebih dari 2 detik
    for (auto it = collect_cooldowns_.begin(); it != collect_cooldowns_.end();) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count() > 2000) {
            collect_attempts_.erase(it->first);
            it = collect_cooldowns_.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto& item : dropped) {
        if (!world_.canPickupItem(item.id)) continue;

        int item_tx = static_cast<int>((item.x + 16.0f) / 32.0f);
        int item_ty = static_cast<int>((item.y + 16.0f) / 32.0f);

        float dx = item.x - current_x;
        float dy = item.y - current_y;
        float dist_sq = dx * dx + dy * dy;

        bool same_tile = (cur_tx == item_tx && cur_ty == item_ty) ||
                         (std::abs(dx) <= 32.0f && std::abs(dy) <= 32.0f);

        bool in_range = (r_tiles <= 0.0f) ? same_tile : (same_tile || dist_sq <= max_dist_sq);

        if (in_range) {
            auto it = collect_cooldowns_.find(item.uid);
            int attempts = collect_attempts_[item.uid];
            if (it != collect_cooldowns_.end()) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
                // Gunakan jeda minimal 120ms per item agar tidak dianggap spam/flood oleh server
                int min_retry_delay = std::max(120, interval_ms);
                if (elapsed < min_retry_delay) continue;
            }

            collect_cooldowns_[item.uid] = now;
            collect_attempts_[item.uid] = attempts + 1;

            collect(static_cast<int>(item.uid), current_x, current_y, item.x, item.y);
            Logger::log("[ACTION]   [Auto-Movement] Menyerap floating item ID=", item.id,
                        " UID=", item.uid, " (", (int)item.count, "x) di Pos=(", item.x, ", ", item.y, ")");
            collected_any = true;
        }
    }
    return collected_any;
}

void BotActions::clearCollectCooldown(uint32_t uid) {
    std::lock_guard<std::mutex> lock(collect_mtx_);
    collect_cooldowns_.erase(uid);
    collect_attempts_.erase(uid);
}

bool BotActions::collectObject(uint32_t uid, float range_tiles) {
    if (!enet_.isConnected()) return false;
    float px = 0.0f, py = 0.0f;
    int cur_tx = 0, cur_ty = 0;
    world_.getLocalPos(px, py, cur_tx, cur_ty);

    auto dropped = world_.getDroppedItems();
    for (const auto& item : dropped) {
        if (item.uid == uid) {
            if (range_tiles > 0.0f) {
                float dx = item.x - px;
                float dy = item.y - py;
                float max_sq = (range_tiles * 32.0f) * (range_tiles * 32.0f);
                if (dx * dx + dy * dy > max_sq) {
                    return false; // Di luar jangkauan yang diminta
                }
            }
            collect(static_cast<int>(uid), px, py, item.x, item.y);
            return true;
        }
    }
    // Direct collect UID
    collect(static_cast<int>(uid), px, py);
    return true;
}

int BotActions::collectByID(uint16_t item_id, float range_tiles) {
    if (!enet_.isConnected()) return 0;
    float px = 0.0f, py = 0.0f;
    int cur_tx = 0, cur_ty = 0;
    world_.getLocalPos(px, py, cur_tx, cur_ty);

    auto dropped = world_.getDroppedItems();
    float max_sq = (range_tiles > 0.0f) ? ((range_tiles * 32.0f) * (range_tiles * 32.0f)) : (4.0f * 32.0f * 4.0f * 32.0f);
    int count = 0;

    for (const auto& item : dropped) {
        if (item.id == item_id) {
            float dx = item.x - px;
            float dy = item.y - py;
            if (range_tiles <= 0.0f || (dx * dx + dy * dy <= max_sq)) {
                collect(static_cast<int>(item.uid), px, py, item.x, item.y);
                count++;
            }
        }
    }
    return count;
}

int BotActions::collectNearby(float radius_tiles) {
    if (!enet_.isConnected()) return 0;

    float px = 0.0f, py = 0.0f;
    int cur_tx = 0, cur_ty = 0;
    world_.getLocalPos(px, py, cur_tx, cur_ty);

    auto dropped = world_.getDroppedItems();
    float max_dist_sq = (radius_tiles * 32.0f) * (radius_tiles * 32.0f);
    int collected = 0;

    for (const auto& item : dropped) {
        float dx = item.x - px;
        float dy = item.y - py;
        float dist_sq = dx * dx + dy * dy;

        if (dist_sq <= max_dist_sq) {
            collect(static_cast<int>(item.uid), px, py, item.x, item.y);
            collected++;
        }
    }
    return collected;
}

void BotActions::drop(uint16_t item_id, int count) {
    if (!enet_.isConnected()) return;
    std::string packet_str = "action|dialog_return\ndialog_name|drop_item\nitemID|"
                           + std::to_string(item_id) + "|\ncount|"
                           + std::to_string(count) + "\n";
    enet_.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, packet_str);
    Logger::log("[ACTION]   Drop item ID=", item_id, " Jumlah=", count);
}

void BotActions::trash(uint16_t item_id, int count) {
    if (!enet_.isConnected()) return;
    std::string packet_str = "action|dialog_return\ndialog_name|trash_item\nitemID|"
                           + std::to_string(item_id) + "|\ncount|"
                           + std::to_string(count) + "\n";
    enet_.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, packet_str);
    Logger::log("[ACTION]   Trash item ID=", item_id, " Jumlah=", count);
}

void BotActions::say(const std::string& text) {
    if (!enet_.isConnected() || text.empty()) return;

    // Anti-spam delay: pastikan ada jeda minimal 1.8 detik antar chat/command
    {
        std::unique_lock<std::mutex> lock(say_mtx_);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_say_time_).count();
        if (elapsed < 1800) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1800 - elapsed));
        }
        last_say_time_ = std::chrono::steady_clock::now();
    }

    std::string packet_str = "action|input\n|text|" + text + "\n";
    enet_.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, packet_str);
    Logger::log("[ACTION]   Chat: \"", text, "\"");
}

void BotActions::warp(const std::string& world_name) {
    if (!enet_.isConnected() || world_name.empty()) return;
    std::string packet_str = "action|join_request\nname|" + world_name + "\ninvitedWorld|0\n";
    enet_.sendText(GtPacket::NET_MESSAGE_GAME_MESSAGE, packet_str);
    enet_.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, packet_str);
    Logger::log("[ACTION]   Warp ke world: ", world_name);
}

void BotActions::respawn() {
    if (!enet_.isConnected()) return;
    enet_.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, "action|respawn\n");
    Logger::log("[ACTION]   Respawn dikirim ke server");
}

void BotActions::requestTileChange(int tile_x, int tile_y, uint16_t item_id) {
    if (!enet_.isConnected()) return;
    float px = 0.0f, py = 0.0f;
    int cur_tx = 0, cur_ty = 0;
    world_.getLocalPos(px, py, cur_tx, cur_ty);

    auto pkt = TankPacketHelper::createTileChange(world_.getLocalNetId(), tile_x, tile_y, item_id, px, py);
    sendTankPacket(pkt);
    Logger::log("[ACTION]   RequestTileChange item=", item_id, " pada tile (", tile_x, ", ", tile_y, ")");
}

void BotActions::handleTouch(float x, float y, uint16_t item_id) {
    if (!enet_.isConnected()) return;
    int tx = 0, ty = 0;
    if (x > 100.0f || y > 100.0f) {
        tx = static_cast<int>(x / 32.0f);
        ty = static_cast<int>(y / 32.0f);
    } else {
        tx = static_cast<int>(x);
        ty = static_cast<int>(y);
    }
    uint16_t use_item = (item_id != 0) ? item_id : selected_item_.load();
    requestTileChange(tx, ty, use_item);
}

void BotActions::setModFly(bool enable) {
    mod_fly_ = enable;
    Logger::log("[ACTION]   ModFly: ", enable ? "ENABLED" : "DISABLED");
}

bool BotActions::isModFly() const {
    return mod_fly_.load();
}

void BotActions::setNoName(bool enable) {
    no_name_ = enable;
    Logger::log("[ACTION]   NoName: ", enable ? "ENABLED" : "DISABLED");
}

bool BotActions::isNoName() const {
    return no_name_.load();
}

void BotActions::setSpeed(int speed_delay_ms) {
    speed_delay_ = std::max(1, speed_delay_ms);
}

int BotActions::getSpeed() const {
    return speed_delay_.load();
}

void BotActions::setPunchRange(int range) {
    punch_range_ = std::max(1, range);
}

int BotActions::getPunchRange() const {
    return punch_range_.load();
}

void BotActions::setBuildRange(int range) {
    build_range_ = std::max(1, range);
}

int BotActions::getBuildRange() const {
    return build_range_.load();
}

void BotActions::setItemSelected(uint16_t item_id) {
    selected_item_ = item_id;
    Logger::log("[ACTION]   Item Selected: ID=", item_id);
}

uint16_t BotActions::getItemSelected() const {
    return selected_item_.load();
}

// ============================================================
// AutoFarmController Implementation
// ============================================================

AutoFarmController::AutoFarmController(BotActions& actions, WorldState& world)
    : actions_(actions), world_(world) {}

AutoFarmController::~AutoFarmController() {
    stop();
}

bool AutoFarmController::start(uint16_t item_id, int tile_x, int tile_y, int delay_ms, int hits_per_block) {
    if (running_.exchange(true)) {
        return false; // sudah jalan
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::thread(&AutoFarmController::farmLoop, this, item_id, tile_x, tile_y, delay_ms, hits_per_block);
    return true;
}

void AutoFarmController::stop() {
    if (running_.exchange(false)) {
        if (worker_.joinable()) {
            worker_.join();
        }
    }
}

bool AutoFarmController::isRunning() const {
    return running_.load();
}

void AutoFarmController::farmLoop(uint16_t item_id, int tile_x, int tile_y, int delay_ms, int hits_per_block) {
    Logger::log("[AUTOFARM]   Auto-Farm DIMULAI untuk Item ID=", item_id, " pada Tile (", tile_x, ", ", tile_y, ")");

    int loop_count = 0;
    while (running_) {
        // Cek stok item di inventory
        int count = world_.getItemCount(item_id);
        if (count <= 0) {
            Logger::log("[AUTOFARM]   Stok item ID=", item_id, " habis di inventory! Menunggu item terkumpul...");
            // Coba ambil drop di sekitar
            actions_.collectNearby(3.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        // 1. Pasang blok
        actions_.place(item_id, tile_x, tile_y);
        AntiDetection::sleepWithJitter(delay_ms, std::max(5, (int)(delay_ms * 0.15f)));

        if (!running_) break;

        // 2. Pukul blok sampai hancur
        for (int h = 0; h < hits_per_block && running_; ++h) {
            actions_.hit(tile_x, tile_y);
            AntiDetection::sleepWithJitter(delay_ms, std::max(5, (int)(delay_ms * 0.15f)));
        }

        if (!running_) break;

        // 3. Ambil drop hasil panen
        actions_.collectNearby(3.0f);
        AntiDetection::sleepWithJitter(delay_ms, std::max(5, (int)(delay_ms * 0.15f)));

        AntiDetection::maybeHumanPause(0.03f, 150, 350);

        loop_count++;
        if (loop_count % 10 == 0) {
            Logger::log("[AUTOFARM]   Siklus panen ke-", loop_count, " selesai. Sisa stok: ", world_.getItemCount(item_id));
        }
    }

    Logger::log("[AUTOFARM]   Auto-Farm BERHENTI.");
}

// ============================================================
// AutoCollectController Implementation
// ============================================================

AutoCollectController::AutoCollectController(BotActions& actions, WorldState& world)
    : actions_(actions), world_(world) {
    startPassiveProximity();
}

AutoCollectController::~AutoCollectController() {
    stopPassiveProximity();
    stop();
}

void AutoCollectController::setPassiveEnabled(bool en) {
    passive_enabled_.store(en);
    if (en) {
        startPassiveProximity();
    } else {
        stopPassiveProximity();
    }
}

void AutoCollectController::startPassiveProximity() {
    if (passive_loop_running_.exchange(true)) return;
    if (passive_thread_.joinable()) passive_thread_.join();
    passive_thread_ = std::thread(&AutoCollectController::passiveProximityWorker, this);
}

void AutoCollectController::stopPassiveProximity() {
    if (passive_loop_running_.exchange(false)) {
        if (passive_thread_.joinable()) {
            passive_thread_.join();
        }
    }
}

void AutoCollectController::passiveProximityWorker() {
    while (passive_loop_running_) {
        if (passive_enabled_.load()) {
            float px = 0.0f, py = 0.0f;
            int tx = 0, ty = 0;
            world_.getLocalPos(px, py, tx, ty);
            if (px > 0.0f || py > 0.0f) {
                auto dropped = world_.getDroppedItems();
                last_scan_count_.store(static_cast<int>(dropped.size()));
                bool collected = actions_.checkPassThroughCollect(px, py, proximity_radius_.load());
                if (collected) {
                    last_collected_uid_.store(actions_.getLastCollectedUid());
                    total_collected_count_.fetch_add(1);
                }
            }
        }
        int tick_delay = std::max(100, actions_.getPickupDelay());
        std::this_thread::sleep_for(std::chrono::milliseconds(tick_delay));
    }
}

bool AutoCollectController::start(uint16_t filter_item_id, int delay_ms, float max_range_tiles) {
    if (running_.exchange(true)) {
        return false; // sudah berjalan
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    filter_item_id_.store(filter_item_id);
    delay_ms_.store(delay_ms > 0 ? delay_ms : 600);
    worker_ = std::thread(&AutoCollectController::collectLoop, this, filter_item_id, delay_ms, max_range_tiles);
    return true;
}

void AutoCollectController::stop() {
    if (running_.exchange(false)) {
        if (worker_.joinable()) {
            worker_.join();
        }
    }
}

bool AutoCollectController::isRunning() const {
    return running_.load();
}

bool AutoCollectController::triggerPassThroughCollect(float current_x, float current_y) {
    if (!passive_enabled_.load()) return false;
    bool res = actions_.checkPassThroughCollect(current_x, current_y, proximity_radius_.load());
    if (res) {
        last_collected_uid_.store(actions_.getLastCollectedUid());
        total_collected_count_.fetch_add(1);
    }
    return res;
}

void AutoCollectController::collectLoop(uint16_t filter_item_id, int delay_ms, float max_range_tiles) {
    Logger::log("[AUTOCOLLECT]   Active Sweeper DIMULAI (Filter ID=", filter_item_id, ", Delay=", delay_ms, "ms)");
    {
        std::lock_guard<std::mutex> lock(retry_mtx_);
        retry_counts_.clear();
    }

    while (running_) {
        float px = 0.0f, py = 0.0f;
        int ptx = 0, pty = 0;
        world_.getLocalPos(px, py, ptx, pty);

        auto dropped = world_.getDroppedItems();
        if (dropped.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            continue;
        }

        // Bersihkan retry_counts_ untuk item yang sudah tidak ada di world
        {
            std::lock_guard<std::mutex> lock(retry_mtx_);
            for (auto it = retry_counts_.begin(); it != retry_counts_.end();) {
                bool exists = false;
                for (const auto& d : dropped) {
                    if (d.uid == it->first) { exists = true; break; }
                }
                if (!exists) {
                    it = retry_counts_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Cari item terdekat yang valid
        const DroppedItem* best_item = nullptr;
        float best_dist_sq = 1e9f;

        for (const auto& item : dropped) {
            if (filter_item_id > 0 && item.id != filter_item_id) {
                continue;
            }
            if (!world_.canPickupItem(item.id)) {
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(retry_mtx_);
                auto it = retry_counts_.find(item.uid);
                if (it != retry_counts_.end() && it->second >= 5) {
                    continue;
                }
            }

            float dx = item.x - px;
            float dy = item.y - py;
            float dist_sq = dx * dx + dy * dy;

            if (max_range_tiles > 0.0f) {
                float max_px = max_range_tiles * 32.0f;
                if (dist_sq > (max_px * max_px)) continue;
            }

            if (dist_sq < best_dist_sq) {
                best_dist_sq = dist_sq;
                best_item = &item;
            }
        }

        if (!best_item || !running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        int target_tx = static_cast<int>((best_item->x + 16.0f) / 32.0f);
        int target_ty = static_cast<int>((best_item->y + 16.0f) / 32.0f);

        // Jika belum berada di tile item, jalan mendekat selangkah demi selangkah
        if (ptx != target_tx || pty != target_ty) {
            bool can_fly = actions_.isModFly();
            auto waypoints = world_.findPath(ptx, pty, target_tx, target_ty, can_fly);
            if (waypoints.empty()) {
                // Jalan bertahap 1 tile per langkah agar sinkron (tidak detach di background)
                int cx = ptx, cy = pty;
                while ((cx != target_tx || cy != target_ty) && running_) {
                    if (cx < target_tx) cx++;
                    else if (cx > target_tx) cx--;
                    if (cy < target_ty) cy++;
                    else if (cy > target_ty) cy--;
                    actions_.moveTo(cx, cy);
                    std::this_thread::sleep_for(std::chrono::milliseconds(actions_.getSpeed()));
                }
            } else {
                for (const auto& wp : waypoints) {
                    if (!running_) break;
                    actions_.moveTo(wp.first, wp.second);
                    std::this_thread::sleep_for(std::chrono::milliseconds(actions_.getSpeed()));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (!running_) break;

        float cur_px = 0.0f, cur_py = 0.0f;
        int cur_tx = 0, cur_ty = 0;
        world_.getLocalPos(cur_px, cur_py, cur_tx, cur_ty);
        float dx = best_item->x - cur_px;
        float dy = best_item->y - cur_py;

        if ((cur_tx == target_tx && cur_ty == target_ty) || (dx * dx + dy * dy <= (48.0f * 48.0f))) {
            {
                std::lock_guard<std::mutex> lock(retry_mtx_);
                retry_counts_[best_item->uid]++;
            }
            actions_.clearCollectCooldown(best_item->uid);
            actions_.collect(static_cast<int>(best_item->uid), cur_px, cur_py, best_item->x, best_item->y);
            recordCollected(best_item->uid);

            int d = delay_ms_.load();
            if (d <= 0) d = 600;
            AntiDetection::sleepWithJitter(d, std::max(10, (int)(d * 0.15f)));
            AntiDetection::maybeHumanPause(0.02f, 100, 250);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    Logger::log("[AUTOCOLLECT]   Active Sweeper BERHENTI.");
}