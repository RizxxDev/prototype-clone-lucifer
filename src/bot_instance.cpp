#include "bot_instance.hpp"
#include "http_client.hpp"
#include "packet.hpp"
#include "game_packet.hpp"
#include "item_database.hpp"
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
    lua_engine_->setProxyCallbacks(
        [this]() { return proxy_; },
        [this](const Socks5Config& p) { proxy_ = p; }
    );
    lua_engine_->setStats(&stats_);

    stats_.loadFromFile(account_name_);
    setupPacketHandlers();
    refreshPublicIpAsync();
}

BotInstance::~BotInstance() {
    stop();
    stats_.stopSession();
    stats_.saveToFile(account_name_);
}

static std::string extractIpFromText(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (isdigit(static_cast<unsigned char>(text[i]))) {
            size_t start = i;
            int dots = 0;
            while (i < text.size() && (isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.')) {
                if (text[i] == '.') dots++;
                i++;
            }
            if (dots == 3) {
                std::string candidate = text.substr(start, i - start);
                std::stringstream ss(candidate);
                std::string seg;
                int count = 0;
                bool ok = true;
                while (std::getline(ss, seg, '.')) {
                    count++;
                    try {
                        int v = std::stoi(seg);
                        if (v < 0 || v > 255) ok = false;
                    } catch (...) { ok = false; }
                }
                if (ok && count == 4 && candidate != "0.0.0.0" && candidate != "127.0.0.1") {
                    return candidate;
                }
            }
        }
    }
    return "";
}

static std::string getFormattedConsoleTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return std::string(buf);
}

void BotInstance::addConsoleMessage(ConsoleMsgType type, const std::string& msg) {
    if (msg.empty()) return;
    std::lock_guard<std::mutex> lock(console_mtx_);
    ConsoleEntry entry;
    entry.type = type;
    entry.timestamp = getFormattedConsoleTimestamp();
    entry.text = msg;
    console_entries_.push_back(std::move(entry));
    if (console_entries_.size() > 500) {
        console_entries_.erase(console_entries_.begin(), console_entries_.begin() + 50);
    }
}

std::vector<ConsoleEntry> BotInstance::getConsoleMessages() const {
    std::lock_guard<std::mutex> lock(console_mtx_);
    return console_entries_;
}

void BotInstance::clearConsoleMessages() {
    std::lock_guard<std::mutex> lock(console_mtx_);
    console_entries_.clear();
}

std::string BotInstance::getEffectiveIp() const {
    if (!server_reported_ip_.empty()) {
        return server_reported_ip_;
    }
    if (!detected_ip_.empty()) {
        return detected_ip_;
    }
    if (proxy_.isValid() && !proxy_.host.empty()) {
        return proxy_.host;
    }
    return HttpClient::getCachedDirectIp();
}

void BotInstance::refreshPublicIpAsync() {
    Socks5Config p = proxy_;
    std::thread([this, p]() {
        std::string ip = HttpClient::fetchPublicIp(p);
        if (!ip.empty()) {
            detected_ip_ = ip;
            Logger::log("[BOT #", id_, "] 🌐 IP publik aktif terdeteksi: ", ip);
        }
    }).detach();
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

    addConsoleMessage(ConsoleMsgType::System, "Getting Server Address...");

    Logger::log("[BOT #", id_, " | ", account_name_, "] 🚀 Memulai koneksi bot",
                (proxy_.isValid() ? (" via SOCKS5 " + proxy_.toString()) : " (Direct)"), "...");

    refreshPublicIpAsync();

    // 1. HTTP Login
    if (!login_url.empty()) {
        try {
            HttpClient http(login_url, use_https, proxy_);
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

    std::string connect_host = game_host;
    int connect_port = game_port;

    // 2. Terowongan SOCKS5 UDP jika dikonfigurasi
    if (proxy_.isValid()) {
        Logger::log("[BOT #", id_, "] 🛡️ Menginisialisasi SOCKS5 UDP Tunnel (", proxy_.toString(), ")...");
        if (!proxy_tunnel_) {
            proxy_tunnel_ = std::make_unique<Socks5UdpTunnel>();
        }
        std::string terr;
        if (proxy_tunnel_->start(proxy_, game_host, static_cast<uint16_t>(game_port), terr)) {
            connect_host = proxy_tunnel_->getLocalHost();
            connect_port = proxy_tunnel_->getLocalPort();
            Logger::log("[BOT #", id_, "] ✅ SOCKS5 UDP Tunnel aktif (Full TCP+UDP): 127.0.0.1:", connect_port, " -> ", game_host, ":", game_port);
        } else {
            Logger::log("[BOT #", id_, "] ⚠️ SOCKS5 UDP Tunnel tidak aktif: ", terr);
            if (proxy_.udp_fallback_direct) {
                Logger::log("[BOT #", id_, "] 🌐 Mengaktifkan Mode Hybrid: HTTP Login & Web Auth via SOCKS5 Proxy, Game via Direct UDP.");
                connect_host = game_host;
                connect_port = game_port;
            } else {
                Logger::log("[BOT #", id_, "] ❌ Gagal mengaktifkan SOCKS5 UDP Tunnel: ", terr);
                {
                    std::lock_guard<std::mutex> lock(status_mtx_);
                    status_string_ = "Proxy Error";
                }
                reconnect_ctrl_.onDisconnected("Gagal SOCKS5 UDP tunnel: " + terr);
                return false;
            }
        }
    } else {
        if (proxy_tunnel_) {
            proxy_tunnel_->stop();
            proxy_tunnel_.reset();
        }
    }

    if (enet_->connect(connect_host, connect_port, 5000)) {
        addConsoleMessage(ConsoleMsgType::System, "Located server, connecting...");
        Logger::log("[BOT #", id_, "] ✅ ENet terhubung ke ", game_host, ":", game_port,
                    (proxy_.isValid() ? (" via " + proxy_.toString()) : " (Direct)"));
        return true;
    } else {
        addConsoleMessage(ConsoleMsgType::System, "Failed to connect to server.");
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
    bool was_active = active_.exchange(false);
    bool was_in_world = in_world_.exchange(false);
    if (autofarm_) autofarm_->stop();
    if (lua_engine_) lua_engine_->stopScript();
    if (enet_) enet_->disconnect();
    if (proxy_tunnel_) {
        proxy_tunnel_->stop();
    }

    bool was_offline = false;
    {
        std::lock_guard<std::mutex> lock(status_mtx_);
        was_offline = (status_string_ == "Offline");
        status_string_ = "Offline";
    }

    if (was_active || was_in_world || !was_offline) {
        Logger::log("[BOT #", id_, " | ", account_name_, "] 🛑 Bot dihentikan.");
    }
    stats_.stopSession();
    stats_.saveToFile(account_name_);
}

void BotInstance::doReconnect() {
    if (!active_ || redirecting_.load()) return;

    Logger::log("[BOT #", id_, " | ", account_name_, "] 🔄 Menjalankan Auto-Reconnect...");
    addConsoleMessage(ConsoleMsgType::System, "Reconnecting...");
    enet_->disconnect();
    world_->clearWorld();
    in_world_ = false;
    handshake_sent_ = false;
    auto_warped_ = false;
    warped_to_target_ = false;

    if (!login_url.empty() && login_url.find("growtopia") != std::string::npos) {
        try {
            HttpClient http_retry(login_url, use_https, proxy_);
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

    std::string connect_host = game_host;
    int connect_port = game_port;

    if (proxy_.isValid()) {
        if (!proxy_tunnel_) proxy_tunnel_ = std::make_unique<Socks5UdpTunnel>();
        std::string terr;
        if (proxy_tunnel_->start(proxy_, game_host, static_cast<uint16_t>(game_port), terr)) {
            connect_host = proxy_tunnel_->getLocalHost();
            connect_port = proxy_tunnel_->getLocalPort();
        } else {
            Logger::log("[BOT #", id_, "] ⚠️ Reconnect SOCKS5 UDP Tunnel tidak aktif: ", terr);
            if (proxy_.udp_fallback_direct) {
                Logger::log("[BOT #", id_, "] 🌐 Menggunakan Mode Hybrid (Game via Direct UDP)...");
                connect_host = game_host;
                connect_port = game_port;
            } else {
                reconnect_ctrl_.onDisconnected("Gagal SOCKS5 UDP tunnel: " + terr);
                return;
            }
        }
    }

    if (enet_->connect(connect_host, connect_port, 5000)) {
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

    // Auto-save statistik (played time & status) berkala
    auto now_stats = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now_stats - last_stats_save_).count() >= 15) {
        last_stats_save_ = now_stats;
        stats_.saveToFile(account_name_);
    }
}

void BotInstance::setupPacketHandlers() {
    auto warp_to_target = [this]() {
        if (!target_world_.empty() && !in_world_.load() && !warped_to_target_.exchange(true)) {
            Logger::log("[BOT #", id_, "] 🚀 Mengirim warp ke target world: ", target_world_);
            actions_->warp(target_world_);
        }
    };

    auto trigger_enter = [this]() {
        if (!auto_warped_.exchange(true)) {
            Logger::log("[BOT #", id_, "] ✅ Login terautentikasi! Mengirim enter_game...");
            enet_->sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, "action|enter_game\n");
            enet_->sendText(GtPacket::NET_MESSAGE_GAME_MESSAGE, "action|enter_game\n");

            if (!target_world_.empty()) {
                std::thread([this]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
                    if (!in_world_.load()) {
                        actions_->warp(target_world_);
                    }
                }).detach();
            }
        }
    };

    enet_->onDisconnect([this]() {
        stats_.stopSession();
        stats_.saveToFile(account_name_);
        addConsoleMessage(ConsoleMsgType::System, "Disconnected from server.");
        if (active_ && !redirecting_.load()) {
            in_world_ = false;
            reconnect_ctrl_.onDisconnected("Koneksi ENet terputus");
            Logger::log("[BOT #", id_, "] 🔌 Terputus dari server. Auto-reconnect aktif.");
        }
    });

    enet_->onPacket([this, warp_to_target, trigger_enter](const GtPacketData& pkt) {
        if (pkt.type == GtPacket::NET_MESSAGE_SERVER_HELLO) {
            if (!handshake_sent_.exchange(true)) {
                addConsoleMessage(ConsoleMsgType::System, "Logging on " + account_name_ + "...");
                Logger::log("[BOT #", id_, "] SERVER_HELLO diterima, mengirim handshake login...");
                enet_->sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, current_handshake_);
            }
        }
        else if (pkt.type == GtPacket::NET_MESSAGE_GAME_MESSAGE ||
                 pkt.type == GtPacket::NET_MESSAGE_GENERIC_TEXT) {
            std::string text((const char*)pkt.payload.data(), pkt.payload.size());
            while (!text.empty() && text.back() == '\0') text.pop_back();

            std::string clean = Logger::stripColorCodes(text);
            if (stats_.parseFromText(clean)) {
                stats_.saveToFile(account_name_);
            }

            if (text.find("action|play_sfx") == std::string::npos &&
                text.find("action|ping") == std::string::npos &&
                text.find("add_label") == std::string::npos &&
                text.find("add_button") == std::string::npos &&
                text.find("add_spacer") == std::string::npos) {
                while (!clean.empty() && (clean.back() == '\r' || clean.back() == '\n' || clean.back() == ' ')) clean.pop_back();
                if (!clean.empty() && clean.size() < 120) {
                    Logger::log("[BOT #", id_, "] 💬 ", clean);
                }
            }

            if (text.find("Entering game") != std::string::npos ||
                text.find("Logon authenticated") != std::string::npos) {
                trigger_enter();
            }
            if (text.find("action|ping") != std::string::npos || text.find("ping\n") == 0) {
                enet_->sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, "action|pong\n");
            }
        }
        else if (pkt.type == GtPacket::NET_MESSAGE_GAME_PACKET) {
            if (pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                GameUpdatePacket tank_update{};
                std::memcpy(&tank_update, pkt.payload.data(), sizeof(GameUpdatePacket));
                lua_engine_->triggerOnProcessTankUpdate(tank_update);
            }
            uint8_t tank_type = pkt.payload.size() > 0 ? pkt.payload[0] : 0;

            // Tank 0: Posisi
            if (tank_type == PACKET_STATE && pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                GameUpdatePacket state_pkt;
                std::memcpy(&state_pkt, pkt.payload.data(), sizeof(GameUpdatePacket));
                world_->updatePlayerPos(state_pkt.net_id, state_pkt.pos_x, state_pkt.pos_y);
            }
            // Tank 4: Map Data
            else if (tank_type == PACKET_SEND_MAP_DATA) {
                in_world_ = true;
                reconnect_ctrl_.onConnected();
                stats_.startSession();
                if (pkt.payload.size() > sizeof(GameUpdatePacket)) {
                    world_->parseMapData(pkt.payload.data() + sizeof(GameUpdatePacket),
                                         pkt.payload.size() - sizeof(GameUpdatePacket));
                }
                world_->setWorldName(target_world_);
                Logger::log("[BOT #", id_, "] 🌍 Map data diterima! Bot berada di world: ", target_world_);
            }
            // Tank 8: Dropped Item
            else if (tank_type == PACKET_ITEM_CHANGE_OBJECT && pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                GameUpdatePacket item_pkt;
                std::memcpy(&item_pkt, pkt.payload.data(), sizeof(GameUpdatePacket));
                world_->addOrUpdateDroppedItem(item_pkt.net_id, static_cast<uint16_t>(item_pkt.main_data),
                                              item_pkt.count1, item_pkt.pos_x, item_pkt.pos_y);
            }
            // Tank 9: Inventory
            else if (tank_type == PACKET_SEND_INVENTORY_STATE) {
                if (pkt.payload.size() > sizeof(GameUpdatePacket)) {
                    world_->parseInventory(pkt.payload.data() + sizeof(GameUpdatePacket),
                                          pkt.payload.size() - sizeof(GameUpdatePacket));
                }
                int inv_gems = world_->getItemCount(112);
                if (inv_gems > 0) {
                    stats_.gems = inv_gems;
                    stats_.saveToFile(account_name_);
                }
                Logger::log("[BOT #", id_, "] 🎒 Inventory dimuat.");
                warp_to_target();
            }
            // Tank 1: Call Function
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

                    auto get_vint = [](const Variant& v) -> int {
                        if (v.type == 9) return v.int_val;
                        if (v.type == 5) return (int)v.uint_val;
                        return v.int_val != 0 ? v.int_val : (int)v.uint_val;
                    };

                    if (fn.find("OnSuperMainStartAcceptLogon") != std::string::npos) {
                        trigger_enter();
                    }
                    else if (fn.find("SetHasGrowID") != std::string::npos ||
                             fn.find("OnRequestWorldSelectMenu") != std::string::npos) {
                        warp_to_target();
                    }
                    else if (fn.find("OnSpawn") != std::string::npos) {
                        in_world_ = true;
                        reconnect_ctrl_.onConnected();
                        stats_.startSession();
                        std::string spawn_data = vars.size() > 1 ? vars[1].str_val : "";
                        world_->parseSpawn(spawn_data, account_name_);
                        int lvl = world_->getLocalLevel();
                        if (lvl > 0 && lvl < 250) {
                            stats_.level = lvl;
                        }
                        Logger::log("[BOT #", id_, "] 🎉 AKUN ONLINE di world! (OnSpawn)");

                        // Otomatis sinkronisasi profil diri sendiri seperti Lucifer bot asli
                        std::thread([this]() {
                            // Delay 2.5s setelah spawn sebelum wrench agar server stabil dan tidak memicu deteksi spam/macro
                            std::this_thread::sleep_for(std::chrono::milliseconds(2500));
                            if (!in_world_.load() || !enet_ || !enet_->isConnected()) return;
                            int my_netid = world_->getLocalNetId();
                            if (my_netid >= 0) {
                                // 1. Wrench diri sendiri untuk meminta dialog profil (1 paket GENERIC_TEXT saja)
                                enet_->sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT,
                                    "action|wrench\nnetid|" + std::to_string(my_netid) + "\n");
                                Logger::log("[BOT #", id_, "] 🔧 Mengirim wrench profil diri sendiri (delay 2.5s pasca-spawn)...");
                            }
                            // 2. Beri jeda 4 detik. HANYA jika stats profil belum terisi sama sekali, baru coba fallback /status
                            std::this_thread::sleep_for(std::chrono::milliseconds(4000));
                            if (!in_world_.load() || !enet_ || !enet_->isConnected()) return;
                            if (stats_.level <= 1 && stats_.played_hours <= 0.0f) {
                                enet_->sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT,
                                    "action|input\n|text|/status\n");
                                Logger::log("[BOT #", id_, "] 🔄 Mengirim /status fallback (stats profil belum terisi)...");
                            }
                        }).detach();
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
                        Logger::log("[BOT #", id_, "] ⚠️ Gagal masuk world (mencoba ulang dalam 2s)...");
                        std::thread([this, warp_to_target]() {
                            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                            if (!in_world_.load()) {
                                warped_to_target_ = false;
                                warp_to_target();
                            }
                        }).detach();
                    }
                    else if (fn.find("OnSetBumperCoinValues") != std::string::npos) {
                        if (vars.size() > 1) {
                            stats_.gems = get_vint(vars[1]);
                            if (vars.size() > 2) {
                                int val2 = get_vint(vars[2]);
                                if (val2 > 0 && val2 < 250) {
                                    stats_.level = val2;
                                }
                            }
                            stats_.saveToFile(account_name_);
                            Logger::log("[BOT #", id_, "] 💎 Stats diperbarui (OnSetBumperCoinValues): Gems=", stats_.gems, ", Level=", stats_.level);
                        }
                    }
                    else if (fn.find("OnSetGems") != std::string::npos || fn.find("SetGems") != std::string::npos) {
                        if (vars.size() > 1) {
                            stats_.gems = get_vint(vars[1]);
                            stats_.saveToFile(account_name_);
                            Logger::log("[BOT #", id_, "] 💎 Stats Gems diperbarui: ", stats_.gems);
                        }
                    }
                    else if (fn.find("OnSetLevel") != std::string::npos || fn.find("SetLevel") != std::string::npos) {
                        if (vars.size() > 1) {
                            int lvl = get_vint(vars[1]);
                            if (lvl > 0 && lvl < 250) {
                                stats_.level = lvl;
                                stats_.saveToFile(account_name_);
                                Logger::log("[BOT #", id_, "] ⭐ Stats Level diperbarui: ", stats_.level);
                            }
                        }
                    }
                    else if (fn.find("OnDialogRequest") != std::string::npos) {
                        if (vars.size() > 1) {
                            std::string dmsg = vars[1].str_val;
                            setLastDialog(dmsg);
                            std::string clean_dmsg = Logger::stripColorCodes(dmsg);
                            if (stats_.parseFromText(dmsg)) {
                                stats_.saveToFile(account_name_);
                                Logger::log("[BOT #", id_, "] 📊 Stats diperbarui dari OnDialogRequest: Level=", stats_.level,
                                            ", Gems=", stats_.gems, ", Played=", stats_.getEffectivePlayedHours(),
                                            "h, Age=", stats_.getEffectiveAgeDays(), "d");
                            }

                            // Jika popup profil memiliki tombol 'personal_info', otomatis balas untuk membuka info detail
                            if (dmsg.find("personal_info") != std::string::npos ||
                                dmsg.find("Show Personal") != std::string::npos) {
                                std::string dname = "popup";
                                size_t epos = dmsg.find("end_dialog|");
                                if (epos != std::string::npos) {
                                    size_t dname_start = epos + 11;
                                    size_t pipe = dmsg.find('|', dname_start);
                                    if (pipe != std::string::npos) {
                                        dname = dmsg.substr(dname_start, pipe - dname_start);
                                    }
                                }
                                // Beri jeda 600ms (reaksi klik tombol manusia) dan kirim HANYA 1 paket dialog_return
                                std::thread([this, dname]() {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(600));
                                    if (enet_ && enet_->isConnected()) {
                                        enet_->sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT,
                                            "action|dialog_return\ndialog_name|" + dname + "\nbuttonClicked|personal_info\n");
                                    }
                                }).detach();
                            } else if (dmsg.find("Total time played") != std::string::npos ||
                                       dmsg.find("created") != std::string::npos) {
                                // Dialog detail profil (Continue) telah terbaca, tutup dialog secara wajar
                                std::string dname = "popup";
                                size_t epos = dmsg.find("end_dialog|");
                                if (epos != std::string::npos) {
                                    size_t dname_start = epos + 11;
                                    size_t pipe = dmsg.find('|', dname_start);
                                    if (pipe != std::string::npos) {
                                        dname = dmsg.substr(dname_start, pipe - dname_start);
                                    }
                                }
                                std::thread([this, dname]() {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(800));
                                    if (enet_ && enet_->isConnected()) {
                                        enet_->sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT,
                                            "action|dialog_return\ndialog_name|" + dname + "\nbuttonClicked|\n");
                                    }
                                }).detach();
                            }
                        }
                    }
                    else if (fn.find("OnConsoleMessage") != std::string::npos) {
                        if (vars.size() > 1) {
                            std::string cmsg = vars[1].str_val;
                            std::string clean = Logger::stripColorCodes(cmsg);
                            if (stats_.parseFromText(clean)) {
                                stats_.saveToFile(account_name_);
                                Logger::log("[BOT #", id_, "] 📊 Stats diperbarui dari OnConsoleMessage: Level=", stats_.level,
                                            ", Gems=", stats_.gems, ", Age=", stats_.getEffectiveAgeDays(), "d");
                            }
                            Logger::log("[BOT #", id_, "] 💬 Console: ", clean);
                            if (clean.find("IP") != std::string::npos || clean.find("ip") != std::string::npos ||
                                clean.find("Logged in") != std::string::npos) {
                                std::string ip = extractIpFromText(clean);
                                if (!ip.empty()) {
                                    server_reported_ip_ = ip;
                                    Logger::log("[BOT #", id_, "] 🌐 IP terkonfirmasi dari OnConsoleMessage: ", ip);
                                }
                            }

                            ConsoleMsgType mtype = ConsoleMsgType::System;
                            if (clean.find("[Welcome to Growtopia") != std::string::npos ||
                                (clean.rfind("[", 0) == 0 && clean.find("]") != std::string::npos &&
                                 (clean.find("Lock") != std::string::npos || clean.find("Level") != std::string::npos || clean.find("Quest") != std::string::npos))) {
                                mtype = ConsoleMsgType::Task;
                            } else if (clean.find("CP:") != std::string::npos || clean.find("whisper") != std::string::npos ||
                                       clean.find("from `") != std::string::npos || clean.find("to `") != std::string::npos) {
                                mtype = ConsoleMsgType::Private;
                            } else if (clean.find("CP:_CT:[W]") != std::string::npos) {
                                mtype = ConsoleMsgType::World;
                            }
                            addConsoleMessage(mtype, cmsg);
                        }
                    }
                    else if (fn.find("OnTalkBubble") != std::string::npos) {
                        int bubble_netid = vars.size() > 1 ? get_vint(vars[1]) : -1;
                        std::string bubble_msg = vars.size() > 2 ? vars[2].str_val : "";
                        if (stats_.parseFromText(bubble_msg)) {
                            stats_.saveToFile(account_name_);
                        }
                        Logger::log("[BOT #", id_, "] 💬 [Bubble #", bubble_netid, "]: ", bubble_msg);
                        if (!bubble_msg.empty()) {
                            addConsoleMessage(ConsoleMsgType::World, bubble_msg);
                        }
                    }
                    else if (fn.find("OnSendToServer") != std::string::npos) {
                        int sub_port = vars.size() > 1 ? get_vint(vars[1]) : game_port;
                        int token = vars.size() > 2 ? get_vint(vars[2]) : 0;
                        int userId = vars.size() > 3 ? get_vint(vars[3]) : 0;
                        std::string ipDoor = vars.size() > 4 ? vars[4].str_val : "";
                        int lmode = vars.size() > 5 ? get_vint(vars[5]) : 1;

                        Logger::log("[BOT #", id_, "] 🔁 Redirect sub-server: Port ", sub_port, " (Token: ", token, ")");

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

                            std::string sub_connect_host = sub_host;
                            int sub_connect_port = sub_port;

                            if (proxy_.isValid()) {
                                if (!proxy_tunnel_) proxy_tunnel_ = std::make_unique<Socks5UdpTunnel>();
                                std::string terr;
                                if (proxy_tunnel_->start(proxy_, sub_host, static_cast<uint16_t>(sub_port), terr)) {
                                    sub_connect_host = proxy_tunnel_->getLocalHost();
                                    sub_connect_port = proxy_tunnel_->getLocalPort();
                                    Logger::log("[BOT #", id_, "] 🔄 SOCKS5 UDP Tunnel dialihkan ke sub-server ", sub_host, ":", sub_port);
                                } else {
                                    Logger::log("[BOT #", id_, "] ❌ Gagal redirect SOCKS5 UDP Tunnel: ", terr);
                                }
                            }

                            if (enet_->connect(sub_connect_host, sub_connect_port, 6000)) {
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
    addConsoleMessage(ConsoleMsgType::World, "<" + account_name_ + "> " + text);
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
    if (proxy_.isValid() && proxy_tunnel_ && proxy_tunnel_->isProxyDisconnected()) {
        return "Proxy DC (" + status_string_ + ")";
    }
    return status_string_;
}

bool BotInstance::isConnected() const {
    return enet_ && enet_->isConnected();
}

int BotInstance::getPing() const {
    return enet_ ? enet_->getPing() : 0;
}

void BotInstance::queryServerStats() {
    if (!enet_ || !enet_->isConnected()) return;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stats_query_).count();
    if (elapsed < 3000) {
        Logger::log("[BOT #", id_, "] ⏳ Mohon tunggu ", (3000 - elapsed), "ms sebelum meminta sinkron stats lagi (Anti-Spam).");
        return;
    }
    last_stats_query_ = now;

    // Kirim hanya 1 command dengan delay 400ms di thread terpisah agar tidak spam
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        if (enet_ && enet_->isConnected()) {
            enet_->sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, "action|input\n|text|/status\n");
            Logger::log("[BOT #", id_, "] 🔄 Mengirim permintaan sinkronisasi stats (/status)...");
        }
    }).detach();
}
