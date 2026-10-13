#include "cli.hpp"
#include "enet_client.hpp"
#include "packet.hpp"
#include "logger.hpp"
#include "world.hpp"
#include "bot_actions.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

Cli::Cli(EnetClient& enet, std::atomic<bool>& running, const BotInfo& info,
         WorldState& world, BotActions& actions, AutoFarmController& autofarm,
         AutoCollectController& autocollect)
    : enet_(enet), running_(running), info_(info),
      world_(world), actions_(actions), autofarm_(autofarm), autocollect_(autocollect) {}

Cli::~Cli() {
    stop();
}

void Cli::start() {
    if (thread_.joinable()) {
        thread_.detach();
    }
    thread_ = std::thread(&Cli::run, this);
}

void Cli::stop() {
    if (thread_.joinable()) {
        thread_.detach();
    }
}

void Cli::run() {
    std::string line;
    while (running_ && enet_.isConnected()) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            break;
        }

        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            continue;
        }
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        if (!line.empty()) {
            processCommand(line);
        }
    }
}

void Cli::processCommand(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    std::string args;
    std::getline(iss, args);
    size_t start = args.find_first_not_of(" \t");
    if (start != std::string::npos) {
        args = args.substr(start);
    } else {
        args.clear();
    }

    std::string cmd_lower = cmd;
    std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Lucifer Command Dispatcher
    if (cmd_lower == "help" || cmd_lower == "?") {
        cmdHelp();
    } else if (cmd_lower == "say" || cmd_lower == "s") {
        cmdSay(args);
    } else if (cmd_lower == "warp" || cmd_lower == "w") {
        cmdWarp(args);
    } else if (cmd_lower == "move" || cmd_lower == "m") {
        cmdMove(args);
    } else if (cmd_lower == "punch" || cmd_lower == "p" || cmd_lower == "hit") {
        cmdPunch(args);
    } else if (cmd_lower == "place") {
        cmdPlace(args);
    } else if (cmd_lower == "wrench") {
        cmdWrench(args);
    } else if (cmd_lower == "enter") {
        cmdEnter();
    } else if (cmd_lower == "collect" || cmd_lower == "c") {
        cmdCollect(args);
    } else if (cmd_lower == "drop") {
        cmdDrop(args);
    } else if (cmd_lower == "trash") {
        cmdTrash(args);
    } else if (cmd_lower == "inv" || cmd_lower == "inventory") {
        cmdInventory();
    } else if (cmd_lower == "players" || cmd_lower == "who") {
        cmdPlayers();
    } else if (cmd_lower == "world") {
        cmdWorld();
    } else if (cmd_lower == "pos" || cmd_lower == "where") {
        cmdPos();
    } else if (cmd_lower == "farm") {
        cmdFarm(args);
    } else if (cmd_lower == "stopfarm") {
        cmdStopFarm();
    } else if (cmd_lower == "autocollect" || cmd_lower == "ac") {
        cmdAutoCollect(args);
    } else if (cmd_lower == "stopcollect" || cmd_lower == "sac") {
        cmdStopCollect();
    } else if (cmd_lower == "respawn") {
        cmdRespawn();
    } else if (cmd_lower == "raw") {
        cmdRaw(args);
    } else if (cmd_lower == "status" || cmd_lower == "st") {
        cmdStatus();
    } else if (cmd_lower == "clear" || cmd_lower == "cls") {
        cmdClear();
    } else if (cmd_lower == "quit" || cmd_lower == "exit" || cmd_lower == "q") {
        cmdQuit();
    } else {
        Logger::log("[CLI] Perintah tidak dikenal: '", cmd, "'. Ketik 'help' untuk daftar perintah Lucifer.");
    }
}

void Cli::cmdHelp() {
    std::lock_guard<std::mutex> lock(Logger::mtx);
    std::cout << "\n================= LUCIFER BOT COMMAND SUITE =================\n"
              << " [Movement & Tiles]\n"
              << "  pos, where              - Menampilkan koordinat bot (pixel & tile)\n"
              << "  move, m <x> <y>         - Berjalan halus ke tile (x, y)\n"
              << "  punch, p, hit <x> <y>   - Memukul/menghancurkan blok pada tile (x, y)\n"
              << "  place <id> <x> <y>      - Memasang blok ID pada tile (x, y)\n"
              << "  wrench <x> <y>          - Membuka menu wrench tile (x, y)\n"
              << "  enter                   - Mengaktifkan pintu/portal di posisi bot\n\n"
              << " [Backpack & Items]\n"
              << "  inv, inventory          - Melihat isi inventory/backpack bot\n"
              << "  collect, c [radius]     - Mengambil drop item di sekitar (default: 4 tile)\n"
              << "  drop <id> <count>       - Menjatuhkan item dari backpack\n"
              << "  trash <id> <count>      - Membuang/menghancurkan item dari backpack\n\n"
              << " [World & Players]\n"
              << "  world                   - Menampilkan info world saat ini\n"
              << "  players, who            - Menampilkan daftar pemain di world\n"
              << "  warp, w <world>         - Berpindah ke world lain\n"
              << "  say, s <text>           - Mengirim chat ke world\n"
              << "  respawn                 - Respawn karakter kembali ke pintu masuk\n\n"
              << " [Automation / Lucifer Auto-Farm & Collector]\n"
              << "  farm <id> <x> <y> [ms]  - Mulai auto-farm loop pada tile (x, y)\n"
              << "  stopfarm                - Menghentikan auto-farm\n"
              << "  autocollect, ac [id] [d]- Mulai auto-collect sweeper dengan pathfinding\n"
              << "  stopcollect, sac        - Menghentikan auto-collect sweeper\n\n"
              << " [Sistem & Debug]\n"
              << "  status, st              - Status koneksi dan akun\n"
              << "  raw <hex>               - Kirim paket mentah hex\n"
              << "  clear, cls              - Bersihkan layar\n"
              << "  quit, exit, q           - Disconnect dan keluar\n"
              << "=============================================================\n\n";
}

void Cli::cmdSay(const std::string& text) {
    if (text.empty()) {
        Logger::log("[CLI] Gunakan: say <pesan>");
        return;
    }
    actions_.say(text);
}

void Cli::cmdWarp(const std::string& world) {
    if (world.empty()) {
        Logger::log("[CLI] Gunakan: warp <nama_world>");
        return;
    }
    actions_.warp(world);
}

void Cli::cmdMove(const std::string& args) {
    std::istringstream iss(args);
    int x = 0, y = 0;
    if (!(iss >> x >> y)) {
        Logger::log("[CLI] Gunakan: move <tile_x> <tile_y> (contoh: move 20 23)");
        return;
    }
    actions_.moveTo(x, y);
}

void Cli::cmdPunch(const std::string& args) {
    std::istringstream iss(args);
    int x = 0, y = 0;
    if (!(iss >> x >> y)) {
        float px = 0, py = 0;
        int tx = 0, ty = 0;
        world_.getLocalPos(px, py, tx, ty);
        x = tx;
        y = ty;
        Logger::log("[CLI] Tidak ada koordinat, memukul tile bot saat ini (", x, ", ", y, ")");
    }
    actions_.hit(x, y);
    Logger::log("[CLI] 👊 Memukul tile (", x, ", ", y, ")");
}

void Cli::cmdPlace(const std::string& args) {
    std::istringstream iss(args);
    int id = 0, x = 0, y = 0;
    if (!(iss >> id >> x >> y)) {
        Logger::log("[CLI] Gunakan: place <item_id> <tile_x> <tile_y> (contoh: place 2 20 24)");
        return;
    }
    actions_.place(static_cast<uint16_t>(id), x, y);
    Logger::log("[CLI] 🧱 Memasang item ID=", id, " pada tile (", x, ", ", y, ")");
}

void Cli::cmdWrench(const std::string& args) {
    std::istringstream iss(args);
    int x = 0, y = 0;
    if (!(iss >> x >> y)) {
        float px = 0, py = 0;
        int tx = 0, ty = 0;
        world_.getLocalPos(px, py, tx, ty);
        x = tx;
        y = ty;
    }
    actions_.wrench(x, y);
}

void Cli::cmdEnter() {
    actions_.enter();
}

void Cli::cmdCollect(const std::string& args) {
    float radius = 4.0f;
    if (!args.empty()) {
        try { radius = std::stof(args); } catch (...) {}
    }
    int count = actions_.collectNearby(radius);
    Logger::log("[CLI] 🧲 Mengambil item di sekitar radius ", radius, " tile (", count, " paket terkirim)");
}

void Cli::cmdDrop(const std::string& args) {
    std::istringstream iss(args);
    int id = 0, count = 0;
    if (!(iss >> id >> count)) {
        Logger::log("[CLI] Gunakan: drop <item_id> <jumlah>");
        return;
    }
    actions_.drop(static_cast<uint16_t>(id), count);
}

void Cli::cmdTrash(const std::string& args) {
    std::istringstream iss(args);
    int id = 0, count = 0;
    if (!(iss >> id >> count)) {
        Logger::log("[CLI] Gunakan: trash <item_id> <jumlah>");
        return;
    }
    actions_.trash(static_cast<uint16_t>(id), count);
}

void Cli::cmdInventory() {
    auto inv = world_.getInventory();
    uint32_t slots = world_.getInventorySlots();

    std::lock_guard<std::mutex> lock(Logger::mtx);
    std::cout << "\n================ BACKPACK / INVENTORY ================\n"
              << " Total Slot: " << inv.size() << " / " << slots << "\n"
              << "------------------------------------------------------\n"
              << " Slot | Item ID | Jumlah | Status\n"
              << "------------------------------------------------------\n";

    if (inv.empty()) {
        std::cout << " (Inventory kosong atau belum dimuat dari server)\n";
    } else {
        int idx = 1;
        for (const auto& item : inv) {
            std::cout << "  #" << std::left << std::setw(3) << idx++
                      << " | ID: " << std::setw(6) << item.id
                      << " | x" << std::setw(5) << (int)item.count
                      << " | " << (item.flags ? "Equipped" : "-")
                      << "\n";
        }
    }
    std::cout << "======================================================\n\n";
}

void Cli::cmdPlayers() {
    auto players = world_.getPlayers();

    std::lock_guard<std::mutex> lock(Logger::mtx);
    std::cout << "\n================ DAFTAR PEMAIN DI WORLD ================\n"
              << " Total: " << players.size() << " pemain\n"
              << "--------------------------------------------------------\n";

    for (const auto& p : players) {
        std::string clean_name = WorldState::stripColorCodes(p.name);
        int tx = static_cast<int>(p.x / 32.0f);
        int ty = static_cast<int>(p.y / 32.0f);

        std::cout << " • " << clean_name
                  << (p.is_local ? " [BOT KITA]" : "")
                  << " (NetID: " << p.net_id
                  << ", UserID: " << p.user_id
                  << ", Country: " << p.country
                  << ", Pos: " << tx << "," << ty << ")\n";
    }
    std::cout << "========================================================\n\n";
}

void Cli::cmdWorld() {
    std::string wname = world_.getWorldName();
    int w = world_.getWidth();
    int h = world_.getHeight();

    float px = 0, py = 0;
    int tx = 0, ty = 0;
    world_.getLocalPos(px, py, tx, ty);

    uint16_t fg = 0, bg = 0;
    world_.getTile(tx, ty, fg, bg);

    std::lock_guard<std::mutex> lock(Logger::mtx);
    std::cout << "\n================ INFORMASI WORLD ================\n"
              << "  Nama World   : " << (wname.empty() ? "(Belum masuk world)" : wname) << "\n"
              << "  Ukuran       : " << w << " x " << h << " tiles\n"
              << "  Posisi Bot   : Tile (" << tx << ", " << ty << ") [Pixel: " << px << ", " << py << "]\n"
              << "  Tile di Bot  : FG=" << fg << " | BG=" << bg << "\n"
              << "=================================================\n\n";
}

void Cli::cmdPos() {
    float px = 0, py = 0;
    int tx = 0, ty = 0;
    world_.getLocalPos(px, py, tx, ty);
    Logger::log("[CLI] 📍 Posisi Bot: Tile (", tx, ", ", ty, ") | Pixel (", px, ", ", py, ") | NetID: ", world_.getLocalNetId());
}

void Cli::cmdFarm(const std::string& args) {
    std::istringstream iss(args);
    int id = 0, x = 0, y = 0, delay = 180, hits = 4;
    if (!(iss >> id >> x >> y)) {
        Logger::log("[CLI] Gunakan: farm <item_id> <tile_x> <tile_y> [delay_ms] [hits_per_block]");
        Logger::log("      Contoh: farm 2 20 24 160 4");
        return;
    }
    iss >> delay;
    iss >> hits;

    if (autofarm_.start(static_cast<uint16_t>(id), x, y, delay, hits)) {
        Logger::log("[CLI] 🚜 Auto-farm diaktifkan untuk Item ID=", id, " di (", x, ", ", y, ")");
    } else {
        Logger::log("[CLI] ⚠️ Auto-farm sudah berjalan! Ketik 'stopfarm' untuk menghentikan terlebih dahulu.");
    }
}

void Cli::cmdStopFarm() {
    if (autofarm_.isRunning()) {
        autofarm_.stop();
        Logger::log("[CLI] 🛑 Auto-farm dihentikan.");
    } else {
        Logger::log("[CLI] Auto-farm tidak sedang berjalan.");
    }
}

void Cli::cmdAutoCollect(const std::string& args) {
    uint16_t filter_id = 0;
    int delay = 600;
    if (!args.empty()) {
        std::istringstream iss(args);
        int id_val = 0;
        if (iss >> id_val) filter_id = static_cast<uint16_t>(id_val);
        iss >> delay;
    }

    if (autocollect_.start(filter_id, delay)) {
        Logger::log("[CLI] 🧲 Auto-collect sweeper diaktifkan (Filter ID=", filter_id, ", Delay=", delay, "ms)");
    } else {
        Logger::log("[CLI] ⚠️ Auto-collect sweeper sudah berjalan! Ketik 'stopcollect' untuk menghentikan.");
    }
}

void Cli::cmdStopCollect() {
    if (autocollect_.isRunning()) {
        autocollect_.stop();
        Logger::log("[CLI] 🛑 Auto-collect sweeper dihentikan.");
    } else {
        Logger::log("[CLI] Auto-collect sweeper tidak sedang berjalan.");
    }
}

void Cli::cmdRespawn() {
    actions_.respawn();
}

void Cli::cmdRaw(const std::string& hex_str) {
    if (hex_str.empty()) {
        Logger::log("[CLI] Gunakan: raw <hex_string>");
        return;
    }
    if (!enet_.isConnected()) {
        Logger::log("[CLI] Bot belum terhubung ke game server!");
        return;
    }

    std::vector<uint8_t> bytes;
    if (!hexToBytes(hex_str, bytes)) {
        Logger::log("[CLI] Format hex tidak valid!");
        return;
    }

    if (bytes.size() < 4) {
        Logger::log("[CLI] Paket hex minimal 4 bytes untuk type!");
        return;
    }

    uint32_t type = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
    std::vector<uint8_t> payload(bytes.begin() + 4, bytes.end());

    enet_.send(type, payload);
    Logger::log("[CLI] [*] Raw send type=", type, " len=", payload.size());
}

void Cli::cmdStatus() {
    std::lock_guard<std::mutex> lock(Logger::mtx);
    std::cout << "\n----------------- Status Bot -----------------\n"
              << "  Akun      : " << info_.account_name << "\n"
              << "  Server    : " << info_.game_host << ":" << info_.game_port << "\n"
              << "  Platform  : " << info_.platform << "\n"
              << "  Version   : " << info_.version << "\n"
              << "  World     : " << world_.getWorldName() << "\n"
              << "  Auto-Farm : " << (autofarm_.isRunning() ? "AKTIF" : "NONAKTIF") << "\n"
              << "  Sweeper   : " << (autocollect_.isRunning() ? "AKTIF" : "NONAKTIF") << "\n"
              << "  On-Move AC: " << (actions_.isAutoCollectOnMove() ? ("AKTIF (Radius: " + std::to_string(actions_.getMovementCollectRadius()).substr(0, 3) + " Tile, Delay: " + std::to_string(actions_.getPickupDelay()) + "ms)") : "NONAKTIF") << "\n"
              << "  Passive   : " << (autocollect_.isPassiveEnabled() ? ("AKTIF (Radius: " + std::to_string(autocollect_.getProximityRadius()).substr(0, 3) + " Tile)") : "NONAKTIF") << "\n"
              << "  Status    : " << (enet_.isConnected() ? "TERHUBUNG (Online)" : "TERPUTUS (Offline)") << "\n"
              << "----------------------------------------------\n\n";
}

void Cli::cmdClear() {
#ifdef _WIN32
    std::system("cls");
#else
    std::system("clear");
#endif
}

void Cli::cmdQuit() {
    Logger::log("[CLI] Menghentikan bot...");
    autofarm_.stop();
    running_ = false;
    enet_.disconnect();
}

bool Cli::hexToBytes(const std::string& hex, std::vector<uint8_t>& out) {
    out.clear();
    std::string clean;
    for (char c : hex) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            clean.push_back(c);
        }
    }
    if (clean.size() % 2 != 0) return false;

    for (size_t i = 0; i < clean.size(); i += 2) {
        std::string byte_str = clean.substr(i, 2);
        char* end = nullptr;
        long val = std::strtol(byte_str.c_str(), &end, 16);
        if (*end != '\0' || val < 0 || val > 255) {
            return false;
        }
        out.push_back(static_cast<uint8_t>(val));
    }
    return true;
}
