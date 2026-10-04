#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <functional>
#include "socks5.hpp"

struct lua_State;
class BotActions;
class WorldState;
class EnetClient;
struct Variant;
struct GameUpdatePacket;
struct BotStats;

// ============================================================
// Bothax & Lucifer Lua Scripting Engine
// Fully compatible with Bothax Lua Documentation & Lucifer v2.86
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

    // Bothax Hook Dispatchers (Dipanggil saat event GTPS terjadi)
    bool triggerOnVariant(const std::vector<Variant>& vars, int netid, int delay);
    bool triggerOnSendPacket(int type, const std::string& packet);
    bool triggerOnSendPacketRaw(const GameUpdatePacket& pkt);
    void triggerOnProcessTankUpdate(const GameUpdatePacket& pkt);
    void triggerOnWorldTouch(float x, float y, bool start);
    void triggerOnDraw(float deltatime);
    void triggerOnInput(int key);

    // SOCKS5 Proxy Integration
    void setProxyCallbacks(std::function<Socks5Config()> get_fn,
                           std::function<void(const Socks5Config&)> set_fn) {
        get_proxy_fn_ = get_fn;
        set_proxy_fn_ = set_fn;
    }
    Socks5Config getProxy() const;
    void setProxy(const Socks5Config& p);
    void setStats(BotStats* stats) { stats_ = stats; }

private:
    void workerThread(std::string code);
    void registerLuciferApi(lua_State* L);
    void registerBothaxApi(lua_State* L);

    // Global / Common Callbacks
    static int luaSleep(lua_State* L);
    static int luaLog(lua_State* L);

    // Bothax Global API Callbacks
    static int bothaxAddHook(lua_State* L);
    static int bothaxRemoveHook(lua_State* L);
    static int bothaxRemoveHooks(lua_State* L);
    static int bothaxChangeValue(lua_State* L);
    static int bothaxCheckPath(lua_State* L);
    static int bothaxEncrypt(lua_State* L);
    static int bothaxEncryptFile(lua_State* L);
    static int bothaxLoadEncrypt(lua_State* L);
    static int bothaxLoadEncryptedFile(lua_State* L);
    static int bothaxFindPath(lua_State* L);
    static int bothaxGetCamera(lua_State* L);
    static int bothaxGetClient(lua_State* L);
    static int bothaxGetInventory(lua_State* L);
    static int bothaxGetItemByIDSafe(lua_State* L);
    static int bothaxGetItemByName(lua_State* L);
    static int bothaxGetItemInfo(lua_State* L);
    static int bothaxGetItemInfoList(lua_State* L);
    static int bothaxGetItemsByPartialName(lua_State* L);
    static int bothaxGetLocal(lua_State* L);
    static int bothaxGetNPC(lua_State* L);
    static int bothaxGetNPCList(lua_State* L);
    static int bothaxGetObjectList(lua_State* L);
    static int bothaxGetPlayer(lua_State* L);
    static int bothaxGetPlayerInfo(lua_State* L);
    static int bothaxGetPlayerList(lua_State* L);
    static int bothaxGetTile(lua_State* L);
    static int bothaxGetTiles(lua_State* L);
    static int bothaxGetWorld(lua_State* L);
    static int bothaxHash32(lua_State* L);
    static int bothaxHash64(lua_State* L);
    static int bothaxMakeRequest(lua_State* L);
    static int bothaxRequestJoinWorld(lua_State* L);
    static int bothaxRunDelayed(lua_State* L);
    static int bothaxRunThread(lua_State* L);
    static int bothaxSendPacket(lua_State* L);
    static int bothaxSendPacketRaw(lua_State* L);
    static int bothaxSendVariantList(lua_State* L);
    static int bothaxSetItemSelected(lua_State* L);
    static int bothaxSetTileFlags(lua_State* L);

    // Lucifer Bot Callbacks
    static int luaGetBot(lua_State* L);
    static int luaGetBots(lua_State* L);
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
    static int botGetPlaytime(lua_State* L);
    static int botGetAge(lua_State* L);
    static int botGetActiveTime(lua_State* L);
    static int botGetLevel(lua_State* L);
    static int botGetGems(lua_State* L);
    static int botWrenchPlayer(lua_State* L);
    static int luaSetProxy(lua_State* L);
    static int luaGetProxy(lua_State* L);
    static int luaTestProxy(lua_State* L);

    // Internal Structure Helpers
    static void pushNetAvatar(lua_State* L, const std::string& name, int netid, int userid,
                              const std::string& country, float x, float y);
    static void pushTile(lua_State* L, int x, int y, uint16_t fg, uint16_t bg);
    static void pushItemInfo(lua_State* L, int id, const std::string& name, int rarity, int type, int coltype);

    BotActions& actions_;
    WorldState& world_;
    EnetClient& enet_;
    BotStats* stats_{nullptr};

    std::function<Socks5Config()> get_proxy_fn_;
    std::function<void(const Socks5Config&)> set_proxy_fn_;
    Socks5Config local_proxy_{};

    std::atomic<bool> is_running_{false};
    std::atomic<bool> stop_requested_{false};
    std::string last_error_;
    mutable std::mutex error_mtx_;
    std::thread worker_;
    std::chrono::steady_clock::time_point start_time_;

    // Mutex & handle untuk eksekusi Lua dan hook dispatching
    mutable std::mutex lua_mtx_;
    lua_State* active_L_ = nullptr;
    std::unordered_map<std::string, std::string> settings_store_;
};
