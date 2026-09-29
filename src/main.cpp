#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <cstring>

#include <nlohmann/json.hpp>
#include "logger.hpp"
#include "http_client.hpp"
#include "enet_client.hpp"
#include "packet.hpp"
#include "game_packet.hpp"
#include "world.hpp"
#include "bot_actions.hpp"
#include "cli.hpp"
#include "env.hpp"
#include "gui.hpp"

using json = nlohmann::json;

static std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "========================================\n";
    std::cout << "  GTPS Lucifer Bot - Core Suite & CLI\n";
    std::cout << "========================================\n\n";

    // ---- Load config (.env prioritized, fallback to JSON) ----
    std::string cfg_path = (argc > 1) ? argv[1] : "";
    std::string login_url  = "http://127.0.0.1/growtopia/server_data.php";
    bool        use_https  = false;
    std::string game_host  = "127.0.0.1";
    int         game_port  = 17091;
    std::string acc_name   = "";
    std::string acc_pass   = "";
    std::string version    = "5.58";
    std::string platform   = "windows";
    int         protocol   = 225;
    std::string target_world = "riziq";

    bool loaded = false;

    // 1. Coba baca .env (jika tidak ada argumen atau argumen .env)
    std::string env_file = cfg_path.empty() ? ".env" : cfg_path;
    if (env_file.find(".json") == std::string::npos) {
        auto env = EnvConfig::load(env_file);
        if (!env.empty()) {
            login_url   = EnvConfig::get(env, "GTPS_LOGIN_URL", login_url);
            use_https   = EnvConfig::getBool(env, "GTPS_USE_HTTPS", use_https);
            game_host   = EnvConfig::get(env, "GTPS_GAME_SERVER", game_host);
            game_port   = EnvConfig::getInt(env, "GTPS_GAME_PORT", game_port);
            acc_name    = EnvConfig::get(env, "ACCOUNT_NAME", acc_name);
            acc_pass    = EnvConfig::get(env, "ACCOUNT_PASSWORD", acc_pass);
            version     = EnvConfig::get(env, "CLIENT_VERSION", version);
            platform    = EnvConfig::get(env, "CLIENT_PLATFORM", platform);
            protocol    = EnvConfig::getInt(env, "CLIENT_PROTOCOL", protocol);
            target_world = EnvConfig::get(env, "TARGET_WORLD", target_world);
            Logger::verbose = EnvConfig::getBool(env, "LOG_VERBOSE", true);
            Logger::hexdump = EnvConfig::getBool(env, "LOG_HEXDUMP", false);
            loaded = true;
            Logger::log("[*] Konfigurasi dimuat dari: ", env_file);
        }
    }

    // 2. Fallback baca JSON jika .env belum dimuat atau argumen .json
    if (!loaded) {
        std::string json_file = cfg_path.empty() ? "config.json" : cfg_path;
        try {
            std::ifstream f(json_file);
            if (f.is_open()) {
                json cfg = json::parse(f);
                if (cfg.contains("logging")) {
                    Logger::verbose = cfg["logging"].value("verbose", true);
                    Logger::hexdump = cfg["logging"].value("hexdump", false);
                }
                login_url  = cfg["gtps"].value("login_url", login_url);
                use_https  = cfg["gtps"].value("use_https", use_https);
                game_host  = cfg["gtps"].value("game_server", game_host);
                game_port  = cfg["gtps"].value("game_port", game_port);
                acc_name   = cfg["account"].value("name", acc_name);
                acc_pass   = cfg["account"].value("password", acc_pass);
                version    = cfg["client"].value("version", version);
                platform   = cfg["client"].value("platform", platform);
                protocol   = cfg["client"].value("protocol", protocol);
                target_world = cfg["client"].value("target_world", target_world);
                loaded = true;
                Logger::log("[*] Konfigurasi dimuat dari: ", json_file);
            }
        } catch (...) {}
    }

    if (!loaded) {
        Logger::log("[!] File konfigurasi (.env atau config.json) tidak ditemukan!");
        Logger::log("    Salin .env.example menjadi .env lalu isi data akun.");
        return 1;
    }

    if (acc_name.empty()) {
        Logger::log("[!] Isi dulu ACCOUNT_NAME di .env");
        return 1;
    }

    Logger::log("[*] Konfigurasi:");
    Logger::log("    Login URL    : ", login_url);
    Logger::log("    Game         : ", game_host, ":", game_port);
    Logger::log("    Akun         : ", acc_name);
    Logger::log("    Target World : ", target_world.empty() ? "(none)" : target_world);

    // ---- Fase 1a: HTTP Login ----
    Logger::log("\n--- FASE 1a: HTTP Login ---");

    HttpClient http(login_url, use_https);
    LoginResponse login_resp = http.login(acc_name, acc_pass,
                                          version, platform);

    if (!login_resp.success) {
        Logger::log("[!] Login HTTP GAGAL");
        Logger::log("    Error: ", login_resp.error);
        return 1;
    }

    Logger::log("[+] HTTP login OK");

    if (!login_resp.server_ip.empty()) {
        game_host = login_resp.server_ip;
    }
    if (login_resp.server_port > 0) {
        game_port = login_resp.server_port;
    }

    // ---- Fase 1b & 2: ENet Connect & Lucifer State Engine ----
    Logger::log("\n--- FASE 1b & 2: ENet Connect & Lucifer State Engine ---");

    try {
        EnetClient enet;
        WorldState world;
        BotActions actions(enet, world);
        AutoFarmController autofarm(actions, world);

        std::string handshake;
        if (!acc_name.empty() && !acc_pass.empty()) {
            handshake += "tankIDName|" + acc_name + "\n";
            handshake += "tankIDPass|" + acc_pass + "\n";
            handshake += "f|1\n";
        } else {
            handshake += "f|0\n";
        }
        handshake += "requestedName|" + (acc_name.empty() ? "GuestBot" : acc_name) + "\n";
        handshake += "protocol|" + std::to_string(protocol) + "\n";
        handshake += "game_version|" + version + "\n";
        handshake += "cbits|128\n";
        handshake += "player_age|20\n";
        handshake += "GDPR|1\n";
        handshake += "hash2|-129482910\n";
        if (!login_resp.meta.empty()) {
            handshake += "meta|" + login_resp.meta + "\n";
        }
        handshake += "fhash|-716928004\n";
        handshake += "platformID|0\n";
        handshake += "deviceVersion|0\n";
        handshake += "country|id\n";
        handshake += "hash|-16777216\n";
        handshake += "mac|38:7a:0e:b9:32:87\n";
        handshake += "rid|02350719000D94880AAD1618027D798B\n";
        handshake += "wk|7F9E8284754593C0E51D4B5391629856\n";
        handshake += "fz|1331849031\n";
        handshake += "zf|-1331849031\n";

        std::string handshake_login = handshake;
        std::string current_handshake = handshake_login;
        std::atomic<bool> handshake_sent{false};
        std::atomic<bool> auto_warped{false};
        std::atomic<bool> warped_to_target{false};
        std::atomic<bool> redirecting{false};
        std::atomic<bool> in_world{false};

        auto warp_to_target_world = [&]() {
            if (!target_world.empty() && !in_world.load() && !warped_to_target.exchange(true)) {
                Logger::log("[*] 🚀 Mengirim request warp ke world: ", target_world, "...");
                actions.warp(target_world);
            }
        };

        auto trigger_enter_game = [&]() {
            if (!auto_warped.exchange(true)) {
                Logger::log("[*] ✅ Login sukses terautentikasi! Mengirim action|enter_game...");
                enet.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, "action|enter_game\n");
                enet.sendText(GtPacket::NET_MESSAGE_GAME_MESSAGE, "action|enter_game\n");

                if (!target_world.empty()) {
                    std::thread([&warp_to_target_world, &in_world]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
                        if (!in_world.load()) {
                            warp_to_target_world();
                        }
                    }).detach();
                }
            }
        };

        // Callback untuk paket masuk dari server
        enet.onPacket([&](const GtPacketData& pkt) {
            if (pkt.type == GtPacket::NET_MESSAGE_SERVER_HELLO) {
                Logger::log("  👋 SERVER_HELLO diterima dari server");
                if (!handshake_sent.exchange(true)) {
                    Logger::log("[*] Mengirim paket handshake login setelah SERVER_HELLO...");
                    enet.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, current_handshake);
                }
            }
            else if (pkt.type == GtPacket::NET_MESSAGE_GAME_MESSAGE ||
                     pkt.type == GtPacket::NET_MESSAGE_GENERIC_TEXT) {
                std::string text((const char*)pkt.payload.data(), pkt.payload.size());
                while (!text.empty() && text.back() == '\0') text.pop_back();
                Logger::log("  💬 Server Text: ", text);

                if (text.find("Entering game") != std::string::npos ||
                    text.find("Logon authenticated") != std::string::npos) {
                    trigger_enter_game();
                }
            }
            else if (pkt.type == GtPacket::NET_MESSAGE_GAME_PACKET) {
                uint8_t tank_type = pkt.payload.size() > 0 ? pkt.payload[0] : 0;

                // Tank packet 0: PACKET_STATE (Player Movement / Pos)
                if (tank_type == PACKET_STATE && pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                    GameUpdatePacket state_pkt;
                    std::memcpy(&state_pkt, pkt.payload.data(), sizeof(GameUpdatePacket));
                    world.updatePlayerPos(state_pkt.net_id, state_pkt.pos_x, state_pkt.pos_y);
                }
                // Tank packet 4: PACKET_SEND_MAP_DATA (World Map)
                else if (tank_type == PACKET_SEND_MAP_DATA) {
                    in_world = true;
                    if (pkt.payload.size() > sizeof(GameUpdatePacket)) {
                        world.parseMapData(pkt.payload.data() + sizeof(GameUpdatePacket),
                                           pkt.payload.size() - sizeof(GameUpdatePacket));
                    }
                    world.setWorldName(target_world);
                    Logger::log("\n************************************************************");
                    Logger::log("  🎉 AKUN ONLINE! PACKET_SEND_MAP_DATA diterima dari server!");
                    Logger::log("  🌍 Bot sudah berhasil masuk dan berada di dalam world: ", target_world);
                    Logger::log("************************************************************\n");
                }
                // Tank packet 8: PACKET_ITEM_CHANGE_OBJECT (Dropped item spawned/collected)
                else if (tank_type == PACKET_ITEM_CHANGE_OBJECT && pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                    GameUpdatePacket item_pkt;
                    std::memcpy(&item_pkt, pkt.payload.data(), sizeof(GameUpdatePacket));
                    world.addOrUpdateDroppedItem(item_pkt.net_id, static_cast<uint16_t>(item_pkt.main_data),
                                                 item_pkt.count1, item_pkt.pos_x, item_pkt.pos_y);
                }
                // Tank packet 9: PACKET_SEND_INVENTORY_STATE
                else if (tank_type == PACKET_SEND_INVENTORY_STATE) {
                    if (pkt.payload.size() > sizeof(GameUpdatePacket)) {
                        world.parseInventory(pkt.payload.data() + sizeof(GameUpdatePacket),
                                             pkt.payload.size() - sizeof(GameUpdatePacket));
                    }
                    Logger::log("[+] 🎒 Inventory state diterima! Pemain siap di server.");
                    warp_to_target_world();
                }
                // Tank packet 1: PACKET_CALL_FUNCTION (VariantList)
                else if (tank_type == PACKET_CALL_FUNCTION && pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                    auto vars = parseVariantList(pkt.payload.data() + sizeof(GameUpdatePacket),
                                                 pkt.payload.size() - sizeof(GameUpdatePacket));
                    if (!vars.empty()) {
                        std::string fn = vars[0].str_val;

                        auto get_vint = [](const Variant& v) -> int {
                            if (v.type == 9) return v.int_val;
                            if (v.type == 5) return (int)v.uint_val;
                            return v.int_val != 0 ? v.int_val : (int)v.uint_val;
                        };

                        if (fn.find("OnSuperMainStartAcceptLogon") != std::string::npos) {
                            Logger::log("  🌟 OnSuperMainStartAcceptLogon diterima dari server: ", fn);
                            trigger_enter_game();
                        }
                        else if (fn.find("SetHasGrowID") != std::string::npos) {
                            Logger::log("  🔑 SetHasGrowID diterima");
                            warp_to_target_world();
                        }
                        else if (fn.find("OnRequestWorldSelectMenu") != std::string::npos) {
                            Logger::log("  📋 OnRequestWorldSelectMenu diterima (World Select Menu)");
                            warp_to_target_world();
                        }
                        else if (fn.find("OnSpawn") != std::string::npos) {
                            in_world = true;
                            std::string spawn_data = vars.size() > 1 ? vars[1].str_val : "";
                            world.parseSpawn(spawn_data, acc_name);
                            Logger::log("[+] 👤 OnSpawn diterima! Karakter terdaftar di world.");
                            Logger::log("[+] ✅ Akun sekarang ONLINE di dalam game!");
                        }
                        else if (fn.find("OnRemove") != std::string::npos) {
                            std::string remove_data = vars.size() > 1 ? vars[1].str_val : "";
                            world.parseRemove(remove_data);
                        }
                        else if (fn.find("OnTalkBubble") != std::string::npos) {
                            int bubble_netid = vars.size() > 1 ? get_vint(vars[1]) : -1;
                            std::string bubble_msg = vars.size() > 2 ? vars[2].str_val : "";
                            Logger::log("  💬 [Bubble NetID #", bubble_netid, "]: ", bubble_msg);
                        }
                        else if (fn.find("OnFailedToEnterWorld") != std::string::npos) {
                            Logger::log("[!] Gagal masuk world (rate limited / busy), retry dalam 2 detik...");
                            std::thread([&warp_to_target_world, &warped_to_target, &in_world]() {
                                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                                if (!in_world.load()) {
                                    warped_to_target = false;
                                    warp_to_target_world();
                                }
                            }).detach();
                        }
                        else if (fn.find("OnConsoleMessage") != std::string::npos) {
                            if (vars.size() > 1) {
                                Logger::log("  💬 Console: ", vars[1].str_val);
                            }
                        }
                        else if (fn.find("OnSendToServer") != std::string::npos) {
                            int sub_port = vars.size() > 1 ? get_vint(vars[1]) : game_port;
                            int token = vars.size() > 2 ? get_vint(vars[2]) : 0;
                            int userId = vars.size() > 3 ? get_vint(vars[3]) : 0;
                            std::string ipDoor = vars.size() > 4 ? vars[4].str_val : "";
                            int lmode = vars.size() > 5 ? get_vint(vars[5]) : 1;

                            Logger::log("[*] 🔁 OnSendToServer diterima! Redirecting ke sub-server:");
                            Logger::log("    Port   : ", sub_port);
                            Logger::log("    Token  : ", token);
                            Logger::log("    UserID : ", userId);
                            Logger::log("    IPDoor : ", ipDoor);
                            Logger::log("    LMode  : ", lmode);

                            // Parse IP & Door
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

                            // Buat handshake untuk sub-server
                            std::string sub_handshake;
                            sub_handshake += "tankIDName|" + acc_name + "\n";
                            sub_handshake += "tankIDPass|" + acc_pass + "\n";
                            sub_handshake += "requestedName|" + acc_name + "\n";
                            sub_handshake += "f|1\n";
                            sub_handshake += "protocol|" + std::to_string(protocol) + "\n";
                            sub_handshake += "game_version|" + version + "\n";
                            sub_handshake += "cbits|128\n";
                            sub_handshake += "player_age|20\n";
                            sub_handshake += "GDPR|1\n";
                            sub_handshake += "hash2|-129482910\n";
                            if (!login_resp.meta.empty()) {
                                sub_handshake += "meta|" + login_resp.meta + "\n";
                            }
                            sub_handshake += "fhash|-716928004\n";
                            sub_handshake += "platformID|0\n";
                            sub_handshake += "deviceVersion|0\n";
                            sub_handshake += "country|id\n";
                            sub_handshake += "hash|-16777216\n";
                            sub_handshake += "mac|38:7a:0e:b9:32:87\n";
                            sub_handshake += "rid|02350719000D94880AAD1618027D798B\n";
                            sub_handshake += "wk|7F9E8284754593C0E51D4B5391629856\n";
                            sub_handshake += "fz|1331849031\n";
                            sub_handshake += "zf|-1331849031\n";
                            sub_handshake += "user|" + std::to_string(userId) + "\n";
                            sub_handshake += "token|" + std::to_string(token) + "\n";
                            sub_handshake += "doorID|" + door_id + "\n";
                            sub_handshake += "lmode|" + std::to_string(lmode) + "\n";

                            // Hubungkan ke subserver pada background thread
                            std::thread([&enet, &world, sub_host, sub_port, sub_handshake, &current_handshake, &handshake_sent, &auto_warped, &warped_to_target, &in_world, &redirecting]() {
                                redirecting = true;
                                Logger::log("[*] 🔄 Mengalihkan koneksi ke sub-server ", sub_host, ":", sub_port, "...");
                                enet.disconnect();
                                std::this_thread::sleep_for(std::chrono::milliseconds(250));

                                world.clearWorld();
                                current_handshake = sub_handshake;
                                handshake_sent = false;
                                auto_warped = false;
                                warped_to_target = false;
                                in_world = false;

                                if (enet.connect(sub_host, sub_port, 6000)) {
                                    Logger::log("[+] ✅ Terhubung ke sub-server ", sub_host, ":", sub_port);
                                } else {
                                    Logger::log("[!] ❌ Gagal terhubung ke sub-server!");
                                }
                                redirecting = false;
                            }).detach();
                        }
                        else {
                            Logger::log("  ⚡ Variant Function: ", fn);
                        }
                    }
                }
            }
            else if (pkt.type == GtPacket::NET_MESSAGE_ERROR) {
                std::string err((const char*)pkt.payload.data(), pkt.payload.size());
                while (!err.empty() && err.back() == '\0') err.pop_back();
                Logger::log("  ❌ ERROR dari server: ", err);
            }
        });

        if (!enet.connect(game_host, game_port, 5000)) {
            Logger::log("[!] Gagal connect ENet ke ", game_host, ":", game_port);
            Logger::log("    Cek: server GTPS ENet jalan? port benar? firewall?");
            return 1;
        }

        BotInfo info{acc_name, version, platform, game_host, game_port};
        LuciferGui gui(enet, world, actions, autofarm, info, g_running);

        Logger::log("\n========================================================");
        Logger::log("  🔥 Membuka Antarmuka GUI Lucifer (Dear ImGui DX11)...");
        Logger::log("========================================================\n");

        if (gui.init(::GetModuleHandle(nullptr), 1080, 720)) {
            // Jalankan GUI Lucifer (Main render loop + ENet polling)
            gui.run();
            gui.shutdown();
        } else {
            Logger::log("[!] Gagal membuka jendela GUI DirectX 11, fallback ke CLI...");
            Cli cli(enet, g_running, info, world, actions, autofarm);
            cli.start();
            while (g_running && (enet.isConnected() || redirecting)) {
                enet.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            cli.stop();
        }

        autofarm.stop();
        Logger::log("[*] Menutup bot dan disconnect dari server.");
        enet.disconnect();

    } catch (const std::exception& e) {
        Logger::log("[!] Exception: ", e.what());
        return 1;
    }

    Logger::log("[*] Selesai.");
    return 0;
}