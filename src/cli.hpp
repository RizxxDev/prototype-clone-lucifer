#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <vector>

class EnetClient;
class WorldState;
class BotActions;
class AutoFarmController;
class AutoCollectController;

struct BotInfo {
    std::string account_name;
    std::string version;
    std::string platform;
    std::string game_host;
    int game_port = 0;
};

class Cli {
public:
    Cli(EnetClient& enet, std::atomic<bool>& running, const BotInfo& info,
        WorldState& world, BotActions& actions, AutoFarmController& autofarm,
        AutoCollectController& autocollect);
    ~Cli();

    // Memulai REPL thread
    void start();

    // Menghentikan REPL thread
    void stop();

private:
    void run();
    void processCommand(const std::string& line);

    // Command handlers (Lucifer CLI Suite)
    void cmdHelp();
    void cmdSay(const std::string& text);
    void cmdWarp(const std::string& world);
    void cmdMove(const std::string& args);
    void cmdPunch(const std::string& args);
    void cmdPlace(const std::string& args);
    void cmdWrench(const std::string& args);
    void cmdEnter();
    void cmdCollect(const std::string& args);
    void cmdDrop(const std::string& args);
    void cmdTrash(const std::string& args);
    void cmdInventory();
    void cmdPlayers();
    void cmdWorld();
    void cmdPos();
    void cmdFarm(const std::string& args);
    void cmdStopFarm();
    void cmdAutoCollect(const std::string& args);
    void cmdStopCollect();
    void cmdRespawn();
    void cmdRaw(const std::string& hex_str);
    void cmdStatus();
    void cmdClear();
    void cmdQuit();

    bool hexToBytes(const std::string& hex, std::vector<uint8_t>& out);

    EnetClient& enet_;
    std::atomic<bool>& running_;
    BotInfo info_;
    WorldState& world_;
    BotActions& actions_;
    AutoFarmController& autofarm_;
    AutoCollectController& autocollect_;
    std::thread thread_;
};
