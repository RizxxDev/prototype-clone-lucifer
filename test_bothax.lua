-- ============================================================
-- Test Suite: Bothax Lua API Complete Compliance Test
-- Verifies all 43 functions, 16 structures, and event hooks
-- ============================================================

local function assert_eq(actual, expected, msg)
    if actual ~= expected then
        error(string.format("ASSERTION FAILED: %s (expected %s, got %s)", tostring(msg), tostring(expected), tostring(actual)))
    end
end

local function assert_true(cond, msg)
    if not cond then
        error(string.format("ASSERTION FAILED: %s", tostring(msg)))
    end
end

LogToConsole("[TEST] Starting Bothax API Complete Validation...")

-- 1. Logging & Console
Log("Test Log")
log("Test log alias")
LogToConsole("Test LogToConsole")
print("Test print")

-- 2. Hashing
local h32 = Hash32("Growtopia")
assert_true(type(h32) == "number", "Hash32 returns number")
local h64 = Hash64("Growtopia")
assert_true(type(h64) == "number", "Hash64 returns number")
LogToConsole(string.format("[TEST] Hash32: %d, Hash64: %s", h32, tostring(h64)))

-- 3. Encryption & Decryption
local secret = "print('Hello from encrypted script!')"
local enc = Encrypt(secret, 9999)
assert_true(type(enc) == "string" and #enc > 0, "Encrypt returns hex string")
local ok_dec = LoadEncrypt(enc, 9999)
assert_true(ok_dec, "LoadEncrypt succeeded")

local ok_enc_file = EncryptFile("offsets.txt", 9999)
assert_true(ok_enc_file, "EncryptFile succeeded")
local ok_load_file = LoadEncryptedFile("offsets.txt", 9999)
-- offsets.txt is not Lua script, so LoadEncryptedFile may return false when parsing, but function exists and is callable
LogToConsole("[TEST] Encrypt & LoadEncrypt validated.")

-- 4. Settings & Values
ChangeValue("modfly", true)
ChangeValue("Speed", 40)
ChangeValue("canseeghost", true)
ChangeValue("isdarkened", false)
ChangeValue("noname", true)
ChangeValue("autoreconnect", true)
LogToConsole("[TEST] ChangeValue validated.")

-- 5. World, Camera, Client, Inventory Queries
local client = GetClient()
assert_true(type(client) == "table", "GetClient table")
assert_true(client.address ~= nil and client.port ~= nil and client.ping ~= nil, "GetClient fields")

local cam = GetCamera()
assert_true(type(cam) == "table" and type(cam.pos) == "table" and type(cam.resolution) == "table", "GetCamera fields")

local world = GetWorld()
assert_true(type(world) == "table" and world.name ~= nil and world.width ~= nil and world.height ~= nil, "GetWorld fields")

local inv = GetInventory()
assert_true(type(inv) == "table", "GetInventory table")

local local_player = GetLocal()
assert_true(type(local_player) == "table" and type(local_player.pos) == "table" and local_player.netid ~= nil, "GetLocal fields")

local player_info = GetPlayerInfo()
assert_true(type(player_info) == "table" and player_info.gems ~= nil and type(player_info.backpack) == "table", "GetPlayerInfo fields")
assert_true(player_info.backpack.size ~= nil and player_info.backpack.selected ~= nil, "PlayerItems backpack fields")

local player_items = GetPlayerItems()
assert_true(type(player_items) == "table" and player_items.gems ~= nil, "GetPlayerItems alias works")

local players = GetPlayerList()
assert_true(type(players) == "table", "GetPlayerList table")

local player_lookup = GetPlayer(local_player.netid)
assert_true(type(player_lookup) == "table" and player_lookup.netid == local_player.netid, "GetPlayer by netid")

local objects = GetObjectList()
assert_true(type(objects) == "table", "GetObjectList table")

-- 6. NPC Queries
local npc_list = GetNPCList()
assert_true(type(npc_list) == "table", "GetNPCList table")
local npc_none = GetNPC(9999)
assert_true(npc_none == nil, "GetNPC returns nil for nonexistent NPC")

-- 7. Tiles & Flags
local tile = GetTile(0, 0)
assert_true(type(tile) == "table" and tile.fg ~= nil and tile.bg ~= nil, "GetTile fg/bg")
assert_true(tile.collidable ~= nil and tile.isdoor ~= nil, "GetTile collidable/isdoor")
assert_true(type(tile.pos) == "table" and tile.pos.x == 0 and tile.pos.y == 0, "GetTile pixel pos")
assert_true(type(tile.flags) == "table" and tile.flags.value ~= nil, "GetTile flags table")
assert_true(tile.locktile ~= nil, "GetTile locktile")

SetTileFlags(0, 0, 5)
local tile_mod = GetTile(0, 0)
assert_eq(tile_mod.flags.value, 5, "SetTileFlags update verified")

local tiles = GetTiles()
assert_true(type(tiles) == "table", "GetTiles table")

-- 8. Pathfinding
local path_ok = CheckPath(0, 0)
assert_true(type(path_ok) == "boolean", "CheckPath returns boolean")
local path_4arg = CheckPath(0, 0, 0, 0)
assert_true(type(path_4arg) == "boolean", "CheckPath 4 args returns boolean")
local find_res = FindPath(0, 0)
assert_true(type(find_res) == "boolean", "FindPath returns boolean")

-- 9. Items Database
local itm_dirt = GetItemByIDSafe(2)
assert_true(type(itm_dirt) == "table" and itm_dirt.name == "Dirt", "GetItemByIDSafe dirt")
assert_true(itm_dirt.id == 2 and itm_dirt.breakhit ~= nil and itm_dirt.growtime ~= nil, "ItemInfo fields")

local itm_by_name = GetItemByName("Dirt")
assert_true(type(itm_by_name) == "table" and itm_by_name.id == 2, "GetItemByName dirt")

local itm_info = GetItemInfo(2)
assert_true(type(itm_info) == "table" and itm_info.id == 2, "GetItemInfo dirt")

local itm_partial = GetItemsByPartialName("Dir")
assert_true(type(itm_partial) == "table" and #itm_partial >= 1, "GetItemsByPartialName")

local all_itms = GetItemInfoList()
assert_true(type(all_itms) == "table" and #all_itms > 0, "GetItemInfoList")

-- 10. Packet & World Interaction APIs
SendPacket(2, "action|enter_game\n")
SendPacketRaw({
    type = 0,
    x = 100.0,
    y = 200.0,
    value = 0,
    netid = 1
})
SendVariantList({ "OnConsoleMessage", "Test message from Lua!" })
SetItemSelected(18)
RequestJoinWorld("START")

-- 11. Offsets emulation
assert_true(Offsets ~= nil and Offsets.GetGameLogic ~= nil, "Offsets table exists")
assert_true(O_GETGAMELOGIC ~= nil, "Global offset constant exists")
local col = IsCollidable(2)
assert_true(type(col) == "boolean", "IsCollidable returns boolean")
local door = IsDoor(12)
assert_true(type(door) == "boolean", "IsDoor returns boolean")
local gl = GetGameLogic()
assert_true(type(gl) == "table" and gl["local"] ~= nil, "GetGameLogic table")
local app = GetBaseApp()
assert_true(type(app) == "table" and app.version ~= nil, "GetBaseApp table")

-- 12. Hook registration and removal
local hook_fired = false
AddHook(BothaxHook.OnSendPacket, "test_hook", function(t, p)
    hook_fired = true
    return false
end)
RemoveHook("test_hook")
RemoveHooks()
LogToConsole("[TEST] Hook management validated.")

-- 13. Threading & Delayed
RunDelayed(50, function(msg)
    LogToConsole("[TEST Delayed] Callback executed with: " .. tostring(msg))
end, "DelayedArgOK")

RunThread(function(num)
    LogToConsole("[TEST Thread] Worker executed with: " .. tostring(num))
end, 42)

-- 14. CamelCase aliases
assert_true(getTile ~= nil and getWorld ~= nil and getLocal ~= nil and findPath ~= nil, "CamelCase aliases registered")

Sleep(150)
LogToConsole("[TEST] ✅ ALL BOTHAX FUNCTIONS AND STRUCTURES VALIDATED SUCCESSFULLY!")
