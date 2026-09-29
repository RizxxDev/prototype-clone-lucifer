#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>

struct lua_State;
class BotActions;
class WorldState;
class EnetClient;

// ============================================================
// Lucifer Lua Scripting Engine (API Compatible with Lucifer v2.86)
// ============================================================

class LuaEngine {
public:
    LuaEngine(BotActions& actions, WorldState& world, EnetClient& enet);
    ~LuaEngine();

    // Jalankan script Lua di background worker thread
    bool runScript(const std::string& script_code);

    // Hentikan eksekusi script yang sedang berjalan
    void stopScript();

    // Status eksekusi
    bool isRunning() const;
    std::string getLastError() const;
    int getExecutionTimeMs() const;

private:
    void workerThread(std::string code);
    void registerLuciferApi(lua_State* L);

    // Lua C Callback implementations
    static int luaSleep(lua_State* L);
    static int luaLog(lua_State* L);
    static int luaGetBot(lua_State* L);
    static int luaGetBots(lua_State* L);

    // Bot methods
    static int botSay(lua_State* L);
    static int botWarp(lua_State* L);
    static int botMoveTo(lua_State* L);
    static int botHit(lua_State* L);
    static int botPlace(lua_State* L);
    static int botWrench(lua_State* L);
    static int botEnter(lua_State* L);
    static int botCollect(lua_State* L);
    static int botDrop(lua_State* L);
    static int botTrash(lua_State* L);
    static int botRespawn(lua_State* L);
    static int botGetLocal(lua_State* L);
    static int botGetWorld(lua_State* L);
    static int botGetInventory(lua_State* L);
    static int botGetPlayers(lua_State* L);
    static int botFindItem(lua_State* L);

    BotActions& actions_;
    WorldState& world_;
    EnetClient& enet_;

    std::atomic<bool> is_running_{false};
    std::atomic<bool> stop_requested_{false};
    std::string last_error_;
    mutable std::mutex error_mtx_;
    std::thread worker_;
    std::chrono::steady_clock::time_point start_time_;
};
