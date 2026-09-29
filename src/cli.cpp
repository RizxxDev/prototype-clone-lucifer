#include "cli.hpp"
#include "enet_client.hpp"
#include "packet.hpp"
#include "logger.hpp"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

Cli::Cli(EnetClient& enet, std::atomic<bool>& running, const BotInfo& info)
    : enet_(enet), running_(running), info_(info) {}

Cli::~Cli() {
    stop();
}

void Cli::start() {
    thread_ = std::thread(&Cli::run, this);
}

void Cli::stop() {
    if (thread_.joinable()) {
        // Pada Windows/terminal getline blocking, thread akan selesai saat proses exit
        // atau saat input ditutup
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

        // Trim leading and trailing whitespace
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
    // Trim leading whitespace from arguments
    size_t start = args.find_first_not_of(" \t");
    if (start != std::string::npos) {
        args = args.substr(start);
    } else {
        args.clear();
    }

    // Convert command to lowercase
    std::string cmd_lower = cmd;
    std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (cmd_lower == "help" || cmd_lower == "?") {
        cmdHelp();
    } else if (cmd_lower == "say" || cmd_lower == "s") {
        cmdSay(args);
    } else if (cmd_lower == "warp" || cmd_lower == "w") {
        cmdWarp(args);
    } else if (cmd_lower == "raw") {
        cmdRaw(args);
    } else if (cmd_lower == "status" || cmd_lower == "st") {
        cmdStatus();
    } else if (cmd_lower == "clear" || cmd_lower == "cls") {
        cmdClear();
    } else if (cmd_lower == "quit" || cmd_lower == "exit" || cmd_lower == "q") {
        cmdQuit();
    } else {
        Logger::log("[CLI] Perintah tidak dikenal: '", cmd, "'. Ketik 'help' untuk daftar perintah.");
    }
}

void Cli::cmdHelp() {
    std::lock_guard<std::mutex> lock(Logger::mtx);
    std::cout << "\n================ Daftar Perintah ================\n"
              << "  help, ?            - Menampilkan bantuan ini\n"
              << "  say, s <text>      - Mengirim pesan chat ke world\n"
              << "  warp, w <world>    - Berpindah ke world tujuan\n"
              << "  raw <hex>          - Mengirim paket mentah (hex string)\n"
              << "  status, st         - Menampilkan status koneksi bot\n"
              << "  clear, cls         - Membersihkan layar konsol\n"
              << "  quit, exit, q      - Keluar dan disconnect dari server\n"
              << "=================================================\n\n";
}

void Cli::cmdSay(const std::string& text) {
    if (text.empty()) {
        Logger::log("[CLI] Gunakan: say <pesan>");
        return;
    }
    if (!enet_.isConnected()) {
        Logger::log("[CLI] Bot belum terhubung ke game server!");
        return;
    }

    std::string packet_str = "action|input\n|text|" + text + "\n";
    enet_.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, packet_str);
    Logger::log("[CLI] Chat terkirim: \"", text, "\"");
}

void Cli::cmdWarp(const std::string& world) {
    if (world.empty()) {
        Logger::log("[CLI] Gunakan: warp <nama_world>");
        return;
    }
    if (!enet_.isConnected()) {
        Logger::log("[CLI] Bot belum terhubung ke game server!");
        return;
    }

    std::string packet_str = "action|join_request\nname|" + world + "\ninvitedWorld|0\n";
    enet_.sendText(GtPacket::NET_MESSAGE_GAME_MESSAGE, packet_str);
    enet_.sendText(GtPacket::NET_MESSAGE_GENERIC_TEXT, packet_str);
    Logger::log("[CLI] Request warp ke: ", world);
}

void Cli::cmdRaw(const std::string& hex_str) {
    if (hex_str.empty()) {
        Logger::log("[CLI] Gunakan: raw <hex_string> (contoh: 02000000050068656c6c6f)");
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
