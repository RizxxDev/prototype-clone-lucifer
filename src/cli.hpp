#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>

class EnetClient;

struct BotInfo {
    std::string account_name;
    std::string version;
    std::string platform;
    std::string game_host;
    int game_port = 0;
};

class Cli {
public:
    Cli(EnetClient& enet, std::atomic<bool>& running, const BotInfo& info);
    ~Cli();

    // Memulai REPL thread
    void start();

    // Menghentikan REPL thread
    void stop();

private:
    void run();
    void processCommand(const std::string& line);

    // Command handlers
    void cmdHelp();
    void cmdSay(const std::string& text);
    void cmdWarp(const std::string& world);
    void cmdRaw(const std::string& hex_str);
    void cmdStatus();
    void cmdClear();
    void cmdQuit();

    // Helper hex parsing
    static bool hexToBytes(const std::string& hex, std::vector<uint8_t>& out);

    EnetClient& enet_;
    std::atomic<bool>& running_;
    BotInfo info_;
    std::thread thread_;
};
