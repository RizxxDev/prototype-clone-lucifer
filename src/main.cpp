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
#include "lua_engine.hpp"
#include "anti_detection.hpp"
#include "reconnect_controller.hpp"
#include "socks5.hpp"
#include "bot_manager.hpp"
#include "item_database.hpp"
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

static BOOL CALLBACK TerminateOldInstanceWindowProc(HWND hwnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == static_cast<DWORD>(lParam)) {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

static void terminatePreviousInstances() {
    DWORD current_pid = GetCurrentProcessId();
    char current_path[MAX_PATH] = {0};
    if (GetModuleFileNameA(NULL, current_path, MAX_PATH) == 0) {
        return;
    }
    std::string current_exe_name = std::filesystem::path(current_path).filename().string();
    if (current_exe_name.empty()) return;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    std::vector<DWORD> pids_to_kill;

    if (Process32First(snapshot, &pe)) {
        do {
            if (pe.th32ProcessID != current_pid && pe.th32ProcessID != 0) {
                if (_stricmp(pe.szExeFile, current_exe_name.c_str()) == 0) {
                    pids_to_kill.push_back(pe.th32ProcessID);
                }
            }
        } while (Process32Next(snapshot, &pe));
    }
    CloseHandle(snapshot);

    for (DWORD pid : pids_to_kill) {
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
        if (hProc) {
            char other_path[MAX_PATH] = {0};
            DWORD size = MAX_PATH;
            bool is_same_app = true;
            if (QueryFullProcessImageNameA(hProc, 0, other_path, &size)) {
                if (_stricmp(other_path, current_path) != 0) {
                    is_same_app = false;
                }
            }

            if (is_same_app) {
                Logger::log("[SYSTEM] ⚠️ Menemukan instance lama berjalan (PID: ", pid, "). Menghentikan instance lama...");
                // Kirim pesan WM_CLOSE ke window GUI instance lama
                EnumWindows(TerminateOldInstanceWindowProc, static_cast<LPARAM>(pid));

                // Tunggu sejenak agar instance lama dapat shutdown secara wajar
                if (WaitForSingleObject(hProc, 400) != WAIT_OBJECT_0) {
                    // Paksa terminate jika instance lama tidak merespons (misal stuck / headless)
                    TerminateProcess(hProc, 0);
                    WaitForSingleObject(hProc, 1000);
                }
                Logger::log("[SYSTEM] ✅ Instance lama (PID: ", pid, ") berhasil di-terminate.");
            }
            CloseHandle(hProc);
        }
    }
}
#endif

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

    // ---- Check for --eval-lua cli argument ----
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--eval-lua" && i + 1 < argc) {
            std::string code = argv[i + 1];
            ItemDatabase::instance().loadFromFile("cache/items.dat");
            if (!ItemDatabase::instance().isLoaded()) {
                ItemDatabase::instance().loadFromFile("items.dat");
            }
            EnetClient dummy_enet;
            WorldState dummy_world;
            BotActions dummy_actions(dummy_enet, dummy_world);
            LuaEngine test_engine(dummy_actions, dummy_world, dummy_enet);
            std::cout << "[LUA TEST RUNNER] Menjalankan skrip...\n";
            test_engine.runScript(code);
            while (test_engine.isRunning()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            std::string err = test_engine.getLastError();
            if (!err.empty()) {
                std::cerr << "[LUA TEST ERROR] " << err << "\n";
                return 1;
            }
            std::cout << "[LUA TEST SUCCESS]\n";
            return 0;
        }
    }

    // ---- Auto-terminate previous running instance if started again ----
    bool allow_multiple = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--allow-multiple") {
            allow_multiple = true;
            break;
        }
    }

    if (!allow_multiple) {
#ifdef _WIN32
        terminatePreviousInstances();
#endif
    }

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
    std::string target_world = "";
    Socks5Config main_proxy{};

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

            std::string socks5_proxy_str = EnvConfig::get(env, "SOCKS5_PROXY", "");
            if (!socks5_proxy_str.empty()) {
                main_proxy = Socks5Config::parse(socks5_proxy_str);
            } else {
                main_proxy.enabled = EnvConfig::getBool(env, "SOCKS5_ENABLED", false);
                main_proxy.host = EnvConfig::get(env, "SOCKS5_HOST", "");
                main_proxy.port = static_cast<uint16_t>(EnvConfig::getInt(env, "SOCKS5_PORT", 1080));
                main_proxy.username = EnvConfig::get(env, "SOCKS5_USERNAME", "");
                main_proxy.password = EnvConfig::get(env, "SOCKS5_PASSWORD", "");
            }

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

                if (cfg.contains("proxy") || cfg.contains("socks5")) {
                    auto p = cfg.contains("proxy") ? cfg["proxy"] : cfg["socks5"];
                    if (p.is_string()) {
                        main_proxy = Socks5Config::parse(p.get<std::string>());
                    } else if (p.is_object()) {
                        main_proxy.enabled = p.value("enabled", false);
                        main_proxy.host = p.value("host", "");
                        main_proxy.port = static_cast<uint16_t>(p.value("port", 1080));
                        main_proxy.username = p.value("username", "");
                        main_proxy.password = p.value("password", "");
                    }
                }

                loaded = true;
                Logger::log("[*] Konfigurasi dimuat dari: ", json_file);
            }
        } catch (...) {}
    }

    if (!loaded) {
        Logger::log("[!] File konfigurasi (.env atau config.json) tidak ditemukan!");
        Logger::log("    Salin .env.example menjadi .env lalu isi data server.");
        return 1;
    }

    Logger::log("[*] Konfigurasi:");
    Logger::log("    Login URL    : ", login_url);
    Logger::log("    Game         : ", game_host, ":", game_port);
    Logger::log("    Akun Default : ", acc_name.empty() ? "(Belum diatur / Standalone Multi-Bot mode)" : acc_name);
    Logger::log("    Target World : ", target_world.empty() ? "(none)" : target_world);
    Logger::log("    Proxy SOCKS5 : ", main_proxy.isValid() ? main_proxy.toString() : "(Direct / Tanpa Proxy)");

    // ---- Fase 1a: HTTP Login ----
    LoginResponse login_resp{};
    if (!acc_name.empty()) {
        Logger::log("\n--- FASE 1a: HTTP Login ---");

        HttpClient http(login_url, use_https, main_proxy);
        login_resp = http.login(acc_name, acc_pass, version, platform);

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
    } else {
        Logger::log("\n[*] Tidak ada akun default di .env. Membuka GUI Lucifer Standalone...");
    }

    // ---- Inisialisasi Cache Directory & Items Database ----
    try {
        std::filesystem::create_directories("cache/fingerprints");
    } catch (...) {}
    ItemDatabase::instance().loadFromFile("cache/items.dat");
    if (!ItemDatabase::instance().isLoaded()) {
        ItemDatabase::instance().loadFromFile("items.dat");
    }

    // ---- Fase 1b & 2: ENet Connect & Lucifer State Engine ----
    Logger::log("\n--- FASE 1b & 2: ENet Connect & Lucifer State Engine ---");

    try {
        EnetClient enet;
        WorldState world;
        BotActions actions(enet, world);
        AutoFarmController autofarm(actions, world);
        LuaEngine lua_engine(actions, world, enet);

        // Anti-Detection & Device Fingerprint
        AntiDetection::init();
        DeviceFingerprint fingerprint = DeviceFingerprint::getOrCreateForAccount(acc_name.empty() ? "default" : acc_name);
        ReconnectController reconnect_ctrl(2000, 2.0f, 30000, 0.15f);
        std::string single_last_dialog;

        auto build_handshake = [&](const std::string& meta_str, int user_id = 0, int token = 0, const std::string& door_id = "", int lmode = -1) -> std::string {
            std::string hs;
            if (!acc_name.empty()) {
                hs += "tankIDName|" + acc_name + "\n";
                if (!acc_pass.empty()) {
                    hs += "tankIDPass|" + acc_pass + "\n";
                    hs += "f|1\n";
                } else if (!login_resp.token.empty()) {
                    hs += "ltoken|" + login_resp.token + "\n";
                    hs += "f|0\n";
                } else {
                    hs += "f|0\n";
                }
            } else {
                hs += "f|0\n";
            }
            hs += "requestedName|" + (acc_name.empty() ? "GuestBot" : acc_name) + "\n";
            hs += "protocol|" + std::to_string(protocol) + "\n";
            hs += "game_version|" + version + "\n";
            hs += "cbits|128\n";
            hs += "player_age|20\n";
            hs += "GDPR|1\n";
            hs += fingerprint.toHandshakeString(meta_str);
            if (user_id > 0) {
                hs += "user|" + std::to_string(user_id) + "\n";
            }
            if (token > 0) {
                hs += "token|" + std::to_string(token) + "\n";
            }
            if (!door_id.empty()) {
                hs += "doorID|" + door_id + "\n";
            }
            if (lmode >= 0) {
                hs += "lmode|" + std::to_string(lmode) + "\n";
            }
            return hs;
        };

        std::string current_handshake = build_handshake(login_resp.meta);
        std::atomic<bool> handshake_sent{false};
        std::atomic<bool> auto_warped{false};
        std::atomic<bool> warped_to_target{false};
        std::atomic<bool> redirecting{false};
        std::atomic<bool> in_world{false};

        std::unique_ptr<Socks5UdpTunnel> single_bot_tunnel;
        auto get_enet_target = [&](const std::string& target_host, int target_port, std::string& out_host, int& out_port) {
            if (main_proxy.isValid()) {
                if (!single_bot_tunnel) single_bot_tunnel = std::make_unique<Socks5UdpTunnel>();
                single_bot_tunnel->stop();
                std::string terr;
                if (single_bot_tunnel->start(main_proxy, target_host, static_cast<uint16_t>(target_port), terr)) {
                    out_host = single_bot_tunnel->getLocalHost();
                    out_port = single_bot_tunnel->getLocalPort();
                    Logger::log("[MAIN] ✅ SOCKS5 UDP Tunnel aktif (Full TCP+UDP): 127.0.0.1:", out_port, " -> ", target_host, ":", target_port);
                    return true;
                } else {
                    Logger::log("[MAIN] ⚠️ SOCKS5 UDP Tunnel tidak aktif: ", terr);
                    if (main_proxy.udp_fallback_direct) {
                        Logger::log("[MAIN] 🌐 Mengaktifkan Mode Hybrid: HTTP Login via SOCKS5 Proxy, Game via Direct UDP.");
                    }
                }
            }
            out_host = target_host;
            out_port = target_port;
            return true;
        };

        std::function<void()> do_reconnect = [&]() {
            if (acc_name.empty()) return;
            if (redirecting.load()) return;
            Logger::log("[*] 🔄 Menjalankan proses Auto-Reconnect...");
            enet.disconnect();
            world.clearWorld();
            in_world = false;
            handshake_sent = false;
            auto_warped = false;
            warped_to_target = false;

            if (!login_url.empty() && login_url.find("growtopia") != std::string::npos) {
                try {
                    HttpClient http_retry(login_url, use_https, main_proxy);
                    auto retry_login = http_retry.login(acc_name, acc_pass, version, platform);
                    if (retry_login.success) {
                        if (!retry_login.server_ip.empty()) game_host = retry_login.server_ip;
                        if (retry_login.server_port > 0) game_port = retry_login.server_port;
                        if (!retry_login.meta.empty()) login_resp.meta = retry_login.meta;
                        Logger::log("[RECONNECT] 🌐 HTTP login session diperbarui");
                    }
                } catch (...) {}
            }

            current_handshake = build_handshake(login_resp.meta);

            std::string conn_host;
            int conn_port = 0;
            get_enet_target(game_host, game_port, conn_host, conn_port);

            if (enet.connect(conn_host, conn_port, 5000)) {
                Logger::log("[RECONNECT] ✅ ENet terhubung ke ", game_host, ":", game_port);
            } else {
                Logger::log("[RECONNECT] ❌ Gagal connect ENet ke ", game_host, ":", game_port);
                reconnect_ctrl.onDisconnected("Gagal terhubung ke host server");
            }
        };

        enet.onDisconnect([&]() {
            if (!acc_name.empty() && !redirecting.load()) {
                in_world = false;
                reconnect_ctrl.onDisconnected("Koneksi ENet terputus dari server");
            }
        });

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

        BotStats single_stats;
        if (!acc_name.empty()) {
            single_stats.loadFromFile(acc_name);
        }

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

                std::string clean = Logger::stripColorCodes(text);
                if (single_stats.parseFromText(clean)) {
                    if (!acc_name.empty()) single_stats.saveToFile(acc_name);
                }

                if (text.find("action|play_sfx") == std::string::npos &&
                    text.find("action|ping") == std::string::npos &&
                    text.find("add_label") == std::string::npos &&
                    text.find("add_button") == std::string::npos &&
                    text.find("add_spacer") == std::string::npos) {
                    while (!clean.empty() && (clean.back() == '\r' || clean.back() == '\n' || clean.back() == ' ')) clean.pop_back();
                    if (!clean.empty() && clean.size() < 120) {
                        Logger::log("  💬 Server: ", clean);
                    }
                }

                if (text.find("Entering game") != std::string::npos ||
                    text.find("Logon authenticated") != std::string::npos) {
                    trigger_enter_game();
                }
                if (text.find("action|ping") != std::string::npos || text.find("ping\n") == 0) {
                    enet.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, "action|pong\n");
                }
            }
            else if (pkt.type == GtPacket::NET_MESSAGE_GAME_PACKET) {
                if (pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                    GameUpdatePacket tank_update{};
                    std::memcpy(&tank_update, pkt.payload.data(), sizeof(GameUpdatePacket));
                    lua_engine.triggerOnProcessTankUpdate(tank_update);
                }
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
                    reconnect_ctrl.onConnected();
                    single_stats.startSession();
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
                    int inv_gems = world.getItemCount(112);
                    if (inv_gems > 0) {
                        single_stats.gems = inv_gems;
                        if (!acc_name.empty()) single_stats.saveToFile(acc_name);
                    }
                    Logger::log("[+] 🎒 Inventory state diterima! Pemain siap di server.");
                    warp_to_target_world();
                }
                // Tank packet 1: PACKET_CALL_FUNCTION (VariantList)
                else if (tank_type == PACKET_CALL_FUNCTION && pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                    GameUpdatePacket call_pkt{};
                    std::memcpy(&call_pkt, pkt.payload.data(), sizeof(GameUpdatePacket));

                    auto vars = parseVariantList(pkt.payload.data() + sizeof(GameUpdatePacket),
                                                 pkt.payload.size() - sizeof(GameUpdatePacket));
                    if (!vars.empty()) {
                        // Dispatch ke Bothax Lua hook OnVariant
                        if (lua_engine.triggerOnVariant(vars, call_pkt.net_id, static_cast<int>(call_pkt.float_var))) {
                            return; // Diblokir oleh script Lua
                        }

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
                            reconnect_ctrl.onConnected();
                            single_stats.startSession();
                            std::string spawn_data = vars.size() > 1 ? vars[1].str_val : "";
                            world.parseSpawn(spawn_data, acc_name);
                            int lvl = world.getLocalLevel();
                            if (lvl > 0 && lvl < 250) {
                                single_stats.level = lvl;
                                if (!acc_name.empty()) single_stats.saveToFile(acc_name);
                            }
                            Logger::log("[+] 👤 OnSpawn diterima! Karakter terdaftar di world.");
                            Logger::log("[+] ✅ Akun sekarang ONLINE di dalam game!");

                            // Otomatis sinkronisasi profil diri sendiri seperti Lucifer bot asli
                            std::thread([&enet, &world, &in_world, &single_stats]() {
                                // Delay 2.5s setelah spawn sebelum wrench agar server stabil dan tidak memicu spam
                                std::this_thread::sleep_for(std::chrono::milliseconds(2500));
                                if (in_world.load() && enet.isConnected()) {
                                    int my_netid = world.getLocalNetId();
                                    if (my_netid >= 0) {
                                        enet.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, "action|wrench\nnetid|" + std::to_string(my_netid) + "\n");
                                    }
                                }
                                // Fallback /status hanya setelah 4s jika stats belum terisi
                                std::this_thread::sleep_for(std::chrono::milliseconds(4000));
                                if (in_world.load() && enet.isConnected()) {
                                    if (single_stats.level <= 1 && single_stats.played_hours <= 0.0f) {
                                        enet.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, "action|input\n|text|/status\n");
                                    }
                                }
                            }).detach();
                        }
                        else if (fn.find("OnSetPos") != std::string::npos) {
                            if (vars.size() > 1 && vars[1].type == 3) {
                                float new_x = vars[1].float_val;
                                float new_y = vars[1].float_val2;
                                world.setLocalPos(new_x, new_y);
                                Logger::log("[WORLD] 📍 OnSetPos diterima: Pos=(", new_x, ", ", new_y, ") Tile=(", (int)(new_x / 32.0f), ", ", (int)(new_y / 32.0f), ")");
                            }
                        }
                        else if (fn.find("OnRemove") != std::string::npos) {
                            std::string remove_data = vars.size() > 1 ? vars[1].str_val : "";
                            world.parseRemove(remove_data);
                        }
                        else if (fn.find("OnSetBumperCoinValues") != std::string::npos) {
                            if (vars.size() > 1) {
                                single_stats.gems = get_vint(vars[1]);
                                if (vars.size() > 2) {
                                    int val2 = get_vint(vars[2]);
                                    if (val2 > 0 && val2 < 250) {
                                        single_stats.level = val2;
                                    }
                                }
                                if (!acc_name.empty()) single_stats.saveToFile(acc_name);
                                Logger::log("[+] 💎 Stats diperbarui (OnSetBumperCoinValues): Gems=", single_stats.gems, ", Level=", single_stats.level);
                            }
                        }
                        else if (fn.find("OnSetGems") != std::string::npos || fn.find("SetGems") != std::string::npos) {
                            if (vars.size() > 1) {
                                single_stats.gems = get_vint(vars[1]);
                                if (!acc_name.empty()) single_stats.saveToFile(acc_name);
                                Logger::log("[+] 💎 Stats Gems diperbarui: ", single_stats.gems);
                            }
                        }
                        else if (fn.find("OnSetLevel") != std::string::npos || fn.find("SetLevel") != std::string::npos) {
                            if (vars.size() > 1) {
                                int lvl = get_vint(vars[1]);
                                if (lvl > 0 && lvl < 250) {
                                    single_stats.level = lvl;
                                    if (!acc_name.empty()) single_stats.saveToFile(acc_name);
                                    Logger::log("[+] ⭐ Stats Level diperbarui: ", single_stats.level);
                                }
                            }
                        }
                        else if (fn.find("OnDialogRequest") != std::string::npos) {
                            if (vars.size() > 1) {
                                std::string dmsg = vars[1].str_val;
                                single_last_dialog = dmsg;
                                if (single_stats.parseFromText(dmsg)) {
                                    if (!acc_name.empty()) single_stats.saveToFile(acc_name);
                                    Logger::log("[+] 📊 Stats diperbarui dari OnDialogRequest: Level=", single_stats.level,
                                                ", Gems=", single_stats.gems, ", Played=", single_stats.getEffectivePlayedHours(),
                                                "h, Age=", single_stats.getEffectiveAgeDays(), "d");
                                }

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
                                    std::thread([&enet, dname]() {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(600));
                                        if (enet.isConnected()) {
                                            enet.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT,
                                                "action|dialog_return\ndialog_name|" + dname + "\nbuttonClicked|personal_info\n");
                                        }
                                    }).detach();
                                } else if (dmsg.find("Total time played") != std::string::npos ||
                                           dmsg.find("created") != std::string::npos) {
                                    std::string dname = "popup";
                                    size_t epos = dmsg.find("end_dialog|");
                                    if (epos != std::string::npos) {
                                        size_t dname_start = epos + 11;
                                        size_t pipe = dmsg.find('|', dname_start);
                                        if (pipe != std::string::npos) {
                                            dname = dmsg.substr(dname_start, pipe - dname_start);
                                        }
                                    }
                                    std::thread([&enet, dname]() {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(800));
                                        if (enet.isConnected()) {
                                            enet.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT,
                                                "action|dialog_return\ndialog_name|" + dname + "\nbuttonClicked|\n");
                                        }
                                    }).detach();
                                }
                            }
                        }
                        else if (fn.find("OnTalkBubble") != std::string::npos) {
                            int bubble_netid = vars.size() > 1 ? get_vint(vars[1]) : -1;
                            std::string bubble_msg = vars.size() > 2 ? vars[2].str_val : "";
                            if (single_stats.parseFromText(bubble_msg)) {
                                if (!acc_name.empty()) single_stats.saveToFile(acc_name);
                            }
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
                                std::string cmsg = vars[1].str_val;
                                std::string clean = Logger::stripColorCodes(cmsg);
                                if (single_stats.parseFromText(clean)) {
                                    if (!acc_name.empty()) single_stats.saveToFile(acc_name);
                                    Logger::log("[+] 📊 Stats diperbarui dari OnConsoleMessage: Level=", single_stats.level,
                                                ", Gems=", single_stats.gems, ", Age=", single_stats.getEffectiveAgeDays(), "d");
                                }
                                Logger::log("  💬 Console: ", clean);
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

                            // Buat handshake untuk sub-server menggunakan fingerprint yang sama
                            std::string sub_handshake = build_handshake(login_resp.meta, userId, token, door_id, lmode);

                            // Hubungkan ke subserver pada background thread
                            std::thread([&enet, &world, sub_host, sub_port, sub_handshake, &current_handshake, &handshake_sent, &auto_warped, &warped_to_target, &in_world, &redirecting, &get_enet_target]() {
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

                                std::string conn_host;
                                int conn_port = 0;
                                get_enet_target(sub_host, sub_port, conn_host, conn_port);

                                if (enet.connect(conn_host, conn_port, 6000)) {
                                    Logger::log("[+] ✅ Terhubung ke sub-server ", sub_host, ":", sub_port);
                                } else {
                                    Logger::log("[!] ❌ Gagal terhubung ke sub-server!");
                                }
                                redirecting = false;
                            }).detach();
                        }
                        else {
                            Logger::logDebug("  ⚡ Variant Function: ", fn);
                        }
                    }
                }
                // Tank packet 21/22/23: Ping Request / Keepalive from server
                else if ((tank_type == PACKET_PING_REQUEST || tank_type == 21 || tank_type == 22 || tank_type == 23) && pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                    GameUpdatePacket ping_in;
                    std::memcpy(&ping_in, pkt.payload.data(), sizeof(GameUpdatePacket));

                    GameUpdatePacket ping_reply{};
                    ping_reply.type = PACKET_PING_REPLY; // 21
                    ping_reply.net_id = world.getLocalNetId();
                    ping_reply.main_data = ping_in.main_data;
                    ping_reply.pos_x = 64.0f;
                    ping_reply.pos_y = 64.0f;
                    ping_reply.speed_x = 1000.0f;
                    ping_reply.speed_y = 250.0f;
                    actions.sendTankPacket(ping_reply);

                    // Kirim juga reply dengan type 22 jika request server bernilai 23
                    if (tank_type == 23) {
                        ping_reply.type = 22;
                        actions.sendTankPacket(ping_reply);
                    }

                    Logger::logDebug("[ENet] 🏓 PING_REQUEST (type=", (int)tank_type, ") dijawab dengan PING_REPLY!");
                }
                // Tank packet 20: PACKET_SET_CHARACTER_STATE (State/Clothing update)
                else if (tank_type == PACKET_SET_CHARACTER_STATE && pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                    // Packet karakter state dari server (bukan error)
                }
                // Tank packet 13: PACKET_SET_ICON_STATE
                else if (tank_type == PACKET_SET_ICON_STATE && pkt.payload.size() >= sizeof(GameUpdatePacket)) {
                    // Packet icon state dari server (bukan error)
                }
                else {
                    Logger::logDebug("[ENet] 📦 Tank Packet tidak tertangani: type=", (int)tank_type, " len=", pkt.payload.size());
                }
            }
            else if (pkt.type == GtPacket::NET_MESSAGE_ERROR) {
                std::string err((const char*)pkt.payload.data(), pkt.payload.size());
                while (!err.empty() && err.back() == '\0') err.pop_back();
                Logger::log("  ❌ ERROR dari server: ", err);
            }
        });

        if (!acc_name.empty()) {
            std::string conn_host;
            int conn_port = 0;
            get_enet_target(game_host, game_port, conn_host, conn_port);

            if (!enet.connect(conn_host, conn_port, 5000)) {
                Logger::log("[!] Gagal connect ENet ke ", game_host, ":", game_port);
                Logger::log("    Cek: server GTPS ENet jalan? port benar? firewall?");
                Logger::log("    Auto-reconnect akan mencoba menyambung ulang...");
                reconnect_ctrl.onDisconnected("Gagal koneksi awal ENet");
            }
        }

        BotInfo info{acc_name, version, platform, game_host, game_port};
        LuciferGui gui(enet, world, actions, autofarm, lua_engine, info, g_running,
                       fingerprint, reconnect_ctrl, do_reconnect);
        gui.setMainProxy(main_proxy);
        gui.getStats() = single_stats;
        gui.setFallbackLastDialog(single_last_dialog);

        // Inisialisasi Multi-Bot Manager & Thread Pool
        BotManager bot_manager(4);
        bot_manager.default_login_url = login_url;
        bot_manager.default_use_https = use_https;
        bot_manager.default_game_host = game_host;
        bot_manager.default_game_port = game_port;
        bot_manager.default_version = version;
        bot_manager.default_platform = platform;
        bot_manager.default_protocol = protocol;
        bot_manager.default_proxy = main_proxy;

        // Daftarkan akun utama ke manager jika dikonfigurasi
        if (!acc_name.empty()) {
            bot_manager.addBot(acc_name, acc_pass, target_world, main_proxy);
        }

        // Muat akun dari cache/accounts.txt atau migrasi dari accounts.txt
        if (std::filesystem::exists("cache/accounts.txt")) {
            bot_manager.loadAccountsFromFile("cache/accounts.txt");
        } else if (std::filesystem::exists("accounts.txt")) {
            bot_manager.loadAccountsFromFile("accounts.txt");
            bot_manager.saveAccountsToFile("cache/accounts.txt");
        }

        // Pemulihan otomatis (Auto-Recovery) jika list bot masih kosong tetapi ada profil akun tersimpan
        if (bot_manager.getBots().empty() && std::filesystem::exists("cache/fingerprints")) {
            for (const auto& entry : std::filesystem::directory_iterator("cache/fingerprints")) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    std::string stem = entry.path().stem().string();
                    if (stem != "default" && !stem.empty()) {
                        Logger::log("[MANAGER] 🔄 Menemukan profil fingerprint '", stem, "', memulihkan akun ke daftar...");
                        bot_manager.addBot(stem, "", "");
                    }
                }
            }
            if (!bot_manager.getBots().empty()) {
                bot_manager.saveAccountsToFile("cache/accounts.txt");
            }
        }

        gui.setBotManager(&bot_manager);
        lua_engine.setBotManager(&bot_manager);

        Logger::log("\n========================================================");
        Logger::log("  🔥 Membuka Antarmuka GUI Lucifer (Dear ImGui DX11)...");
        Logger::log("========================================================\n");

        if (gui.init(::GetModuleHandle(nullptr), 1080, 720)) {
            // Jalankan GUI Lucifer (Main render loop + ENet polling + Auto-reconnect update)
            gui.run();
            gui.shutdown();
        } else {
            Logger::log("[!] Gagal membuka jendela GUI DirectX 11, fallback ke CLI...");
            Cli cli(enet, g_running, info, world, actions, autofarm);
            cli.start();
            while (g_running) {
                enet.poll();
                reconnect_ctrl.update(do_reconnect);
                bot_manager.pollAll();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            cli.stop();
        }

        bot_manager.stopAll();

        lua_engine.stopScript();
        autofarm.stop();
        enet.disconnect();

    } catch (const std::exception& e) {
        Logger::log("[!] Exception: ", e.what());
        return 1;
    }

    Logger::log("[*] Selesai.");
    return 0;
}