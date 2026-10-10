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

    int dx = tile_x - cur_tx;
    int dy = tile_y - cur_ty;
    int steps = std::max(std::abs(dx), std::abs(dy));

    if (steps == 0) return;

    int net_id = world_.getLocalNetId();
    if (net_id < 0) net_id = -1;

    int delay = (step_delay_ms > 0) ? step_delay_ms : speed_delay_.load();

    if (steps <= 1 || delay <= 0) {
        uint32_t flags = mod_fly_.load() ? TankFlags::FLYING : TankFlags::ON_GROUND;
        if (target_x < cur_x) flags |= TankFlags::FACING_LEFT;
        auto pkt = TankPacketHelper::createState(net_id, target_x, target_y, flags);
        sendTankPacket(pkt);
        world_.setLocalPos(target_x, target_y);
        Logger::log("[ACTION] 🚶 Bergerak ke tile (", tile_x, ", ", tile_y, ") [Pixel: ", target_x, ", ", target_y, "]");
    } else {
        bool flying = mod_fly_.load();
        // Multi-langkah (misal step 2, 3, 5): jalankan di background thread agar TIDAK memblokir ENet poll / GUI
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

                if (delay > 0 && i < steps) {
                    AntiDetection::sleepWithJitter(delay, std::max(2, delay / 4));
                    AntiDetection::maybeHumanPause(0.015f, 60, 150);
                }
            }
            Logger::log("[ACTION] 🚶 Selesai bergerak ke tile (", tile_x, ", ", tile_y, ") [Pixel: ", target_x, ", ", target_y, "]");
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
    Logger::log("[ACTION] 🔧 Wrench tile (", tile_x, ", ", tile_y, ")");
}

void BotActions::wrenchPlayer(int net_id) {
    if (!enet_.isConnected()) return;
    enet_.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, "action|wrench\nnetid|" + std::to_string(net_id) + "\n");
    Logger::log("[ACTION] 🔧 Wrench player NetID=", net_id);
}

void BotActions::enter() {
    if (!enet_.isConnected()) return;

    float px = 0.0f, py = 0.0f;
    int cur_tx = 0, cur_ty = 0;
    world_.getLocalPos(px, py, cur_tx, cur_ty);

    auto pkt = TankPacketHelper::createTileActivate(world_.getLocalNetId(), cur_tx, cur_ty, px, py);
    sendTankPacket(pkt);
    Logger::log("[ACTION] 🚪 Mengaktifkan pintu/portal di tile (", cur_tx, ", ", cur_ty, ")");
}

void BotActions::collect(int object_uid) {
    if (!enet_.isConnected()) return;

    float px = 0.0f, py = 0.0f;
    int cur_tx = 0, cur_ty = 0;
    world_.getLocalPos(px, py, cur_tx, cur_ty);

    auto pkt = TankPacketHelper::createCollectObject(world_.getLocalNetId(), object_uid, px, py);
    sendTankPacket(pkt);
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
            collect(item.uid);
            collected++;
        }
    }

    // Jika daftar dropped lokal kosong atau server butuh generic collect
    if (collected == 0) {
        collect(-1);
        collected = 1;
    }

    return collected;
}

void BotActions::drop(uint16_t item_id, int count) {
    if (!enet_.isConnected()) return;
    std::string packet_str = "action|dialog_return\ndialog_name|drop_item\nitemID|"
                           + std::to_string(item_id) + "|\ncount|"
                           + std::to_string(count) + "\n";
    enet_.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, packet_str);
    Logger::log("[ACTION] 📦 Drop item ID=", item_id, " Jumlah=", count);
}

void BotActions::trash(uint16_t item_id, int count) {
    if (!enet_.isConnected()) return;
    std::string packet_str = "action|dialog_return\ndialog_name|trash_item\nitemID|"
                           + std::to_string(item_id) + "|\ncount|"
                           + std::to_string(count) + "\n";
    enet_.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, packet_str);
    Logger::log("[ACTION] 🗑️ Trash item ID=", item_id, " Jumlah=", count);
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
    Logger::log("[ACTION] 💬 Chat: \"", text, "\"");
}

void BotActions::warp(const std::string& world_name) {
    if (!enet_.isConnected() || world_name.empty()) return;
    std::string packet_str = "action|join_request\nname|" + world_name + "\ninvitedWorld|0\n";
    enet_.sendText(GtPacket::NET_MESSAGE_GAME_MESSAGE, packet_str);
    enet_.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, packet_str);
    Logger::log("[ACTION] 🚀 Warp ke world: ", world_name);
}

void BotActions::respawn() {
    if (!enet_.isConnected()) return;
    enet_.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, "action|respawn\n");
    Logger::log("[ACTION] 🔄 Respawn dikirim ke server");
}

void BotActions::requestTileChange(int tile_x, int tile_y, uint16_t item_id) {
    if (!enet_.isConnected()) return;
    float px = 0.0f, py = 0.0f;
    int cur_tx = 0, cur_ty = 0;
    world_.getLocalPos(px, py, cur_tx, cur_ty);

    auto pkt = TankPacketHelper::createTileChange(world_.getLocalNetId(), tile_x, tile_y, item_id, px, py);
    sendTankPacket(pkt);
    Logger::log("[ACTION] 🔨 RequestTileChange item=", item_id, " pada tile (", tile_x, ", ", tile_y, ")");
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
    Logger::log("[ACTION] 🕊️ ModFly: ", enable ? "ENABLED" : "DISABLED");
}

bool BotActions::isModFly() const {
    return mod_fly_.load();
}

void BotActions::setNoName(bool enable) {
    no_name_ = enable;
    Logger::log("[ACTION] 👤 NoName: ", enable ? "ENABLED" : "DISABLED");
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
    Logger::log("[ACTION] 🎒 Item Selected: ID=", item_id);
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
    Logger::log("[AUTOFARM] 🚜 Auto-Farm DIMULAI untuk Item ID=", item_id, " pada Tile (", tile_x, ", ", tile_y, ")");

    int loop_count = 0;
    while (running_) {
        // Cek stok item di inventory
        int count = world_.getItemCount(item_id);
        if (count <= 0) {
            Logger::log("[AUTOFARM] ⚠️ Stok item ID=", item_id, " habis di inventory! Menunggu item terkumpul...");
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
            Logger::log("[AUTOFARM] 🌾 Siklus panen ke-", loop_count, " selesai. Sisa stok: ", world_.getItemCount(item_id));
        }
    }

    Logger::log("[AUTOFARM] 🛑 Auto-Farm BERHENTI.");
}
