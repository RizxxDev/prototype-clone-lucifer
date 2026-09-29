#include "lua_engine.hpp"
#include "bot_actions.hpp"
#include "world.hpp"
#include "enet_client.hpp"
#include "logger.hpp"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <chrono>
#include <sstream>
#include <algorithm>

static void luaHook(lua_State* L, lua_Debug* /*ar*/) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (self && !self->isRunning()) {
        luaL_error(L, "Script execution stopped by user.");
    }
}

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
    Logger::log("[LUA] 🚀 Memulai eksekusi script Lua...");

    lua_State* L = luaL_newstate();
    if (!L) {
        std::lock_guard<std::mutex> lock(error_mtx_);
        last_error_ = "Gagal mengalokasikan Lua state memory.";
        is_running_ = false;
        return;
    }

    luaL_openlibs(L);

    // Simpan pointer instance di registry
    lua_pushlightuserdata(L, this);
    lua_setfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");

    // Hook untuk mengecek interupsi stop
    lua_sethook(L, luaHook, LUA_MASKCOUNT, 1000);

    registerLuciferApi(L);

    int status = luaL_dostring(L, code.c_str());
    if (status != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        std::string err_str = err ? err : "Unknown Lua error";
        {
            std::lock_guard<std::mutex> lock(error_mtx_);
            last_error_ = err_str;
        }
        Logger::log("[LUA] ❌ Error script: ", err_str);
    } else {
        Logger::log("[LUA] ✅ Script Lua selesai dieksekusi dengan sukses.");
    }

    lua_close(L);
    is_running_ = false;
}

void LuaEngine::registerLuciferApi(lua_State* L) {
    // 1. Global sleep(ms)
    lua_pushcfunction(L, luaSleep);
    lua_setglobal(L, "sleep");

    // 2. Global log(msg) & print(msg)
    lua_pushcfunction(L, luaLog);
    lua_setglobal(L, "log");
    lua_pushcfunction(L, luaLog);
    lua_setglobal(L, "print");

    // 3. Global getBot() & getBots()
    lua_pushcfunction(L, luaGetBot);
    lua_setglobal(L, "getBot");
    lua_pushcfunction(L, luaGetBots);
    lua_setglobal(L, "getBots");

    // 4. Create Bot table & metatable
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

    // Tetapkan bot table sebagai global `bot` dan alias `Lucifer`
    lua_pushvalue(L, -1);
    lua_setglobal(L, "bot");
    lua_setglobal(L, "Lucifer");
}

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
    Logger::log("[LUA] ", ss.str());
    return 0;
}

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
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return 0;

    float px = 0, py = 0;
    int tx = 0, ty = 0;
    self->world_.getLocalPos(px, py, tx, ty);
    int net_id = self->world_.getLocalNetId();

    lua_newtable(L);
    lua_pushnumber(L, px);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, py);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, tx);
    lua_setfield(L, -2, "tile_x");
    lua_pushinteger(L, ty);
    lua_setfield(L, -2, "tile_y");
    lua_pushinteger(L, net_id);
    lua_setfield(L, -2, "net_id");
    return 1;
}

int LuaEngine::botGetWorld(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return 0;

    std::string wname = self->world_.getWorldName();
    int w = self->world_.getWidth();
    int h = self->world_.getHeight();

    lua_newtable(L);
    lua_pushstring(L, wname.c_str());
    lua_setfield(L, -2, "name");
    lua_pushinteger(L, w);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, h);
    lua_setfield(L, -2, "height");
    return 1;
}

int LuaEngine::botGetInventory(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return 0;

    auto items = self->world_.getInventory();
    lua_newtable(L);

    for (size_t i = 0; i < items.size(); ++i) {
        lua_newtable(L);
        lua_pushinteger(L, items[i].id);
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, items[i].count);
        lua_setfield(L, -2, "count");
        lua_pushinteger(L, items[i].flags);
        lua_setfield(L, -2, "flags");
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

int LuaEngine::botGetPlayers(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "__LUA_ENGINE_INSTANCE");
    LuaEngine* self = reinterpret_cast<LuaEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return 0;

    auto players = self->world_.getPlayers();
    lua_newtable(L);

    for (size_t i = 0; i < players.size(); ++i) {
        lua_newtable(L);
        lua_pushstring(L, WorldState::stripColorCodes(players[i].name).c_str());
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, players[i].net_id);
        lua_setfield(L, -2, "net_id");
        lua_pushinteger(L, players[i].user_id);
        lua_setfield(L, -2, "user_id");
        lua_pushnumber(L, players[i].x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, players[i].y);
        lua_setfield(L, -2, "y");
        lua_pushstring(L, players[i].country.c_str());
        lua_setfield(L, -2, "country");
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
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
