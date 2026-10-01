#include "bot_instance.hpp"
#include "http_client.hpp"
#include "packet.hpp"
#include "game_packet.hpp"
#include "logger.hpp"
#include <cstring>
#include <chrono>
#include <thread>

BotInstance::BotInstance(int id,
                         const std::string& account_name,
                         const std::string& password,
                         const std::string& target_world,
                         const Socks5Config& proxy)
    : id_(id),
      account_name_(account_name),
      password_(password),
      target_world_(target_world),
      proxy_(proxy),
      reconnect_ctrl_(2000, 2.0f, 30000, 0.15f) {

    fingerprint_ = DeviceFingerprint::getOrCreateForAccount(account_name_);

    enet_ = std::make_unique<EnetClient>();
    world_ = std::make_unique<WorldState>();
    actions_ = std::make_unique<BotActions>(*enet_, *world_);
    autofarm_ = std::make_unique<AutoFarmController>(*actions_, *world_);
    lua_engine_ = std::make_unique<LuaEngine>(*actions_, *world_, *enet_);

    setupPacketHandlers();
}

BotInstance::~BotInstance() {
    stop();
}

std::string BotInstance::buildHandshake(const std::string& meta_str, int user_id, int token,
                                       const std::string& door_id, int lmode) {
    std::string hs;
    if (!account_name_.empty()) {
        hs += "tankIDName|" + account_name_ + "\n";
        if (!password_.empty()) {
            hs += "tankIDPass|" + password_ + "\n";
            hs += "f|1\n";
        } else if (!cached_token_.empty()) {
            hs += "ltoken|" + cached_token_ + "\n";
            hs += "f|0\n";
        } else {
            hs += "f|0\n";
        }
    } else {
        hs += "f|0\n";
    }
    hs += "requestedName|" + (account_name_.empty() ? "GuestBot" : account_name_) + "\n";
    hs += "protocol|" + std::to_string(protocol) + "\n";
    hs += "game_version|" + version + "\n";
    hs += "cbits|128\n";
    hs += "player_age|20\n";
    hs += "GDPR|1\n";
    hs += fingerprint_.toHandshakeString(meta_str);
    if (user_id > 0) hs += "user|" + std::to_string(user_id) + "\n";
    if (token > 0) hs += "token|" + std::to_string(token) + "\n";
    if (!door_id.empty()) hs += "doorID|" + door_id + "\n";
    if (lmode >= 0) hs += "lmode|" + std::to_string(lmode) + "\n";
    return hs;
}

bool BotInstance::start() {
    active_ = true;
    in_world_ = false;
    handshake_sent_ = false;
    auto_warped_ = false;
    warped_to_target_ = false;
    redirecting_ = false;

    {
        std::lock_guard<std::mutex> lock(status_mtx_);
        status_string_ = "HTTP Login...";
    }

    Logger::log("[BOT #", id_, " | ", account_name_, "] 🚀 Memulai koneksi bot...");

    // 1. HTTP Login
    if (!login_url.empty()) {
        try {
            HttpClient http(login_url, use_https);
            auto login_resp = http.login(account_name_, password_, version, platform);
            if (login_resp.success) {
                if (!login_resp.server_ip.empty()) {
                    game_host = login_resp.server_ip;
                    cached_server_ip_ = login_resp.server_ip;
                }
                if (login_resp.server_port > 0) {
                    game_port = login_resp.server_port;
                    cached_server_port_ = login_resp.server_port;
                }
                if (!login_resp.meta.empty()) cached_meta_ = login_resp.meta;
                if (!login_resp.token.empty()) cached_token_ = login_resp.token;
                Logger::log("[BOT #", id_, "] HTTP Login sukses.");
            } else {
                Logger::log("[BOT #", id_, "] HTTP Login peringatan: ", login_resp.error);
                if (!cached_server_ip_.empty() && cached_server_port_ > 0) {
                    Logger::log("[BOT #", id_, "] 🔄 Menggunakan cache server GTPS: ", cached_server_ip_, ":", cached_server_port_);
                    game_host = cached_server_ip_;
                    game_port = cached_server_port_;
                }
            }
        } catch (const std::exception& e) {
            Logger::log("[BOT #", id_, "] HTTP Login exception: ", e.what());
            if (!cached_server_ip_.empty() && cached_server_port_ > 0) {
                Logger::log("[BOT #", id_, "] 🔄 Menggunakan cache server GTPS: ", cached_server_ip_, ":", cached_server_port_);
                game_host = cached_server_ip_;
                game_port = cached_server_port_;
            }
        }
    }

    current_handshake_ = buildHandshake(cached_meta_);

    {
        std::lock_guard<std::mutex> lock(status_mtx_);
        status_string_ = "Connecting ENet...";
    }

    if (enet_->connect(game_host, game_port, 5000)) {
        Logger::log("[BOT #", id_, "] ✅ ENet terhubung ke ", game_host, ":", game_port);
        return true;
    } else {
        Logger::log("[BOT #", id_, "] ❌ Gagal terhubung ke ", game_host, ":", game_port);
        reconnect_ctrl_.onDisconnected("Gagal terhubung ke host server");
        {
            std::lock_guard<std::mutex> lock(status_mtx_);
            status_string_ = "Connect Timeout";
        }
        return false;
    }
}

void BotInstance::stop() {
    active_ = false;
    in_world_ = false;
    if (autofarm_) autofarm_->stop();
    if (lua_engine_) lua_engine_->stopScript();
    if (enet_) enet_->disconnect();

    {
        std::lock_guard<std::mutex> lock(status_mtx_);
        status_string_ = "Offline";
    }
    Logger::log("[BOT #", id_, " | ", account_name_, "] 🛑 Bot dihentikan.");
}

void BotInstance::doReconnect() {
    if (!active_ || redirecting_.load()) return;

    Logger::log("[BOT #", id_, " | ", account_name_, "] 🔄 Menjalankan Auto-Reconnect...");
    enet_->disconnect();
    world_->clearWorld();
    in_world_ = false;
    handshake_sent_ = false;
    auto_warped_ = false;
    warped_to_target_ = false;

    if (!login_url.empty() && login_url.find("growtopia") != std::string::npos) {
        try {
            HttpClient http_retry(login_url, use_https);
            auto retry_login = http_retry.login(account_name_, password_, version, platform);
            if (retry_login.success) {
                if (!retry_login.server_ip.empty()) {
                    game_host = retry_login.server_ip;
                    cached_server_ip_ = retry_login.server_ip;
                }
                if (retry_login.server_port > 0) {
                    game_port = retry_login.server_port;
                    cached_server_port_ = retry_login.server_port;
                }
                if (!retry_login.meta.empty()) cached_meta_ = retry_login.meta;
                if (!retry_login.token.empty()) cached_token_ = retry_login.token;
            } else {
                if (!cached_server_ip_.empty() && cached_server_port_ > 0) {
                    Logger::log("[BOT #", id_, "] 🔄 HTTP server_data timeout, memakai cache server: ", cached_server_ip_, ":", cached_server_port_);
                    game_host = cached_server_ip_;
                    game_port = cached_server_port_;
                }
            }
        } catch (...) {
            if (!cached_server_ip_.empty() && cached_server_port_ > 0) {
                game_host = cached_server_ip_;
                game_port = cached_server_port_;
            }
        }
    }

    current_handshake_ = buildHandshake(cached_meta_);

    if (enet_->connect(game_host, game_port, 5000)) {
        Logger::log("[BOT #", id_, "] ✅ Reconnect ENet berhasil.");
        {
            std::lock_guard<std::mutex> lock(status_mtx_);
            status_string_ = "Handshaking...";
        }
    } else {
        Logger::log("[BOT #", id_, "] ❌ Reconnect ENet gagal.");
        reconnect_ctrl_.onDisconnected("Gagal connect ENet host");
        {
            std::lock_guard<std::mutex> lock(status_mtx_);
            status_string_ = "Retry in " + std::to_string(reconnect_ctrl_.getSecondsRemaining()) + "s";
        }
    }
}

void BotInstance::poll() {
    if (!active_) return;

    if (enet_) {
        enet_->poll();
    }

    reconnect_ctrl_.update([this]() {
        doReconnect();
    });

    // Update status teks
    {
        std::lock_guard<std::mutex> lock(status_mtx_);
        if (in_world_.load()) {
            std::string wname = world_->getWorldName();
            status_string_ = "Online (" + (wname.empty() ? target_world_ : wname) + ")";
        } else if (reconnect_ctrl_.getState() == ReconnectState::WaitingReconnect) {
            status_string_ = "Retry in " + std::to_string(reconnect_ctrl_.getSecondsRemaining()) + "s (#" +
                             std::to_string(reconnect_ctrl_.getRetryCount()) + ")";
        } else if (reconnect_ctrl_.getState() == ReconnectState::Reconnecting) {
            status_string_ = "Reconnecting...";
        } else if (enet_->isConnected()) {
            status_string_ = "Lobby / Auth...";
        }
    }
}

void BotInstance::setupPacketHandlers() {
    auto warp_to_target = [this]() {
        if (!target_world_.empty() && !in_world_.load() && !warped_to_target_.exchange(true)) {
            Logger::log("[BOT #", id_, "] 🚀 Mengirim warp ke target world: ", target_world_);
            actions_->warp(target_world_);
        }
    };

    auto trigger_enter = [this, warp_to_target]() {
        if (!auto_warped_.exchange(true)) {
            Logger::log("[BOT #", id_, "] ✅ Logon authenticated, mengirim action|enter_game...");
            enet_->sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, "action|enter_game\n");
            enet_->sendText(GtPacket::NET_MESSAGE_GAME_MESSAGE, "action|enter_game\n");

            if (!target_world_.empty()) {
                std::thread([this, warp_to_target]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
                    if (!in_world_.load()) {
                        warp_to_target();
                    }
                }).detach();
            }
        }
    };

    enet_->onDisconnect([this]() {
        if (!redirecting_.load() && active_.load()) {
            in_world_ = false;
            reconnect_ctrl_.onDisconnected("ENet disconnect event");
        }
    });

    enet_->onPacket([this, warp_to_target, trigger_enter](const GtPacketData& pkt) {
        if (pkt.type == GtPacket::NET_MESSAGE_SERVER_HELLO) {
            if (!handshake_sent_.exchange(true)) {
                Logger::log("[BOT #", id_, "] Handshake dikirim setelah SERVER_HELLO.");
                enet_->sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, current_handshake_);
            }
        }
        else if (pkt.type == GtPacket::NET_MESSAGE_GAME_MESSAGE ||
                 pkt.type == GtPacket::NET_MESSAGE_GENERIC_TEXT) {
            std::string text((const char*)pkt.payload.data(), pkt.payload.size());
            while (!text.empty() && text.back() == '\0') text.pop_back();

            Logger::log("[BOT #", id_, "] 💬 Server Text: ", text);

            if (text.find("Entering game") != std::string::npos ||
                text.find("Logon authenticated") != std::string::npos) {
                trigger_enter();
            }
            if (text.find("action|ping") != std::string::npos || text.find("ping\n") == 0) {
                enet_->sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, "action|pong\n");
                Logger::log("[BOT #", id_, "] 🏓 Pong dikirim ke server");
            }
        }
        else if (pkt.type == GtPacket::NET_MESSAGE_ERROR) {
            std::string err((const char*)pkt.payload.data(), pkt.payload.size());
            while (!err.empty() && err.back() == '\0') err.pop_back();
            Logger::log("[BOT #", id_, "] ❌ ERROR dari server: ", err);
            std::lock_guard<std::mutex> lock(status_mtx_);
            status_string_ = "Error: " + err;
        }
        else if (pkt.type == GtPacket::NET_MESSAGE_GAME_PACKET) {
            if (pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                GameUpdatePacket tank_update{};
                std::memcpy(&tank_update, pkt.payload.data(), sizeof(GameUpdatePacket));
                lua_engine_->triggerOnProcessTankUpdate(tank_update);
            }

            uint8_t tank_type = pkt.payload.size() > 0 ? pkt.payload[0] : 0;

            if (tank_type == PACKET_STATE && pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                GameUpdatePacket state_pkt;
                std::memcpy(&state_pkt, pkt.payload.data(), sizeof(GameUpdatePacket));
                world_->updatePlayerPos(state_pkt.net_id, state_pkt.pos_x, state_pkt.pos_y);
            }
            else if (tank_type == PACKET_SEND_MAP_DATA) {
                in_world_ = true;
                reconnect_ctrl_.onConnected();
                if (pkt.payload.size() > sizeof(GameUpdatePacket)) {
                    world_->parseMapData(pkt.payload.data() + sizeof(GameUpdatePacket),
                                        pkt.payload.size() - sizeof(GameUpdatePacket));
                }
                world_->setWorldName(target_world_);
                Logger::log("[BOT #", id_, "] 🎉 AKUN ONLINE di world: ", target_world_);
            }
            else if (tank_type == PACKET_ITEM_CHANGE_OBJECT && pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                GameUpdatePacket item_pkt;
                std::memcpy(&item_pkt, pkt.payload.data(), sizeof(GameUpdatePacket));
                world_->addOrUpdateDroppedItem(item_pkt.net_id, static_cast<uint16_t>(item_pkt.main_data),
                                              item_pkt.count1, item_pkt.pos_x, item_pkt.pos_y);
            }
            else if (tank_type == PACKET_SEND_INVENTORY_STATE) {
                if (pkt.payload.size() > sizeof(GameUpdatePacket)) {
                    world_->parseInventory(pkt.payload.data() + sizeof(GameUpdatePacket),
                                          pkt.payload.size() - sizeof(GameUpdatePacket));
                }
                warp_to_target();
            }
            else if (tank_type == PACKET_CALL_FUNCTION && pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                GameUpdatePacket call_pkt{};
                std::memcpy(&call_pkt, pkt.payload.data(), sizeof(GameUpdatePacket));

                auto vars = parseVariantList(pkt.payload.data() + sizeof(GameUpdatePacket),
                                             pkt.payload.size() - sizeof(GameUpdatePacket));
                if (!vars.empty()) {
                    if (lua_engine_->triggerOnVariant(vars, call_pkt.net_id, static_cast<int>(call_pkt.float_var))) {
                        return;
                    }

                    std::string fn = vars[0].str_val;
                    Logger::log("[BOT #", id_, "] ⚡ Variant Function: ", fn);

                    auto get_vint = [](const Variant& v) -> int {
                        if (v.type == 9) return v.int_val;
                        if (v.type == 5) return (int)v.uint_val;
                        return v.int_val != 0 ? v.int_val : (int)v.uint_val;
                    };

                    if (fn.find("OnSuperMainStartAcceptLogon") != std::string::npos) {
                        Logger::log("[BOT #", id_, "] 🌟 OnSuperMainStartAcceptLogon diterima! Mengirim action|enter_game...");
                        trigger_enter();
                    }
                    else if (fn.find("SetHasGrowID") != std::string::npos ||
                             fn.find("OnRequestWorldSelectMenu") != std::string::npos) {
                        Logger::log("[BOT #", id_, "] 📋 Berada di World Select Menu / Lobby.");
                        {
                            std::lock_guard<std::mutex> lock(status_mtx_);
                            if (!in_world_.load()) status_string_ = "Lobby";
                        }
                        if (!target_world_.empty()) {
                            warp_to_target();
                        } else {
                            Logger::log("[BOT #", id_, "] 💡 Target world belum disetel. Gunakan input 'Warp' untuk masuk world.");
                        }
                    }
                    else if (fn.find("OnConsoleMessage") != std::string::npos) {
                        if (vars.size() > 1) {
                            Logger::log("[BOT #", id_, "] 💬 Console: ", vars[1].str_val);
                        }
                    }
                    else if (fn.find("OnDialogRequest") != std::string::npos) {
                        if (vars.size() > 1) {
                            Logger::log("[BOT #", id_, "] 📋 Dialog Request:\n", vars[1].str_val);
                        }
                    }
                    else if (fn.find("OnSpawn") != std::string::npos) {
                        in_world_ = true;
                        reconnect_ctrl_.onConnected();
                        std::string spawn_data = vars.size() > 1 ? vars[1].str_val : "";
                        world_->parseSpawn(spawn_data, account_name_);
                        Logger::log("[BOT #", id_, "] 🎉 AKUN ONLINE di world! (OnSpawn)");
                    }
                    else if (fn.find("OnSetPos") != std::string::npos) {
                        if (vars.size() > 1 && vars[1].type == 3) {
                            world_->setLocalPos(vars[1].float_val, vars[1].float_val2);
                        }
                    }
                    else if (fn.find("OnRemove") != std::string::npos) {
                        std::string remove_data = vars.size() > 1 ? vars[1].str_val : "";
                        world_->parseRemove(remove_data);
                    }
                    else if (fn.find("OnFailedToEnterWorld") != std::string::npos) {
                        Logger::log("[BOT #", id_, "] ⚠️ Gagal masuk world (rate limited / world full / invalid). Retry 2s...");
                        std::thread([this, warp_to_target]() {
                            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                            if (!in_world_.load()) {
                                warped_to_target_ = false;
                                warp_to_target();
                            }
                        }).detach();
                    }
                    else if (fn.find("OnSendToServer") != std::string::npos) {
                        int sub_port = vars.size() > 1 ? get_vint(vars[1]) : game_port;
                        int token = vars.size() > 2 ? get_vint(vars[2]) : 0;
                        int userId = vars.size() > 3 ? get_vint(vars[3]) : 0;
                        std::string ipDoor = vars.size() > 4 ? vars[4].str_val : "";
                        int lmode = vars.size() > 5 ? get_vint(vars[5]) : 1;

                        Logger::log("[BOT #", id_, "] 🔁 OnSendToServer diterima!");
                        Logger::log("         Port   : ", sub_port);
                        Logger::log("         Token  : ", token);
                        Logger::log("         UserID : ", userId);
                        Logger::log("         IPDoor : ", ipDoor);
                        Logger::log("         LMode  : ", lmode);

                        std::string sub_host = game_host;
                        std::string door_id = "0";

                        size_t pipe_pos = ipDoor.find('|');
                        if (pipe_pos != std::string::npos) {
                            std::string ip_part = ipDoor.substr(0, pipe_pos);
                            door_id = ipDoor.substr(pipe_pos + 1);
                            size_t next_pipe = door_id.find('|');
                            if (next_pipe != std::string::npos) {
                                door_id = door_id.substr(0, next_pipe);
                            }
                            if (!ip_part.empty() && ip_part != "127.0.0.1" && ip_part != "0.0.0.0" && ip_part != "localhost") {
                                sub_host = ip_part;
                            }
                        } else if (!ipDoor.empty() && ipDoor != "127.0.0.1" && ipDoor != "0.0.0.0" && ipDoor != "localhost") {
                            sub_host = ipDoor;
                        }

                        std::string sub_handshake = buildHandshake(cached_meta_, userId, token, door_id, lmode);

                        std::thread([this, sub_host, sub_port, sub_handshake]() {
                            redirecting_ = true;
                            Logger::log("[BOT #", id_, "] 🔄 Redirecting ke sub-server ", sub_host, ":", sub_port, "...");
                            enet_->disconnect();
                            std::this_thread::sleep_for(std::chrono::milliseconds(250));

                            world_->clearWorld();
                            current_handshake_ = sub_handshake;
                            handshake_sent_ = false;
                            auto_warped_ = false;
                            warped_to_target_ = false;
                            in_world_ = false;

                            if (enet_->connect(sub_host, sub_port, 6000)) {
                                Logger::log("[BOT #", id_, "] ✅ Terhubung ke sub-server ", sub_host, ":", sub_port);
                            } else {
                                Logger::log("[BOT #", id_, "] ❌ Gagal terhubung ke sub-server!");
                            }
                            redirecting_ = false;
                        }).detach();
                    }
                }
            }
            else if ((tank_type == PACKET_PING_REQUEST || tank_type == 21 || tank_type == 22 || tank_type == 23) &&
                     pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                GameUpdatePacket ping_in;
                std::memcpy(&ping_in, pkt.payload.data(), sizeof(GameUpdatePacket));

                GameUpdatePacket ping_reply{};
                ping_reply.type = PACKET_PING_REPLY;
                ping_reply.net_id = world_->getLocalNetId();
                ping_reply.main_data = ping_in.main_data;
                ping_reply.pos_x = 64.0f;
                ping_reply.pos_y = 64.0f;
                ping_reply.speed_x = 1000.0f;
                ping_reply.speed_y = 250.0f;
                actions_->sendTankPacket(ping_reply);

                if (tank_type == 23) {
                    ping_reply.type = 22;
                    actions_->sendTankPacket(ping_reply);
                }
            }
        }
    });
}

void BotInstance::warp(const std::string& world_name) {
    if (actions_) actions_->warp(world_name);
}

void BotInstance::say(const std::string& text) {
    if (actions_) actions_->say(text);
}

void BotInstance::moveTo(int tile_x, int tile_y) {
    if (actions_) actions_->moveTo(tile_x, tile_y);
}

void BotInstance::collectNearby(float radius) {
    if (actions_) actions_->collectNearby(radius);
}

void BotInstance::respawn() {
    if (actions_) actions_->respawn();
}

std::string BotInstance::getStatusString() const {
    std::lock_guard<std::mutex> lock(status_mtx_);
    return status_string_;
}

bool BotInstance::isConnected() const {
    return enet_ && enet_->isConnected();
}

int BotInstance::getPing() const {
    return enet_ ? enet_->getPing() : 0;
}
