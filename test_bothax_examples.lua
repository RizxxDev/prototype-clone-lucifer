-- ============================================================
-- Test Suite: Bothax Official Repository Examples Test
-- Verifies exact code patterns from yeweroooo/bothax examples
-- ============================================================

LogToConsole("[EXAMPLES TEST] Testing official Bothax repository example patterns...")

-- Pattern 1: 01_quick_start.lua
LogToConsole("Hello World!")
local me = GetLocal()
LogToConsole(string.format("Name: %s, NetID: %d, UserID: %d", me.name, me.netid, me.userid))

-- Pattern 2: 02_find_path.lua
local can_reach = CheckPath(0, 0)
LogToConsole(string.format("Can reach (0, 0): %s", tostring(can_reach)))
local path_result = FindPath(0, 0)
LogToConsole(string.format("FindPath result: %s", tostring(path_result)))

-- Pattern 3: 03_hooks.lua
AddHook(BothaxHook.OnVariant, "test_on_variant", function(var, netid, delay)
    LogToConsole(string.format("OnVariant hook: netid=%d, delay=%d", netid, delay))
    return false
end)
RemoveHook(BothaxHook.OnVariant, "test_on_variant")

AddHook(BothaxHook.OnSendPacket, "test_on_send", function(type, packet)
    LogToConsole(string.format("OnSendPacket hook: type=%d", type))
    return false
end)
RemoveHook("test_on_send")

-- Pattern 4: 04_request.lua
local res = MakeRequest("https://httpbin.org/get", "GET", { ["User-Agent"] = "Bothax/1.0" })
LogToConsole(string.format("MakeRequest status: %d, error: %s, method: %s", res.status, tostring(res.error), res.method))

-- Pattern 5: 05_encrypt.lua
local source_code = "LogToConsole('Executed from encrypted source chunk!')"
local enc_chunk = Encrypt(source_code)
LogToConsole("Encrypted chunk length: " .. tostring(#enc_chunk))
LoadEncrypt(enc_chunk)

LogToConsole("[EXAMPLES TEST] ✅ All 5 official Bothax example patterns succeeded!")
