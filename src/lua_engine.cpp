#include "lua_engine.hpp"
#include "bot_actions.hpp"
#include "world.hpp"
#include "enet_client.hpp"
#include "game_packet.hpp"
#include "packet.hpp"
#include "logger.hpp"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

#include <chrono>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <cmath>

// ============================================================
// Built-in Item Database (Common Growtopia Items & Fallback)
// ============================================================
struct BuiltinItem {
    int id;
    const char* name;
    int rarity;
    int type;
    int coltype;
};

static const BuiltinItem G_BUILTIN_ITEMS[] = {
    { 0, "Fist", 0, 0, 0 },
    { 2, "Dirt", 1, 0, 1 },
    { 3, "Dirt Seed", 1, 1, 0 },
    { 4, "Lava", 2, 0, 1 },
    { 5, "Lava Seed", 2, 1, 0 },
    { 6, "Main Door", 0, 0, 1 },
    { 8, "Bedrock", 999, 0, 1 },
    { 10, "Rock", 1, 0, 1 },
    { 11, "Rock Seed", 1, 1, 0 },
    { 12, "Door", 1, 0, 1 },
    { 13, "Door Seed", 1, 1, 0 },
    { 14, "Cave Background", 1, 0, 0 },
    { 15, "Cave Background Seed", 1, 1, 0 },
    { 18, "Wrench", 0, 0, 0 },
    { 20, "Sign", 1, 0, 1 },
    { 22, "Wooden Block", 2, 0, 1 },
    { 23, "Wooden Block Seed", 2, 1, 0 },
    { 26, "Daisy", 2, 0, 0 },
    { 28, "Pointy Sign", 2, 0, 1 },
    { 30, "Wood Wall", 2, 0, 0 },
    { 32, "Glass Pane", 3, 0, 1 },
    { 34, "Iron Block", 4, 0, 1 },
    { 36, "Brick", 3, 0, 1 },
    { 48, "Mushroom", 3, 0, 1 },
    { 50, "Cargo Shortcut", 10, 0, 1 },
    { 52, "Torch", 2, 0, 0 },
    { 96, "Display Block", 5, 0, 1 },
    { 224, "Red Block", 2, 0, 1 },
    { 226, "Green Block", 2, 0, 1 },
    { 228, "Blue Block", 2, 0, 1 },
    { 242, "World Lock", 10, 0, 1 },
    { 244, "Small Lock", 1, 0, 1 },
    { 246, "Big Lock", 3, 0, 1 },
    { 248, "Huge Lock", 5, 0, 1 },
    { 456, "Wood Platform", 2, 0, 2 },
    { 458, "Wood Platform Seed", 2, 1, 0 },
    { 542, "Robotic Lock", 20, 0, 1 },
    { 564, "Ruby", 15, 0, 0 },
    { 566, "Emerald", 15, 0, 0 },
    { 568, "Sapphire", 15, 0, 0 },
    { 822, "Laser Grid", 25, 0, 1 },
    { 824, "Laser Grid Seed", 25, 1, 0 },
    { 1450, "Pepper Tree", 35, 0, 1 },
    { 1452, "Pepper Tree Seed", 35, 1, 0 },
    { 1796, "Diamond Lock", 100, 0, 1 },
    { 3004, "Magplant 5000", 250, 0, 1 },
    { 3212, "Gaia's Beacon", 250, 0, 1 },
    { 3418, "Unstable Tesseract", 250, 0, 1 },
    { 4584, "Rayman's Fist", 500, 0, 0 },
    { 5260, "ATM Machine", 80, 0, 1 },
    { 5262, "ATM Machine Seed", 80, 1, 0 },
    { 7004, "Ancestral Lens of Riches", 400, 0, 0 },
    { 7188, "Blue Gem Lock", 1000, 0, 1 },
    { 11550, "Royal Lock", 2000, 0, 1 }
};

static std::string toLowerStr(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower;
}

static void luaHook(lua_State* L, lua_Debug* /*ar*/) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (self && !self->isRunning()) {
        luaL_error(L, "Script execution stopped by user.");
    }
}

// ============================================================
// Constructor & Destructor
// ============================================================
LuaEngine::LuaEngine(BotActions& actions, WorldState& world, EnetClient& enet)
    : actions_(actions), world_(world), enet_(enet) {}

LuaEngine::~LuaEngine() {
    stopScript();
}

bool LuaEngine::isRunning() const {
    return is_running_.load();
}

std::string LuaEngine::getLastError() const {
    std::lock_guard<std::mutex> lock(error_mtx_);
    return last_error_;
}

int LuaEngine::getExecutionTimeMs() const {
    if (!is_running_.load()) return 0;
    auto now = std::chrono::steady_clock::now();
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count());
}

void LuaEngine::stopScript() {
    stop_requested_ = true;
    is_running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
    std::lock_guard<std::mutex> lock(lua_mtx_);
    active_L_ = nullptr;
}

bool LuaEngine::runScript(const std::string& script_code) {
    if (is_running_.load()) {
        stopScript();
    }

    stop_requested_ = false;
    is_running_ = true;
    start_time_ = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(error_mtx_);
        last_error_.clear();
    }

    worker_ = std::thread(&LuaEngine::workerThread, this, script_code);
    return true;
}

void LuaEngine::workerThread(std::string code) {
    Logger::log("[LUA] 🚀 Memulai eksekusi script Lua (Bothax & Lucifer Engine)...");

    lua_State* L = luaL_newstate();
    if (!L) {
        std::lock_guard<std::mutex> lock(error_mtx_);
        last_error_ = "Gagal mengalokasikan Lua state memory.";
        is_running_ = false;
        return;
    }

    luaL_openlibs(L);

    {
        std::lock_guard<std::mutex> lock(lua_mtx_);
        active_L_ = L;
    }

    // Simpan instance pointer di registry
    lua_pushlightuserdata(L, this);
    lua_setfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");

    // Inisialisasi table hooks di registry
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "__BOTHAX_HOOKS");

    // Hook untuk mengecek interupsi stop
    lua_sethook(L, luaHook, LUA_MASKCOUNT, 1000);

    // Daftarkan semua Bothax API dan Lucifer API
    registerBothaxApi(L);
    registerLuciferApi(L);

    int status = LUA_OK;
    {
        std::lock_guard<std::mutex> lock(lua_mtx_);
        status = luaL_dostring(L, code.c_str());
    }

    if (status != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        std::string err_str = err ? err : "Unknown Lua error";
        {
            std::lock_guard<std::mutex> lock(error_mtx_);
            last_error_ = err_str;
        }
        Logger::log("[LUA] ❌ Error script: ", err_str);
        is_running_ = false;
    } else {
        Logger::log("[LUA] ✅ Main script chunk selesai dieksekusi.");

        // Jika ada hooks terdaftar atau script dirancang event-driven,
        // pertahankan Lua state tetap hidup sampai dihentikan pengguna.
        bool has_hooks = false;
        {
            std::lock_guard<std::mutex> lock(lua_mtx_);
            lua_getfield(L, LUA_REGISTRYINDEX, "__BOTHAX_HOOKS");
            if (lua_istable(L, -1)) {
                lua_pushnil(L);
                if (lua_next(L, -2) != 0) {
                    has_hooks = true;
                    lua_pop(L, 2);
                }
            }
            lua_pop(L, 1);
        }

        if (has_hooks) {
            Logger::log("[LUA] 🔄 Script mendengarkan event hooks di background.");
            while (is_running_.load() && !stop_requested_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } else {
            is_running_ = false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(lua_mtx_);
        active_L_ = nullptr;
    }

    lua_close(L);
    is_running_ = false;
    Logger::log("[LUA] ⏹ Script engine idle.");
}

// ============================================================
// Helper Pushing Structure Table
// ============================================================
void LuaEngine::pushNetAvatar(lua_State* L, const std::string& name, int netid, int userid,
                              const std::string& country, float x, float y) {
    lua_newtable(L);

    // pos = { x, y }
    lua_newtable(L);
    lua_pushnumber(L, x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, y);
    lua_setfield(L, -2, "y");
    lua_setfield(L, -2, "pos");

    lua_pushstring(L, name.c_str());
    lua_setfield(L, -2, "name");

    lua_pushboolean(L, false);
    lua_setfield(L, -2, "isleft");

    lua_pushinteger(L, netid);
    lua_setfield(L, -2, "netid");

    lua_pushinteger(L, userid);
    lua_setfield(L, -2, "userid");

    lua_pushstring(L, country.c_str());
    lua_setfield(L, -2, "country");

    lua_pushboolean(L, false);
    lua_setfield(L, -2, "invisible");

    lua_pushboolean(L, false);
    lua_setfield(L, -2, "mstate");

    lua_pushboolean(L, false);
    lua_setfield(L, -2, "smstate");

    // Convenience fields
    lua_pushnumber(L, x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, static_cast<int>(x / 32.0f));
    lua_setfield(L, -2, "tile_x");
    lua_pushinteger(L, static_cast<int>(y / 32.0f));
    lua_setfield(L, -2, "tile_y");
}

void LuaEngine::pushTile(lua_State* L, int x, int y, uint16_t fg, uint16_t bg) {
    lua_newtable(L);

    lua_pushinteger(L, fg);
    lua_setfield(L, -2, "fg");

    lua_pushinteger(L, bg);
    lua_setfield(L, -2, "bg");

    bool collidable = (fg != 0);
    lua_pushboolean(L, collidable);
    lua_setfield(L, -2, "collidable");

    lua_pushinteger(L, x);
    lua_setfield(L, -2, "x");

    lua_pushinteger(L, y);
    lua_setfield(L, -2, "y");

    // pos = { x, y } in pixels
    lua_newtable(L);
    lua_pushinteger(L, x * 32);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, y * 32);
    lua_setfield(L, -2, "y");
    lua_setfield(L, -2, "pos");

    lua_pushinteger(L, fg > 0 ? 1 : 0);
    lua_setfield(L, -2, "coltype");

    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "locktile");

    // flags (TileFlags structure)
    lua_newtable(L);
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "value");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "locked");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "spliced");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "dropseed");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "tree");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "flipped");
    lua_pushboolean(L, true);
    lua_setfield(L, -2, "enabled");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "public");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "silenced");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "water");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "glue");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "burn");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "red");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "green");
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "blue");
    lua_setfield(L, -2, "flags");

    lua_pushnil(L);
    lua_setfield(L, -2, "extra");
}

void LuaEngine::pushItemInfo(lua_State* L, int id, const std::string& name, int rarity, int type, int coltype) {
    lua_newtable(L);
    lua_pushinteger(L, id);
    lua_setfield(L, -2, "id");
    lua_pushstring(L, name.c_str());
    lua_setfield(L, -2, "name");
    std::string fname = "item_" + std::to_string(id) + ".png";
    lua_pushstring(L, fname.c_str());
    lua_setfield(L, -2, "filename");
    lua_pushinteger(L, rarity);
    lua_setfield(L, -2, "rarity");
    lua_pushinteger(L, 4);
    lua_setfield(L, -2, "breakhit");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "growtime");
    lua_pushinteger(L, type);
    lua_setfield(L, -2, "type");
    lua_pushinteger(L, coltype);
    lua_setfield(L, -2, "coltype");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "clothingtype");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "visualstyle");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "texturex");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "texturey");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "flags");
}

// ============================================================
// Registration: Bothax & Lucifer APIs
// ============================================================
void LuaEngine::registerBothaxApi(lua_State* L) {
    // 1. Bothax Logging & Timing
    lua_pushcfunction(L, luaLog);
    lua_setglobal(L, "LogToConsole");
    lua_pushcfunction(L, luaLog);
    lua_setglobal(L, "log");
    lua_pushcfunction(L, luaLog);
    lua_setglobal(L, "print");

    lua_pushcfunction(L, luaSleep);
    lua_setglobal(L, "Sleep");
    lua_pushcfunction(L, luaSleep);
    lua_setglobal(L, "sleep");

    // 2. Bothax Hook Management
    lua_pushcfunction(L, bothaxAddHook);
    lua_setglobal(L, "AddHook");
    lua_pushcfunction(L, bothaxRemoveHook);
    lua_setglobal(L, "RemoveHook");
    lua_pushcfunction(L, bothaxRemoveHooks);
    lua_setglobal(L, "RemoveHooks");

    // BothaxHook Constants Table
    lua_newtable(L);
    lua_pushstring(L, "OnVariant");
    lua_setfield(L, -2, "OnVariant");
    lua_pushstring(L, "OnSendPacket");
    lua_setfield(L, -2, "OnSendPacket");
    lua_pushstring(L, "OnSendPacketRaw");
    lua_setfield(L, -2, "OnSendPacketRaw");
    lua_pushstring(L, "OnProcessTankUpdate");
    lua_setfield(L, -2, "OnProcessTankUpdate");
    lua_pushstring(L, "OnWorldTouch");
    lua_setfield(L, -2, "OnWorldTouch");
    lua_pushstring(L, "OnDraw");
    lua_setfield(L, -2, "OnDraw");
    lua_pushstring(L, "OnInput");
    lua_setfield(L, -2, "OnInput");
    lua_setglobal(L, "BothaxHook");

    // Minimal ImGui Table for Lua Scripts (OnDraw hook compatibility)
    luaL_dostring(L,
        "ImGui = {\n"
        "  Begin = function(title) return true end,\n"
        "  End = function() end,\n"
        "  Text = function(text) LogToConsole(text) end,\n"
        "  Button = function(label) return false end,\n"
        "  SameLine = function() end,\n"
        "  Separator = function() end\n"
        "}\n"
    );

    // 3. Bothax Configuration & Movement
    lua_pushcfunction(L, bothaxChangeValue);
    lua_setglobal(L, "ChangeValue");
    lua_pushcfunction(L, bothaxCheckPath);
    lua_setglobal(L, "CheckPath");
    lua_pushcfunction(L, bothaxCheckPath);
    lua_setglobal(L, "checkPath");
    lua_pushcfunction(L, bothaxFindPath);
    lua_setglobal(L, "FindPath");

    // 4. Encryption & Hashing
    lua_pushcfunction(L, bothaxEncrypt);
    lua_setglobal(L, "Encrypt");
    lua_pushcfunction(L, bothaxEncryptFile);
    lua_setglobal(L, "EncryptFile");
    lua_pushcfunction(L, bothaxLoadEncrypt);
    lua_setglobal(L, "LoadEncrypt");
    lua_pushcfunction(L, bothaxLoadEncryptedFile);
    lua_setglobal(L, "LoadEncryptedFile");
    lua_pushcfunction(L, bothaxHash32);
    lua_setglobal(L, "Hash32");
    lua_pushcfunction(L, bothaxHash64);
    lua_setglobal(L, "Hash64");

    // 5. World, Entities & Player Queries
    lua_pushcfunction(L, bothaxGetCamera);
    lua_setglobal(L, "GetCamera");
    lua_pushcfunction(L, bothaxGetClient);
    lua_setglobal(L, "GetClient");
    lua_pushcfunction(L, bothaxGetInventory);
    lua_setglobal(L, "GetInventory");
    lua_pushcfunction(L, bothaxGetLocal);
    lua_setglobal(L, "GetLocal");
    lua_pushcfunction(L, bothaxGetNPC);
    lua_setglobal(L, "GetNPC");
    lua_pushcfunction(L, bothaxGetNPCList);
    lua_setglobal(L, "GetNPCList");
    lua_pushcfunction(L, bothaxGetObjectList);
    lua_setglobal(L, "GetObjectList");
    lua_pushcfunction(L, bothaxGetPlayer);
    lua_setglobal(L, "GetPlayer");
    lua_pushcfunction(L, bothaxGetPlayerInfo);
    lua_setglobal(L, "GetPlayerInfo");
    lua_pushcfunction(L, bothaxGetPlayerInfo);
    lua_setglobal(L, "GetPlayerItems");
    lua_pushcfunction(L, bothaxGetPlayerList);
    lua_setglobal(L, "GetPlayerList");
    lua_pushcfunction(L, bothaxGetTile);
    lua_setglobal(L, "GetTile");
    lua_pushcfunction(L, bothaxGetTiles);
    lua_setglobal(L, "GetTiles");
    lua_pushcfunction(L, bothaxGetWorld);
    lua_setglobal(L, "GetWorld");

    // 6. Item Database Queries
    lua_pushcfunction(L, bothaxGetItemByIDSafe);
    lua_setglobal(L, "GetItemByIDSafe");
    lua_pushcfunction(L, bothaxGetItemByName);
    lua_setglobal(L, "GetItemByName");
    lua_pushcfunction(L, bothaxGetItemInfo);
    lua_setglobal(L, "GetItemInfo");
    lua_pushcfunction(L, bothaxGetItemInfoList);
    lua_setglobal(L, "GetItemInfoList");
    lua_pushcfunction(L, bothaxGetItemsByPartialName);
    lua_setglobal(L, "GetItemsByPartialName");

    // 7. Network, Packets & World Interaction
    lua_pushcfunction(L, bothaxSendPacket);
    lua_setglobal(L, "SendPacket");
    lua_pushcfunction(L, bothaxSendPacketRaw);
    lua_setglobal(L, "SendPacketRaw");
    lua_pushcfunction(L, bothaxSendVariantList);
    lua_setglobal(L, "SendVariantList");
    lua_pushcfunction(L, bothaxRequestJoinWorld);
    lua_setglobal(L, "RequestJoinWorld");
    lua_pushcfunction(L, bothaxSetItemSelected);
    lua_setglobal(L, "SetItemSelected");
    lua_pushcfunction(L, bothaxSetTileFlags);
    lua_setglobal(L, "SetTileFlags");

    // 8. Threading & HTTP
    lua_pushcfunction(L, bothaxMakeRequest);
    lua_setglobal(L, "MakeRequest");
    lua_pushcfunction(L, bothaxRunDelayed);
    lua_setglobal(L, "RunDelayed");
    lua_pushcfunction(L, bothaxRunThread);
    lua_setglobal(L, "RunThread");
}

void LuaEngine::registerLuciferApi(lua_State* L) {
    lua_pushcfunction(L, luaGetBot);
    lua_setglobal(L, "getBot");
    lua_pushcfunction(L, luaGetBots);
    lua_setglobal(L, "getBots");

    lua_newtable(L); // bot table
    lua_pushcfunction(L, botSay);
    lua_setfield(L, -2, "say");
    lua_pushcfunction(L, botWarp);
    lua_setfield(L, -2, "warp");
    lua_pushcfunction(L, botMoveTo);
    lua_setfield(L, -2, "moveTo");
    lua_pushcfunction(L, botHit);
    lua_setfield(L, -2, "hit");
    lua_pushcfunction(L, botPlace);
    lua_setfield(L, -2, "place");
    lua_pushcfunction(L, botWrench);
    lua_setfield(L, -2, "wrench");
    lua_pushcfunction(L, botEnter);
    lua_setfield(L, -2, "enter");
    lua_pushcfunction(L, botCollect);
    lua_setfield(L, -2, "collect");
    lua_pushcfunction(L, botDrop);
    lua_setfield(L, -2, "drop");
    lua_pushcfunction(L, botTrash);
    lua_setfield(L, -2, "trash");
    lua_pushcfunction(L, botRespawn);
    lua_setfield(L, -2, "respawn");
    lua_pushcfunction(L, botGetLocal);
    lua_setfield(L, -2, "getLocal");
    lua_pushcfunction(L, botGetWorld);
    lua_setfield(L, -2, "getWorld");
    lua_pushcfunction(L, botGetInventory);
    lua_setfield(L, -2, "getInventory");
    lua_pushcfunction(L, botGetPlayers);
    lua_setfield(L, -2, "getPlayers");
    lua_pushcfunction(L, botFindItem);
    lua_setfield(L, -2, "findItem");

    lua_pushvalue(L, -1);
    lua_setglobal(L, "bot");
    lua_setglobal(L, "Lucifer");
}

// ============================================================
// Bothax Global API Implementations
// ============================================================

int LuaEngine::luaSleep(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    int ms = static_cast<int>(luaL_optinteger(L, 1, 100));
    int elapsed = 0;
    while (elapsed < ms && self && self->is_running_.load()) {
        int step = (std::min)(20, ms - elapsed);
        std::this_thread::sleep_for(std::chrono::milliseconds(step));
        elapsed += step;
    }
    if (self && !self->is_running_.load()) {
        luaL_error(L, "Script execution stopped by user.");
    }
    return 0;
}

int LuaEngine::luaLog(lua_State* L) {
    int n = lua_gettop(L);
    std::ostringstream ss;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) ss << " ";
        if (lua_isstring(L, i)) {
            ss << lua_tostring(L, i);
        } else if (lua_isboolean(L, i)) {
            ss << (lua_toboolean(L, i) ? "true" : "false");
        } else if (lua_isnumber(L, i)) {
            ss << lua_tonumber(L, i);
        } else if (lua_isnil(L, i)) {
            ss << "nil";
        } else {
            ss << lua_typename(L, lua_type(L, i));
        }
    }
    Logger::log("[Bothax] ", ss.str());
    return 0;
}

int LuaEngine::bothaxAddHook(lua_State* L) {
    const char* hook_type = luaL_checkstring(L, 1);
    std::string label;
    if (lua_isnumber(L, 2)) {
        label = std::to_string(lua_tointeger(L, 2));
    } else {
        label = luaL_checkstring(L, 2);
    }
    luaL_checktype(L, 3, LUA_TFUNCTION);

    lua_getfield(L, LUA_REGISTRYINDEX, "__BOTHAX_HOOKS");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, "__BOTHAX_HOOKS");
        lua_getfield(L, LUA_REGISTRYINDEX, "__BOTHAX_HOOKS");
    }

    lua_getfield(L, -1, hook_type);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, hook_type);
    }

    lua_pushvalue(L, 3); // callback function
    lua_setfield(L, -2, label.c_str());
    lua_pop(L, 2); // pop hook category table & __BOTHAX_HOOKS table

    Logger::log("[Bothax] 🔗 Hook ditambahkan: ", hook_type, " [Label: ", label, "]");
    return 0;
}

int LuaEngine::bothaxRemoveHook(lua_State* L) {
    std::string label;
    if (lua_isnumber(L, 1)) {
        label = std::to_string(lua_tointeger(L, 1));
    } else {
        label = luaL_checkstring(L, 1);
    }

    lua_getfield(L, LUA_REGISTRYINDEX, "__BOTHAX_HOOKS");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_istable(L, -1)) {
                lua_pushnil(L);
                lua_setfield(L, -2, label.c_str());
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    Logger::log("[Bothax] ✂️ Hook dihapus: [Label: ", label, "]");
    return 0;
}

int LuaEngine::bothaxRemoveHooks(lua_State* L) {
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "__BOTHAX_HOOKS");
    Logger::log("[Bothax] 🧹 Semua hooks berhasil dibersihkan.");
    return 0;
}

int LuaEngine::bothaxChangeValue(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    const char* name = luaL_checkstring(L, 1);
    std::string val_str;
    if (lua_isboolean(L, 2)) {
        val_str = lua_toboolean(L, 2) ? "true" : "false";
    } else if (lua_isnumber(L, 2)) {
        val_str = std::to_string(lua_tonumber(L, 2));
    } else if (lua_isstring(L, 2)) {
        val_str = lua_tostring(L, 2);
    }

    if (self) {
        self->settings_store_[name] = val_str;
    }
    Logger::log("[Bothax] ⚙️ ChangeValue: ", name, " -> ", val_str);
    return 0;
}

int LuaEngine::bothaxCheckPath(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    int tx = static_cast<int>(luaL_checkinteger(L, 1));
    int ty = static_cast<int>(luaL_checkinteger(L, 2));

    if (!self) {
        lua_pushboolean(L, false);
        return 1;
    }

    int w = self->world_.getWidth();
    int h = self->world_.getHeight();
    if (tx < 0 || tx >= w || ty < 0 || ty >= h) {
        lua_pushboolean(L, false);
        return 1;
    }

    uint16_t fg = 0, bg = 0;
    if (self->world_.getTile(tx, ty, fg, bg)) {
        // Bedrock (8) atau solid blocks umum
        bool solid = (fg == 8);
        lua_pushboolean(L, !solid);
    } else {
        lua_pushboolean(L, true);
    }
    return 1;
}

int LuaEngine::bothaxFindPath(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    int tx = static_cast<int>(luaL_checkinteger(L, 1));
    int ty = static_cast<int>(luaL_checkinteger(L, 2));

    if (self) {
        self->actions_.moveTo(tx, ty, 35);
    }
    return 0;
}

int LuaEngine::bothaxGetCamera(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    float px = 0, py = 0;
    int tx = 0, ty = 0;
    if (self) {
        self->world_.getLocalPos(px, py, tx, ty);
    }

    lua_newtable(L);

    // pos
    lua_newtable(L);
    lua_pushnumber(L, px);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, py);
    lua_setfield(L, -2, "y");
    lua_setfield(L, -2, "pos");

    // center
    lua_newtable(L);
    lua_pushnumber(L, px);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, py);
    lua_setfield(L, -2, "y");
    lua_setfield(L, -2, "center");

    lua_pushnumber(L, 1.0);
    lua_setfield(L, -2, "scale");

    // resolution
    lua_newtable(L);
    lua_pushnumber(L, 1024.0);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, 768.0);
    lua_setfield(L, -2, "y");
    lua_setfield(L, -2, "resolution");

    return 1;
}

int LuaEngine::bothaxGetClient(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    lua_newtable(L);
    if (self) {
        lua_pushstring(L, self->enet_.getHost().c_str());
        lua_setfield(L, -2, "address");
        lua_pushinteger(L, self->enet_.getPort());
        lua_setfield(L, -2, "port");
        lua_pushinteger(L, self->enet_.getPing());
        lua_setfield(L, -2, "ping");
    } else {
        lua_pushstring(L, "127.0.0.1");
        lua_setfield(L, -2, "address");
        lua_pushinteger(L, 17091);
        lua_setfield(L, -2, "port");
        lua_pushinteger(L, 15);
        lua_setfield(L, -2, "ping");
    }
    return 1;
}

int LuaEngine::bothaxGetInventory(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    lua_newtable(L);
    if (!self) return 1;

    auto items = self->world_.getInventory();
    for (size_t i = 0; i < items.size(); ++i) {
        lua_newtable(L);
        lua_pushinteger(L, items[i].id);
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, items[i].count);
        lua_setfield(L, -2, "amount");
        lua_pushinteger(L, items[i].count);
        lua_setfield(L, -2, "count"); // alias
        lua_pushinteger(L, items[i].flags);
        lua_setfield(L, -2, "flags");
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

int LuaEngine::bothaxGetItemByIDSafe(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    for (const auto& item : G_BUILTIN_ITEMS) {
        if (item.id == id) {
            pushItemInfo(L, item.id, item.name, item.rarity, item.type, item.coltype);
            return 1;
        }
    }
    pushItemInfo(L, id, "Item #" + std::to_string(id), 1, 0, (id % 2 == 0 ? 1 : 0));
    return 1;
}

int LuaEngine::bothaxGetItemByName(lua_State* L) {
    std::string name = luaL_checkstring(L, 1);
    std::string lower = toLowerStr(name);

    for (const auto& item : G_BUILTIN_ITEMS) {
        if (toLowerStr(item.name) == lower) {
            pushItemInfo(L, item.id, item.name, item.rarity, item.type, item.coltype);
            return 1;
        }
    }
    pushItemInfo(L, 9999, name, 1, 0, 1);
    return 1;
}

int LuaEngine::bothaxGetItemInfo(lua_State* L) {
    if (lua_isnumber(L, 1)) {
        return bothaxGetItemByIDSafe(L);
    }
    return bothaxGetItemByName(L);
}

int LuaEngine::bothaxGetItemInfoList(lua_State* L) {
    lua_newtable(L);
    int idx = 1;
    for (const auto& item : G_BUILTIN_ITEMS) {
        pushItemInfo(L, item.id, item.name, item.rarity, item.type, item.coltype);
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

int LuaEngine::bothaxGetItemsByPartialName(lua_State* L) {
    std::string query = luaL_checkstring(L, 1);
    std::string query_lower = toLowerStr(query);

    lua_newtable(L);
    int idx = 1;
    for (const auto& item : G_BUILTIN_ITEMS) {
        if (toLowerStr(item.name).find(query_lower) != std::string::npos) {
            pushItemInfo(L, item.id, item.name, item.rarity, item.type, item.coltype);
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

int LuaEngine::bothaxGetLocal(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    if (!self) {
        pushNetAvatar(L, "Bot", 0, 0, "id", 0.0f, 0.0f);
        return 1;
    }

    auto p = self->world_.getLocalPlayer();
    pushNetAvatar(L, p.name.empty() ? "Bot" : WorldState::stripColorCodes(p.name),
                  p.net_id, p.user_id, p.country.empty() ? "id" : p.country, p.x, p.y);
    return 1;
}

int LuaEngine::bothaxGetNPC(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    lua_newtable(L);
    lua_pushinteger(L, id);
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "type");

    lua_newtable(L);
    lua_pushnumber(L, 0.0);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, 0.0);
    lua_setfield(L, -2, "y");
    lua_setfield(L, -2, "pos");

    lua_newtable(L);
    lua_pushnumber(L, 0.0);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, 0.0);
    lua_setfield(L, -2, "y");
    lua_setfield(L, -2, "target");
    return 1;
}

int LuaEngine::bothaxGetNPCList(lua_State* L) {
    lua_newtable(L);
    return 1;
}

int LuaEngine::bothaxGetObjectList(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    lua_newtable(L);
    if (!self) return 1;

    auto items = self->world_.getDroppedItems();
    for (size_t i = 0; i < items.size(); ++i) {
        lua_newtable(L);
        lua_pushinteger(L, items[i].id);
        lua_setfield(L, -2, "id");

        lua_newtable(L);
        lua_pushnumber(L, items[i].x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, items[i].y);
        lua_setfield(L, -2, "y");
        lua_setfield(L, -2, "pos");

        lua_pushinteger(L, items[i].count);
        lua_setfield(L, -2, "amount");
        lua_pushinteger(L, 0);
        lua_setfield(L, -2, "flags");
        lua_pushinteger(L, items[i].uid);
        lua_setfield(L, -2, "oid");

        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

int LuaEngine::bothaxGetPlayer(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    int netid = static_cast<int>(luaL_checkinteger(L, 1));
    if (!self) {
        lua_pushnil(L);
        return 1;
    }

    auto players = self->world_.getPlayers();
    for (const auto& p : players) {
        if (p.net_id == netid) {
            pushNetAvatar(L, WorldState::stripColorCodes(p.name), p.net_id, p.user_id, p.country, p.x, p.y);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

int LuaEngine::bothaxGetPlayerInfo(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    uint32_t slots = self ? self->world_.getInventorySlots() : 16;

    lua_newtable(L);
    lua_pushinteger(L, 0); // gems
    lua_setfield(L, -2, "gems");

    // backpack
    lua_newtable(L);
    lua_newtable(L);
    lua_setfield(L, -2, "priority");
    lua_pushinteger(L, slots);
    lua_setfield(L, -2, "size");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "selected");
    lua_setfield(L, -2, "backpack");

    return 1;
}

int LuaEngine::bothaxGetPlayerList(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    lua_newtable(L);
    if (!self) return 1;

    auto players = self->world_.getPlayers();
    int idx = 1;
    for (const auto& p : players) {
        if (p.is_local) continue; // Exclude local player as specified in Bothax docs
        pushNetAvatar(L, WorldState::stripColorCodes(p.name), p.net_id, p.user_id, p.country, p.x, p.y);
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

int LuaEngine::bothaxGetTile(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    int x = static_cast<int>(luaL_checkinteger(L, 1));
    int y = static_cast<int>(luaL_checkinteger(L, 2));

    uint16_t fg = 0, bg = 0;
    if (self) {
        self->world_.getTile(x, y, fg, bg);
    }
    pushTile(L, x, y, fg, bg);
    return 1;
}

int LuaEngine::bothaxGetTiles(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    lua_newtable(L);
    if (!self) return 1;

    int w = self->world_.getWidth();
    auto tiles = self->world_.getAllTiles();
    for (size_t i = 0; i < tiles.size(); ++i) {
        int tx = static_cast<int>(i % w);
        int ty = static_cast<int>(i / w);
        pushTile(L, tx, ty, tiles[i].fg, tiles[i].bg);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

int LuaEngine::bothaxGetWorld(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    lua_newtable(L);
    if (!self) return 1;

    int w = self->world_.getWidth();
    int h = self->world_.getHeight();
    lua_pushstring(L, self->world_.getWorldName().c_str());
    lua_setfield(L, -2, "name");
    lua_pushinteger(L, w);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, h);
    lua_setfield(L, -2, "height");
    lua_pushinteger(L, w * h);
    lua_setfield(L, -2, "tilecount");
    lua_pushinteger(L, self->world_.getDroppedItems().size());
    lua_setfield(L, -2, "objectcount");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "lastoid");

    return 1;
}

int LuaEngine::bothaxHash32(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    uint32_t hash = static_cast<uint32_t>(luaL_optinteger(L, 2, 0x811c9dc5));
    if (hash == 0) hash = 0x811c9dc5;
    while (*text) {
        hash ^= static_cast<uint8_t>(*text++);
        hash *= 0x01000193;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(hash));
    return 1;
}

int LuaEngine::bothaxHash64(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    uint64_t hash = static_cast<uint64_t>(luaL_optinteger(L, 2, 0xcbf29ce484222325ULL));
    if (hash == 0) hash = 0xcbf29ce484222325ULL;
    while (*text) {
        hash ^= static_cast<uint8_t>(*text++);
        hash *= 0x100000001b3ULL;
    }
    lua_pushnumber(L, static_cast<lua_Number>(hash));
    return 1;
}

static std::string bothaxXor(const std::string& input, int key) {
    std::string out = input;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] ^= static_cast<char>((key + static_cast<int>(i)) & 0xFF);
    }
    return out;
}

int LuaEngine::bothaxEncrypt(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    int key = static_cast<int>(luaL_optinteger(L, 2, 12345));

    std::string enc = bothaxXor(text, key);
    // Convert to hex
    std::ostringstream ss;
    for (unsigned char c : enc) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    }
    lua_pushstring(L, ss.str().c_str());
    return 1;
}

int LuaEngine::bothaxEncryptFile(lua_State* L) {
    const char* filename = luaL_checkstring(L, 1);
    int key = static_cast<int>(luaL_optinteger(L, 2, 12345));

    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open()) return 0;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::string enc = bothaxXor(content, key);
    std::string outname = std::string(filename) + "_enc";
    std::ofstream out(outname, std::ios::binary);
    if (out.is_open()) {
        out.write(enc.data(), enc.size());
        out.close();
    }
    return 0;
}

int LuaEngine::bothaxLoadEncrypt(lua_State* L) {
    const char* hex = luaL_checkstring(L, 1);
    int key = static_cast<int>(luaL_optinteger(L, 2, 12345));

    std::string raw;
    size_t len = strlen(hex);
    for (size_t i = 0; i + 1 < len; i += 2) {
        std::string byteString = std::string(hex + i, 2);
        char byte = (char)strtol(byteString.c_str(), NULL, 16);
        raw.push_back(byte);
    }

    std::string dec = bothaxXor(raw, key);
    luaL_dostring(L, dec.c_str());
    return 0;
}

int LuaEngine::bothaxLoadEncryptedFile(lua_State* L) {
    const char* filename = luaL_checkstring(L, 1);
    int key = static_cast<int>(luaL_optinteger(L, 2, 12345));

    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open()) return 0;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::string dec = bothaxXor(content, key);
    luaL_dostring(L, dec.c_str());
    return 0;
}

int LuaEngine::bothaxMakeRequest(lua_State* L) {
    const char* url_str = luaL_checkstring(L, 1);
    const char* method = luaL_optstring(L, 2, "GET");
    const char* post_body = luaL_optstring(L, 4, "");
    int timeout_ms = static_cast<int>(luaL_optinteger(L, 5, 5000));

    std::string url = url_str;
    bool is_https = (url.find("https://") == 0);
    std::string clean = url;
    if (is_https) clean = url.substr(8);
    else if (url.find("http://") == 0) clean = url.substr(7);

    std::string host, path = "/";
    size_t slash = clean.find('/');
    if (slash != std::string::npos) {
        host = clean.substr(0, slash);
        path = clean.substr(slash);
    } else {
        host = clean;
    }

    int port = is_https ? 443 : 80;
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        port = std::stoi(host.substr(colon + 1));
        host = host.substr(0, colon);
    }

    int status_code = 0;
    std::string response_content;

#ifdef _WIN32
    std::wstring w_host(host.begin(), host.end());
    std::wstring w_path(path.begin(), path.end());
    std::wstring w_method(method, method + strlen(method));

    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 Bothax/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession) {
        WinHttpSetTimeouts(hSession, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
        HINTERNET hConnect = WinHttpConnect(hSession, w_host.c_str(), (INTERNET_PORT)port, 0);
        if (hConnect) {
            DWORD flags = is_https ? WINHTTP_FLAG_SECURE : 0;
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, w_method.c_str(), w_path.c_str(),
                                                   NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
            if (hRequest) {
                DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                                 SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
                WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

                LPCWSTR headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
                DWORD body_len = static_cast<DWORD>(strlen(post_body));
                if (WinHttpSendRequest(hRequest, headers, (DWORD)-1L, (LPVOID)post_body, body_len, body_len, 0)) {
                    if (WinHttpReceiveResponse(hRequest, NULL)) {
                        DWORD dwStatus = 0;
                        DWORD dwSize = sizeof(dwStatus);
                        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                            WINHTTP_HEADER_NAME_BY_INDEX, &dwStatus, &dwSize, WINHTTP_NO_HEADER_INDEX);
                        status_code = static_cast<int>(dwStatus);

                        DWORD bytesRead = 0;
                        char buf[4096];
                        while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
                            response_content.append(buf, bytesRead);
                        }
                    }
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }
#endif

    lua_newtable(L);
    lua_pushinteger(L, status_code);
    lua_setfield(L, -2, "status");
    lua_pushboolean(L, status_code != 200);
    lua_setfield(L, -2, "error");
    lua_pushstring(L, method);
    lua_setfield(L, -2, "method");
    lua_pushstring(L, response_content.c_str());
    lua_setfield(L, -2, "content");
    return 1;
}

int LuaEngine::bothaxRequestJoinWorld(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    const char* world = luaL_checkstring(L, 1);
    if (self) {
        self->actions_.warp(world);
    }
    return 0;
}

int LuaEngine::bothaxRunDelayed(lua_State* L) {
    int ms = static_cast<int>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TFUNCTION);

    // Dapatkan instance engine
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    // Ambil optional arg pertama jika ada
    std::string arg;
    if (lua_gettop(L) >= 3 && lua_isstring(L, 3)) {
        arg = lua_tostring(L, 3);
    }

    // Buat reference ke fungsi callback di registry
    lua_pushvalue(L, 2);
    int func_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    std::thread([self, L, ms, func_ref, arg]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        if (self && self->is_running_.load()) {
            std::lock_guard<std::mutex> lock(self->lua_mtx_);
            if (self->active_L_ == L) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, func_ref);
                if (lua_isfunction(L, -1)) {
                    if (!arg.empty()) {
                        lua_pushstring(L, arg.c_str());
                        lua_pcall(L, 1, 0, 0);
                    } else {
                        lua_pcall(L, 0, 0, 0);
                    }
                } else {
                    lua_pop(L, 1);
                }
            }
        }
        luaL_unref(L, LUA_REGISTRYINDEX, func_ref);
    }).detach();

    return 0;
}

int LuaEngine::bothaxRunThread(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);

    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    std::string arg;
    if (lua_gettop(L) >= 2 && lua_isstring(L, 2)) {
        arg = lua_tostring(L, 2);
    } else if (lua_gettop(L) >= 2 && lua_isnumber(L, 2)) {
        arg = std::to_string(lua_tointeger(L, 2));
    }

    lua_pushvalue(L, 1);
    int func_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    std::thread([self, L, func_ref, arg]() {
        if (self && self->is_running_.load()) {
            std::lock_guard<std::mutex> lock(self->lua_mtx_);
            if (self->active_L_ == L) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, func_ref);
                if (lua_isfunction(L, -1)) {
                    if (!arg.empty()) {
                        lua_pushstring(L, arg.c_str());
                        lua_pcall(L, 1, 0, 0);
                    } else {
                        lua_pcall(L, 0, 0, 0);
                    }
                } else {
                    lua_pop(L, 1);
                }
            }
        }
        luaL_unref(L, LUA_REGISTRYINDEX, func_ref);
    }).detach();

    return 0;
}

int LuaEngine::bothaxSendPacket(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    int type = static_cast<int>(luaL_checkinteger(L, 1));
    const char* packet = luaL_checkstring(L, 2);

    if (self) {
        // Cek OnSendPacket hooks
        if (self->triggerOnSendPacket(type, packet)) {
            Logger::log("[Bothax] 🛑 SendPacket diblokir oleh Hook!");
            return 0;
        }
        self->enet_.sendText(type, packet);
    }
    return 0;
}

int LuaEngine::bothaxSendPacketRaw(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    bool to_client = lua_toboolean(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    GameUpdatePacket pkt{};

    auto getIntField = [&](const char* field) -> int32_t {
        lua_getfield(L, 2, field);
        int32_t val = static_cast<int32_t>(luaL_optinteger(L, -1, 0));
        lua_pop(L, 1);
        return val;
    };
    auto getFloatField = [&](const char* field) -> float {
        lua_getfield(L, 2, field);
        float val = static_cast<float>(luaL_optnumber(L, -1, 0.0));
        lua_pop(L, 1);
        return val;
    };

    pkt.type = static_cast<uint8_t>(getIntField("type"));
    pkt.obj_type = static_cast<uint8_t>(getIntField("dropped"));

    int netid = getIntField("netid");
    if (netid == 0) netid = getIntField("player_flags");
    pkt.net_id = netid != 0 ? netid : (self ? self->world_.getLocalNetId() : -1);

    int snetid = getIntField("snetid");
    if (snetid == 0) snetid = getIntField("secondnetid");
    pkt.target_net_id = snetid;

    int value = getIntField("value");
    if (value == 0) value = getIntField("int_data");
    pkt.main_data = value;

    float px = getFloatField("x");
    if (px == 0.0f) px = getFloatField("pos_x");
    pkt.pos_x = px;

    float py = getFloatField("y");
    if (py == 0.0f) py = getFloatField("pos_y");
    pkt.pos_y = py;

    pkt.speed_x = getFloatField("xspeed");
    pkt.speed_y = getFloatField("yspeed");

    int tx = getIntField("px");
    if (tx == 0) tx = getIntField("tx");
    if (tx == 0) tx = getIntField("int_x");
    pkt.int_x = tx;

    int ty = getIntField("py");
    if (ty == 0) ty = getIntField("ty");
    if (ty == 0) ty = getIntField("int_y");
    pkt.int_y = ty;

    if (self) {
        if (self->triggerOnSendPacketRaw(pkt)) {
            Logger::log("[Bothax] 🛑 SendPacketRaw diblokir oleh Hook!");
            return 0;
        }
        if (!to_client) {
            self->actions_.sendTankPacket(pkt);
        }
    }
    return 0;
}

int LuaEngine::bothaxSendVariantList(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    luaL_checktype(L, 1, LUA_TTABLE);
    int netid = static_cast<int>(luaL_optinteger(L, 2, -1));
    int delay = static_cast<int>(luaL_optinteger(L, 3, 0));

    std::vector<Variant> vars;
    // Iterate 0, 1, 2, 3...
    for (int i = 0; i < 16; ++i) {
        lua_pushinteger(L, i);
        lua_gettable(L, 1);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            if (i > 0) break;
            else continue;
        }

        Variant v;
        if (lua_isnumber(L, -1)) {
            v.type = 9; // int
            v.int_val = static_cast<int32_t>(lua_tointeger(L, -1));
        } else if (lua_isstring(L, -1)) {
            v.type = 2; // string
            v.str_val = lua_tostring(L, -1);
        } else if (lua_isboolean(L, -1)) {
            v.type = 9;
            v.int_val = lua_toboolean(L, -1) ? 1 : 0;
        }
        vars.push_back(v);
        lua_pop(L, 1);
    }

    if (self && !vars.empty()) {
        auto payload = serializeVariantList(vars);
        GameUpdatePacket pkt{};
        pkt.type = PACKET_CALL_FUNCTION; // 1
        pkt.net_id = netid;
        pkt.float_var = static_cast<float>(delay);
        self->actions_.sendTankPacket(pkt, payload.data(), payload.size());
    }
    return 0;
}

int LuaEngine::bothaxSetItemSelected(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    int id = static_cast<int>(luaL_checkinteger(L, 1));
    if (self) {
        GameUpdatePacket pkt{};
        pkt.type = PACKET_ITEM_ACTIVATE_REQUEST; // 10
        pkt.main_data = id;
        pkt.net_id = self->world_.getLocalNetId();
        self->actions_.sendTankPacket(pkt);
        Logger::log("[Bothax] 📦 Item dipilih: ID ", id);
    }
    return 0;
}

int LuaEngine::bothaxSetTileFlags(lua_State* L) {
    int x = static_cast<int>(luaL_checkinteger(L, 1));
    int y = static_cast<int>(luaL_checkinteger(L, 2));
    Logger::log("[Bothax] 🚩 SetTileFlags di (", x, ", ", y, ")");
    return 0;
}

// ============================================================
// Bothax Hook Dispatchers
// ============================================================
bool LuaEngine::triggerOnVariant(const std::vector<Variant>& vars, int netid, int delay) {
    if (!is_running_.load()) return false;
    std::lock_guard<std::mutex> lock(lua_mtx_);
    if (!active_L_) return false;

    lua_State* L = active_L_;
    lua_getfield(L, LUA_REGISTRYINDEX, "__BOTHAX_HOOKS");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }

    lua_getfield(L, -1, "OnVariant");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        return false;
    }

    bool blocked = false;
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_isfunction(L, -1)) {
            // Arg 1: Table var
            lua_newtable(L);
            for (size_t i = 0; i < vars.size(); ++i) {
                lua_pushinteger(L, static_cast<lua_Integer>(i));
                if (vars[i].type == 2) {
                    lua_pushstring(L, vars[i].str_val.c_str());
                } else if (vars[i].type == 1) {
                    lua_pushnumber(L, vars[i].float_val);
                } else {
                    lua_pushinteger(L, vars[i].int_val);
                }
                lua_settable(L, -3);
            }

            // Arg 2: netid
            lua_pushinteger(L, netid);
            // Arg 3: delay
            lua_pushinteger(L, delay);

            if (lua_pcall(L, 3, 1, 0) == LUA_OK) {
                if (lua_isboolean(L, -1) && lua_toboolean(L, -1)) {
                    blocked = true;
                }
                lua_pop(L, 1);
            }
        } else {
            lua_pop(L, 1);
        }
    }

    lua_pop(L, 2);
    return blocked;
}

bool LuaEngine::triggerOnSendPacket(int type, const std::string& packet) {
    if (!is_running_.load()) return false;
    std::lock_guard<std::mutex> lock(lua_mtx_);
    if (!active_L_) return false;

    lua_State* L = active_L_;
    lua_getfield(L, LUA_REGISTRYINDEX, "__BOTHAX_HOOKS");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }

    lua_getfield(L, -1, "OnSendPacket");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        return false;
    }

    bool blocked = false;
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_isfunction(L, -1)) {
            lua_pushinteger(L, type);
            lua_pushstring(L, packet.c_str());
            if (lua_pcall(L, 2, 1, 0) == LUA_OK) {
                if (lua_isboolean(L, -1) && lua_toboolean(L, -1)) {
                    blocked = true;
                }
                lua_pop(L, 1);
            }
        } else {
            lua_pop(L, 1);
        }
    }

    lua_pop(L, 2);
    return blocked;
}

bool LuaEngine::triggerOnSendPacketRaw(const GameUpdatePacket& pkt) {
    if (!is_running_.load()) return false;
    std::lock_guard<std::mutex> lock(lua_mtx_);
    if (!active_L_) return false;

    lua_State* L = active_L_;
    lua_getfield(L, LUA_REGISTRYINDEX, "__BOTHAX_HOOKS");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }

    lua_getfield(L, -1, "OnSendPacketRaw");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        return false;
    }

    bool blocked = false;
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_isfunction(L, -1)) {
            lua_newtable(L);
            lua_pushinteger(L, pkt.type);
            lua_setfield(L, -2, "type");
            lua_pushinteger(L, pkt.main_data);
            lua_setfield(L, -2, "value");
            lua_pushinteger(L, pkt.net_id);
            lua_setfield(L, -2, "netid");
            lua_pushnumber(L, pkt.pos_x);
            lua_setfield(L, -2, "pos_x");
            lua_pushnumber(L, pkt.pos_y);
            lua_setfield(L, -2, "pos_y");

            if (lua_pcall(L, 1, 1, 0) == LUA_OK) {
                if (lua_isboolean(L, -1) && lua_toboolean(L, -1)) {
                    blocked = true;
                }
                lua_pop(L, 1);
            }
        } else {
            lua_pop(L, 1);
        }
    }

    lua_pop(L, 2);
    return blocked;
}

void LuaEngine::triggerOnProcessTankUpdate(const GameUpdatePacket& pkt) {
    if (!is_running_.load()) return;
    std::lock_guard<std::mutex> lock(lua_mtx_);
    if (!active_L_) return;

    lua_State* L = active_L_;
    lua_getfield(L, LUA_REGISTRYINDEX, "__BOTHAX_HOOKS");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

    lua_getfield(L, -1, "OnProcessTankUpdate");
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return; }

    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_isfunction(L, -1)) {
            lua_newtable(L);
            lua_pushinteger(L, pkt.type);
            lua_setfield(L, -2, "type");
            lua_pushinteger(L, pkt.main_data);
            lua_setfield(L, -2, "value");
            lua_pushinteger(L, pkt.net_id);
            lua_setfield(L, -2, "netid");
            lua_pushnumber(L, pkt.pos_x);
            lua_setfield(L, -2, "pos_x");
            lua_pushnumber(L, pkt.pos_y);
            lua_setfield(L, -2, "pos_y");
            lua_pcall(L, 1, 0, 0);
        } else {
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);
}

void LuaEngine::triggerOnWorldTouch(float x, float y, bool start) {
    if (!is_running_.load()) return;
    std::lock_guard<std::mutex> lock(lua_mtx_);
    if (!active_L_) return;

    lua_State* L = active_L_;
    lua_getfield(L, LUA_REGISTRYINDEX, "__BOTHAX_HOOKS");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

    lua_getfield(L, -1, "OnWorldTouch");
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return; }

    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_isfunction(L, -1)) {
            lua_newtable(L);
            lua_pushnumber(L, x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, y);
            lua_setfield(L, -2, "y");
            lua_pushboolean(L, start);
            lua_pcall(L, 2, 0, 0);
        } else {
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);
}

void LuaEngine::triggerOnDraw(float deltatime) {
    if (!is_running_.load()) return;
    std::lock_guard<std::mutex> lock(lua_mtx_);
    if (!active_L_) return;

    lua_State* L = active_L_;
    lua_getfield(L, LUA_REGISTRYINDEX, "__BOTHAX_HOOKS");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

    lua_getfield(L, -1, "OnDraw");
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return; }

    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_isfunction(L, -1)) {
            lua_pushnumber(L, deltatime);
            lua_pcall(L, 1, 0, 0);
        } else {
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);
}

void LuaEngine::triggerOnInput(int key) {
    if (!is_running_.load()) return;
    std::lock_guard<std::mutex> lock(lua_mtx_);
    if (!active_L_) return;

    lua_State* L = active_L_;
    lua_getfield(L, LUA_REGISTRYINDEX, "__BOTHAX_HOOKS");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

    lua_getfield(L, -1, "OnInput");
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return; }

    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_isfunction(L, -1)) {
            lua_pushinteger(L, key);
            lua_pcall(L, 1, 0, 0);
        } else {
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);
}

// ============================================================
// Lucifer Bot Metatable Methods (Backward Compatibility)
// ============================================================
int LuaEngine::luaGetBot(lua_State* L) {
    lua_getglobal(L, "bot");
    return 1;
}

int LuaEngine::luaGetBots(lua_State* L) {
    lua_newtable(L);
    lua_getglobal(L, "bot");
    lua_rawseti(L, -2, 1);
    return 1;
}

int LuaEngine::botSay(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return 0;

    int idx = lua_isuserdata(L, 1) || lua_istable(L, 1) ? 2 : 1;
    const char* text = luaL_checkstring(L, idx);
    self->actions_.say(text);
    return 0;
}

int LuaEngine::botWarp(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return 0;

    int idx = lua_isuserdata(L, 1) || lua_istable(L, 1) ? 2 : 1;
    const char* world = luaL_checkstring(L, idx);
    self->actions_.warp(world);
    return 0;
}

int LuaEngine::botMoveTo(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return 0;

    int base = lua_isuserdata(L, 1) || lua_istable(L, 1) ? 2 : 1;
    int x = static_cast<int>(luaL_checkinteger(L, base));
    int y = static_cast<int>(luaL_checkinteger(L, base + 1));
    int delay = static_cast<int>(luaL_optinteger(L, base + 2, 35));
    self->actions_.moveTo(x, y, delay);
    return 0;
}

int LuaEngine::botHit(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return 0;

    int base = lua_isuserdata(L, 1) || lua_istable(L, 1) ? 2 : 1;
    int x = static_cast<int>(luaL_checkinteger(L, base));
    int y = static_cast<int>(luaL_checkinteger(L, base + 1));
    self->actions_.hit(x, y);
    return 0;
}

int LuaEngine::botPlace(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return 0;

    int base = lua_isuserdata(L, 1) || lua_istable(L, 1) ? 2 : 1;
    int id = static_cast<int>(luaL_checkinteger(L, base));
    int x = static_cast<int>(luaL_checkinteger(L, base + 1));
    int y = static_cast<int>(luaL_checkinteger(L, base + 2));
    self->actions_.place(static_cast<uint16_t>(id), x, y);
    return 0;
}

int LuaEngine::botWrench(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return 0;

    int base = lua_isuserdata(L, 1) || lua_istable(L, 1) ? 2 : 1;
    int x = static_cast<int>(luaL_checkinteger(L, base));
    int y = static_cast<int>(luaL_checkinteger(L, base + 1));
    self->actions_.wrench(x, y);
    return 0;
}

int LuaEngine::botEnter(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (self) self->actions_.enter();
    return 0;
}

int LuaEngine::botCollect(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return 0;

    int base = lua_isuserdata(L, 1) || lua_istable(L, 1) ? 2 : 1;
    float r = static_cast<float>(luaL_optnumber(L, base, 4.0));
    int collected = self->actions_.collectNearby(r);
    lua_pushinteger(L, collected);
    return 1;
}

int LuaEngine::botDrop(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return 0;

    int base = lua_isuserdata(L, 1) || lua_istable(L, 1) ? 2 : 1;
    int id = static_cast<int>(luaL_checkinteger(L, base));
    int count = static_cast<int>(luaL_checkinteger(L, base + 1));
    self->actions_.drop(static_cast<uint16_t>(id), count);
    return 0;
}

int LuaEngine::botTrash(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return 0;

    int base = lua_isuserdata(L, 1) || lua_istable(L, 1) ? 2 : 1;
    int id = static_cast<int>(luaL_checkinteger(L, base));
    int count = static_cast<int>(luaL_checkinteger(L, base + 1));
    self->actions_.trash(static_cast<uint16_t>(id), count);
    return 0;
}

int LuaEngine::botRespawn(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (self) self->actions_.respawn();
    return 0;
}

int LuaEngine::botGetLocal(lua_State* L) {
    return bothaxGetLocal(L);
}

int LuaEngine::botGetWorld(lua_State* L) {
    return bothaxGetWorld(L);
}

int LuaEngine::botGetInventory(lua_State* L) {
    return bothaxGetInventory(L);
}

int LuaEngine::botGetPlayers(lua_State* L) {
    return bothaxGetPlayerList(L);
}

int LuaEngine::botFindItem(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return 0;

    int base = lua_isuserdata(L, 1) || lua_istable(L, 1) ? 2 : 1;
    int id = static_cast<int>(luaL_checkinteger(L, base));
    int count = self->world_.getItemCount(static_cast<uint16_t>(id));
    lua_pushinteger(L, count);
    return 1;
}
