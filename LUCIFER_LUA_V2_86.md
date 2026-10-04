# Lucifer Lua API v2.86

This guide documents the Lua interface available to Lucifer scripts in version 2.86. It is written for script authors: examples use Lua names and describe observable behavior rather than native implementation details.

> Documentation boundary: this reference intentionally omits source symbols, memory layout, backing containers, private helpers, and packet constants. Use the methods documented here instead of depending on raw state.

## Getting started

Most bot-local scripts begin by getting their current bot:

```lua
local bot = getBot()

if bot == nil then
    print("This script needs a bot context.")
    return
end

print("Running as " .. bot.name)
```

Lua method calls use a colon:

```lua
bot:say("Hello!")
bot:warp("START")
```

Use a dot for properties and enum members:

```lua
bot.auto_reconnect = true

if bot.status == BotStatus.online then
    print("Connected")
end
```

### Values and collections

- Lua arrays are one-based.
- A lookup that finds nothing normally returns `nil`.
- Read-only values are marked **R**. Writable values are marked **R/W**.
- Positions named `x` and `y` use tile coordinates unless the entry explicitly says pixels.
- Time values are seconds unless the entry explicitly says milliseconds.
- Methods returning `nil` perform an action without reporting server acceptance.

### Safe starter script

```lua
local bot = getBot()

if bot == nil or not bot:isInWorld() then
    return
end

local world = bot:getWorld()
local inventory = bot:getInventory()

print("World: " .. world.name)
print("Position: " .. bot.x .. ", " .. bot.y)
print("Inventory slots used: " .. inventory.itemcount)

local dirt = inventory:getItem(2)
if dirt ~= nil then
    print("Dirt: " .. dirt.count)
end
```

## Script lifecycle

### `sleep(milliseconds)`

Pauses the current script. The pause remains responsive to a stop request.

```lua
sleep(500)
```

### `runThread(function, ...)`

Runs a Lua function in a separate script thread and forwards any extra arguments.

```lua
runThread(function(message)
    sleep(250)
    print(message)
end, "Thread finished")
```

Threads have their own Lua state. Pass simple values or public API objects deliberately, and avoid unsynchronized writes to the same file or bot setting.

### `on_stop(error_message)`

Define this optional global callback to run when the script ends. `error_message` is empty after a normal finish and contains the script error after a failure.

```lua
function on_stop(error_message)
    if error_message ~= "" then
        print("Stopped: " .. error_message)
    end
end
```

### `script_id`

The current script identifier, when the host launched the script with one.

## Global functions

### Files and text

| Function | Returns | Description |
| --- | --- | --- |
| `read(path)` | `string` | Reads the entire file, or an empty string when it cannot be opened. |
| `write(path, content)` | `boolean` | Replaces a file with `content`. |
| `append(path, content)` | `boolean` | Appends `content` to a file. |
| `removeColor(text)` | `string` | Removes Growtopia color formatting. |
| `clearConsole()` | `nil` | Clears the application console. |

Treat file paths as local application paths. Never write account tokens, passwords, recovery details, or webhook URLs to shared files.

### Account and bot lookup

| Function | Returns | Description |
| --- | --- | --- |
| `getUsername()` | `string` | Current Lucifer account username. |
| `getBanRate()` | `number` | Latest reported ban-rate value. |
| `getBot()` | `Lucifer` or `nil` | Current bot in a bot-local script. |
| `getBot(name)` | `Lucifer` or `nil` | Finds a bot by name. |
| `getBot(index)` | `Lucifer` or `nil` | Finds a bot by one-based index. |
| `getBots()` | `Lucifer[]` | Returns all bots as a one-based Lua table. |
| `removeBot()` | `nil` | Removes the current bot. |
| `removeBot(name)` | `nil` | Removes the named bot. |

### Adding bots

Common forms:

```lua
local bot = addBot("growid", "password")
local token_bot = addTokenBot("login-token")
local proxied_bot = addTokenBot("login-token", "127.0.0.1:1080:user:pass")
```

Available calls:

- `addBot(name, password)`
- `addBot(name, mac, rid)`
- `addBot(name, password, mac, rid)`
- `addBot(name, password, mac, rid, platform)`
- `addUbiBot(email, password, otp_secret)`
- `addSteamBot(ubisoft_email, ubisoft_password, steam_login)`
- `addSteamBot(ubisoft_email, ubisoft_password, otp_secret, steam_login)`
- `addTokenBot(token[, proxy])`

For advanced account types, `addBot(options)` accepts a table. Only provide fields required by that account:

```lua
local bot = addBot({
    name = "growid",
    display = "Friendly label",
    password = "password",
    platform = Platform.windows,
    proxy = "127.0.0.1:1080:user:pass",
    connect = true
})
```

Supported option names are `name`, `display`, `password`, `mail`, `mac`, `rid`, `vid`, `aid`, `secret`, `recovery`, `appleData`, `proxy`, `steamlogin`, `gmaillogin`, `hash`, `platform`, and `connect`.

### Item catalog

| Function | Returns | Description |
| --- | --- | --- |
| `getInfo(item_id)` | `ItemDescription` or `nil` | Finds an item by numeric ID. |
| `getInfo(name)` | `ItemDescription` or `nil` | Finds an item by name. |
| `getInfos()` | item collection | Returns the available item catalog. |

```lua
local item = getInfo("Dirt")
if item ~= nil then
    print(item.id .. ": " .. item.name)
end
```

### Packs and trash list

| Function | Returns | Description |
| --- | --- | --- |
| `addPack(data)` | `nil` | Adds a pack definition. |
| `removePack(data)` | `nil` | Removes a pack definition. |
| `getTrashList()` | `integer[]` | Returns the global item IDs marked for trashing. |
| `setTrashList(ids)` | `nil` | Replaces the global trash list. |

## The `Lucifer` bot object

### Identity and live status

| Property | Access | Description |
| --- | --- | --- |
| `name` | R | Current bot name. |
| `index` | R | One-based bot index, or `-1` when unavailable. |
| `status` | R | A `BotStatus` value. |
| `custom_status` | R/W | User-defined status text. |
| `google_status` | R | A `GoogleStatus` value. |
| `malady` | R | Current malady identifier. |
| `home_world` | R | Detected home world. |
| `selected` | R/W | Whether the bot is selected in the UI. |
| `x`, `y` | R | Current tile coordinates; both are `0` when local-player state is unavailable. |
| `history` | R | Recent generic-message history. |
| `warp_count` | R/W | Warp counter. |
| `gem_count` | R | Current gems. |
| `obtained_gem_count` | R | Gems obtained during the tracked run. |
| `obtained_pack_count` | R | Packs obtained during the tracked run. |
| `pearl_count` | R | Current pearl count. |
| `level` | R | Current level. |
| `is_account_secured` | R | Whether account protection was detected. |
| `maximum_ping` | R/W | Configured maximum acceptable ping. |

### Connection and account methods

| Method | Returns | Description |
| --- | --- | --- |
| `bot:connect()` | `nil` | Starts a connection. |
| `bot:disconnect()` | `nil` | Disconnects the bot. |
| `bot:hasToken()` | `boolean` | Reports whether login token data is available. |
| `bot:updateToken(token)` | `nil` | Replaces the login token. |
| `bot:updateProxy(proxy)` | `boolean` | Changes the bot proxy. |
| `bot:getProxy()` | proxy snapshot | Returns the bot's current proxy details. |
| `bot:getPing()` | `integer` | Current ping in milliseconds. |
| `bot:getSignal()` | `Signal` | Returns the latest Geiger signal with read-only `x`, `y`, and `type`. |
| `bot:getCaptcha()` | `string` | Current captcha URL, or an empty string. |
| `bot:getPlaytime()` | `integer` | Account playtime value. |
| `bot:getAge()` | `integer` | Account age value. |
| `bot:getActiveTime()` | `string` | Formatted active-time value. |
| `bot:setMac(mac)` | `nil` | Updates the configured MAC value. |
| `bot:setRid(rid)` | `nil` | Updates the configured RID value. |
| `bot:setPlatform(platform)` | `nil` | Updates the login platform. |

Account update calls are also available:

- `bot:updateBot([name[, password[, mac[, rid[, secret[, hash[, platform]]]]]]])`
- `bot:updateCustomBot(options)`
- `bot:updateTokenBot(token[, proxy])`
- `bot:updateUbiBot(email, password[, secret[, is_steam]])`

Changing identity or device values while connected can invalidate the current session. Update them before connecting unless you specifically need a reconnect.

### World and chat actions

| Method | Returns | Description |
| --- | --- | --- |
| `bot:warp(world)` | `nil` | Warps to a world. |
| `bot:warp(world, door)` | `nil` | Warps to a world and door ID. |
| `bot:leaveWorld()` | `nil` | Leaves the current world. |
| `bot:say(text)` | `nil` | Sends chat or a supported slash command. |
| `bot:respawn()` | `nil` | Requests a respawn. |
| `bot:enter()` | `nil` | Activates the door on the bot's current tile. |
| `bot:enter(password)` | `nil` | Remembers a door password and enters. |
| `bot:enter(button_id)` | `nil` | Remembers a door button and enters. |
| `bot:active(x, y)` | `nil` | Activates the tile at `(x, y)`. |

```lua
if not bot:isInWorld("START") then
    bot:warp("START")
end
```

### Inventory actions

| Method | Returns | Description |
| --- | --- | --- |
| `bot:use(item_id)` | `nil` | Uses an inventory item. |
| `bot:activate(item_id)` | `nil` | Alias of `wear`. |
| `bot:wear(item_id)` | `nil` | Wears an item. |
| `bot:unwear(item_id)` | `nil` | Removes a worn item. |
| `bot:drop(item_id, count)` | `nil` | Drops an amount through the normal flow. |
| `bot:trash(item_id, count)` | `nil` | Trashes an amount through the normal flow. |
| `bot:fastDrop(item_id, count)` | `boolean` | Attempts the fast drop flow. |
| `bot:fastTrash(item_id, count)` | `boolean` | Attempts the fast trash flow. |
| `bot:buy(store_id)` | `nil` | Buys using a store identifier. |
| `bot:buy(item_id, count, max_price)` | `boolean` | Attempts an item purchase with a price ceiling. |
| `bot:retrieve(x, y)` | `boolean` | Retrieves the default amount from supported storage. |
| `bot:retrieve(x, y, count)` | `boolean` | Retrieves up to `count` items. |

Boolean results report whether the local request could be started, not whether the game server ultimately accepted it.

### Tile and player actions

| Method | Returns | Description |
| --- | --- | --- |
| `bot:place(x, y, item_id)` | `nil` | Places an item at a tile. |
| `bot:hit(x, y)` | `nil` | Punches a tile. |
| `bot:hit(npc_id)` | `nil` | Hits an NPC. |
| `bot:wrench(x, y)` | `nil` | Wrenches a tile. |
| `bot:wrenchPlayer(net_id)` | `nil` | Wrenches a player. |
| `bot:setDirection(left)` | `nil` | Faces left when true, right when false. |
| `bot:setBubble(bubble)` | `nil` | Changes the chat bubble state. |
| `bot:setSkin(skin_id)` | `nil` | Changes the skin value. |
| `bot:setCountry(country)` | `nil` | Changes the two-letter country value. |

### Movement and collection

| Method | Returns | Description |
| --- | --- | --- |
| `bot:moveTo(dx, dy)` | `nil` | Moves by a tile offset from the current position. |
| `bot:moveTile(x, y)` | `nil` | Moves directly to tile coordinates. |
| `bot:moveLeft([count])` | `nil` | Moves left by one tile or `count` tiles. |
| `bot:moveRight([count])` | `nil` | Moves right by one tile or `count` tiles. |
| `bot:moveUp([count])` | `nil` | Moves up by one tile or `count` tiles. |
| `bot:moveDown([count])` | `nil` | Moves down by one tile or `count` tiles. |
| `bot:getPath(x, y)` | `PathNode[]` | Calculates a path to a tile. |
| `bot:findPath(x, y)` | `boolean` | Follows a path to a tile and waits for completion. |
| `bot:collectObject(object_id, range)` | `boolean` | Attempts to collect one world object. |
| `bot:collectByID(item_id)` | `boolean` | Finds and attempts one object with the item ID. |
| `bot:collect(range[, interval_ms])` | `nil` | Attempts nearby objects, waiting between requests. |
| `bot:findOutput([item_id])` | `boolean` | Finds or approaches a configured output location. |

`findPath` is blocking. Prefer short paths and ensure the destination is valid before calling it.

### State checks

| Method | Returns | Description |
| --- | --- | --- |
| `bot:isInWorld()` | `boolean` | Whether full world state is available. |
| `bot:isInWorld(name)` | `boolean` | Whether the bot is in the named world. |
| `bot:isInTutorial()` | `boolean` | Whether the current world is a tutorial world. |
| `bot:isInTile(x, y)` | `boolean` | Whether the bot is on a tile. |
| `bot:isInArea(min_x, min_y, max_x, max_y)` | `boolean` | Whether the bot is inside a tile rectangle. |
| `bot:isSupporter()` | `boolean` | Whether supporter status was detected. |
| `bot:isResting()` | `boolean` | Whether the bot is resting. |
| `bot:isRunningScript()` | `boolean` | Whether the bot executor is running a script. |

### State access

| Method | Returns |
| --- | --- |
| `bot:getWorld()` | `World` |
| `bot:getInventory()` | `Inventory` |
| `bot:getConsole()` | `Console` |
| `bot:getLog()` | `Log` |
| `bot:getLogin()` | `Credentials` |

Bot-local scripts also have the shortcuts `getWorld()`, `getInventory()`, `getConsole()`, `getLog()`, `getLogin()`, `getDialog()`, `getTile(x, y)`, `getTilesSafe()`, `getObject(oid)`, `getObjects()`, `getPlayer(name_or_net_id)`, `getPlayers()`, `getNPC(id)`, `getNPCs()`, `getLocal()`, and `hasAccess(x, y)`.

### Timing

```lua
local old = bot:getInterval(Action.move)
bot:setInterval(Action.move, 0.25)
```

`bot:getInterval(action)` returns the effective interval in seconds. `bot:setInterval(action, seconds)` changes its base interval. Available actions are `Action.move`, `collect`, `hit`, `place`, `harvest`, `plant`, `warp`, `drop`, and `trash`.

### Roles and nested scripts

| Method | Returns | Description |
| --- | --- | --- |
| `bot:startRole(role)` | `boolean` | Starts a role flow. |
| `bot:stopRole()` | `nil` | Stops the current role flow. |
| `bot:finishRole()` | `boolean` | Attempts to finish the current role. |
| `bot:getRoleMission()` | `string` | Requests the current role mission. |
| `bot:runScript(source)` | `nil` | Runs Lua source on this bot. |
| `bot:stopScript()` | `nil` | Stops this bot's running script. |
| `bot:setPack(name)` | `nil` | Selects a configured pack by name. |
| `bot:getMaladyDuration()` | duration value | Returns the remaining tracked malady duration. |

## Bot behavior settings

These `Lucifer` properties are writable. Change settings before enabling the related feature.

| Group | Properties |
| --- | --- |
| Reconnect | `auto_reconnect`, `reconnect_multiplier`, `random_reconnect`, `reconnect_interval`, `min_reconnect`, `max_reconnect`, `bypass_logon` |
| Safety | `auto_leave_on_admin`, `auto_leave_on_mod`, `auto_ban`, `auto_stop`, `maximum_ping` |
| World behavior | `leave_world_before_warp`, `legit_mode`, `auto_accept`, `auto_follow`, `anti_toxic`, `anti_fire`, `hide_gems` |
| Inventory | `auto_trash`, `auto_wear`, `auto_clothes`, `auto_birth`, `auto_consume`, `auto_expand_inventory`, `wear_id`, `wear_storage`, `birth_storage`, `gem_limit`, `consume_id`, `consume_interval` |
| Resting | `auto_rest_mode`, `auto_disconnect`, `disconnect_on_rest`, `rest_time`, `rest_interval` |
| Collection | `auto_collect`, `collect_path_check`, `collect_all`, `object_collect_delay`, `collect_range`, `ignore_gems`, `ignore_essences` |
| Movement | `move_range`, `move_x`, `move_y`, `dynamic_delay` |
| Special | `auto_tutorial`, `auto_alien_scanner`, `auto_mooncake_magnificence` |

`auto_disconnect` and `disconnect_on_rest` control the same setting in v2.86.

## Reading world state safely

Use query methods instead of retaining raw collections between updates. World state can be replaced after a warp, reconnect, or map update.

### `World`

Useful read-only properties:

| Property | Description |
| --- | --- |
| `name` | Current world name. |
| `x`, `y` | World width and height in tiles. |
| `tile_count` | Number of tiles. |
| `weather` | Current weather identifier. |
| `public` | Whether the world is public. |
| `version` | Current map version. |
| `growscan` | Aggregate item counts. |
| `battle` | Current battle state. |

Query methods:

| Method | Returns | Description |
| --- | --- | --- |
| `world:getTile(x, y)` | `Tile` or `nil` | Gets one tile. |
| `world:getTilesSafe()` | `Tile[]` | Gets a snapshot suitable for iteration. |
| `world:getObject(oid)` | `NetObject` or `nil` | Gets one dropped object. |
| `world:getObjects()` | `NetObject[]` | Gets a dropped-object snapshot. |
| `world:getPlayer(name_or_net_id)` | `Player` or `nil` | Finds a player. |
| `world:getPlayers()` | `Player[]` | Gets a player snapshot. |
| `world:getLocal()` | `Player` or `nil` | Gets the local player. |
| `world:getNPC(id)` | `NPC` or `nil` | Gets an NPC. |
| `world:getNPCs()` | `NPC[]` | Gets an NPC snapshot. |
| `world:getTileParent(x, y)` | `Tile` or `nil` | Gets the effective parent tile. |
| `world:hasAccess(x, y)` | access value | Checks access at a tile. |
| `world:isValidTile(x, y)` | `boolean` | Checks tile bounds. |
| `world:isValidPosition(x, y)` | `boolean` | Checks pixel-position bounds. |
| `world:getAdventures()` | `Adventure[]` | Gets adventure markers. |
| `world:getAdventure(id)` | `Adventure` or `nil` | Finds an adventure marker. |
| `world:hasAdventure(id)` | `boolean` | Checks for an adventure marker. |

```lua
local world = bot:getWorld()

for _, tile in ipairs(world:getTilesSafe()) do
    if tile:canHarvest() then
        print("Ready at " .. tile.x .. ", " .. tile.y)
    end
end
```

### `Tile`

| Member | Access | Description |
| --- | --- | --- |
| `foreground`, `fg` | R | Foreground item ID. |
| `background`, `bg` | R | Background item ID. |
| `x`, `y` | R | Tile coordinates. |
| `parent` | R | Parent tile index or identifier. |
| `flags` | R | Tile flags. |
| `tile:hasExtra()` | method | Whether extra metadata is available. |
| `tile:getExtra()` | method | Returns optional extra metadata. |
| `tile:canHarvest()` | method | Whether the tile is ready to harvest. |
| `tile:hasFlag(flag)` | method | Checks a tile flag. |

Extra metadata differs by tile type. Common public values include `type`, `label`, `owner`, `locked`, `growth`, `item1`, `item2`, `item_count`, `item_price`, `fruit_count`, and `flags`. Always call `hasExtra()` first and treat absent or unrelated values as unavailable.

### `Player`

All player values are read-only.

| Properties | Description |
| --- | --- |
| `name`, `altName`, `country` | Display identity. |
| `netid`, `userid` | Session and user identifiers. |
| `posx`, `posy` | Pixel position. |
| `vecx`, `vecy` | Movement values. |
| `bubble` | Bubble state. |
| `isModerator`, `isSuperModerator` | Moderator indicators. |
| `isLocalPlayer`, `isFriendWithOwner` | Relationship indicators. |
| `skincolor`, `roleskin`, `roleicon`, `clothes` | Appearance. |
| `team`, `battle_item`, `battle_score` | Battle state. |

Clothing fields are `hat`, `shirt`, `pants`, `shoes`, `face`, `hand`, `wings`, `mask`, `neck`, and `ances`.

### `NetObject`, `NPC`, and `PathNode`

Dropped objects expose read-only `id`, `x`, `y`, `count`, `flags`, and `oid`. Object positions are pixels.

NPCs expose read-only `type`, `id`, `x`, `y`, `destx`, `desty`, `var`, and `unk`. Path nodes expose read-only tile coordinates `x` and `y`.

### `Inventory`

| Member | Returns | Description |
| --- | --- | --- |
| `inventory.itemcount` | `integer` | Number of occupied inventory entries. |
| `inventory.emptyslots` | `integer` | Available slots. |
| `inventory.slotcount` | `integer` | Total slot capacity. |
| `inventory.version` | `integer` | Inventory version. |
| `inventory:getItem(id_or_name)` | `InventoryItem` or `nil` | Gets one item. |
| `inventory:getItems()` | `InventoryItem[]` | Gets an item snapshot. |
| `inventory:findItem(id_or_name)` | `integer` | Gets the current count. |
| `inventory:getItemCount(id_or_name)` | `integer` | Alias of `findItem`. |
| `inventory:canCollect(item_id)` | `boolean` | Whether the item can fit. |
| `inventory:price()` | price value | Returns the next inventory-upgrade price. |

An `InventoryItem` has read-only `id`, `count`, and `isActive` values.

### `ItemDescription`

Item descriptions expose read-only `id`, `name`, `desc`, `flags`, `level`, `rarity`, `growth`, `strength`, `action_type`, `collision_type`, `clothing_type`, `type`, `collision`, `clothing`, `texture`, and `visual`.

## Events

Register callbacks first, then listen for a bounded number of seconds:

```lua
addEvent(Event.game_message, function(message)
    print(removeColor(message))
end)

addEvent(Event.variantlist, function(values, net_id)
    print("Variant event from " .. net_id)
end)

listenEvents(30)
removeEvents()
```

| Function | Description |
| --- | --- |
| `addEvent(event, callback)` | Adds a callback for an event. |
| `removeEvent(event)` | Removes all callbacks for one event. |
| `removeEvents()` | Removes every callback. |
| `listenEvents(timeout_seconds)` | Dispatches queued events until timeout or cancellation. |
| `unlistenEvents()` | Requests the active listener to stop. |

Event callback arguments:

| Event | Callback |
| --- | --- |
| `Event.variantlist` | `function(values, net_id)` |
| `Event.update_packet` | `function(packet)` |
| `Event.track_packet` | `function(text)` |
| `Event.generic_text` | `function(text)` |
| `Event.game_message` | `function(text)` |
| `Event.mod_enter` | `function(text)` |
| `Event.got_punched` | `function(text)` |
| `Event.render` | `function()` |

Do little work inside callbacks. Queue or copy the values you need and return quickly.

## Console, log, and dialog helpers

### Console and log

| Object | Members |
| --- | --- |
| `Console` | `enabled`, `contents`, `append(text)`, `clear()` |
| `Log` | `content`, `append(text)`, `clear()` |

### Dialog

| Method | Description |
| --- | --- |
| `dialog:get()` | Returns the current parsed dialog. |
| `dialog:clear()` | Clears current dialog state. |
| `dialog:getCustom()` | Returns the latest custom-dialog result. |
| `dialog:setCustom(builder)` | Displays a custom dialog. |

`dialog.active` controls whether normal dialogs are displayed.

A `DialogBuilder` provides `clear()`, `setTitle(text)`, `addLabel(text)`, `addButton(name, label)`, `addCheckbox(name, label[, default])`, and `addInput(name, label[, default])`.

## Automation modules

Automation objects are available as writable properties on each bot. Configure the module first, then set its `enabled` property or call its start method. Not every module is valid in every world or account state.

| Bot property | Type | Main public settings |
| --- | --- | --- |
| `auto_tutorial` | `AutoTutorial` | `enabled`, `set_high_level`, `set_random_skin`, `set_random_profile`, `detect_tutorial`, `clear_inventory`; read-only `world`, `message` |
| `auto_farm` | `AutoFarm` | `enabled`, `auto_place`, `auto_break`, `auto_retrieve`, `auto_remote`, `mag_x`, `mag_y`, `id`, `block_storage`, `seed_storage`, `setActive(state)` |
| `auto_spam` | `AutoSpam` | `enabled`, `show_emote`, `use_color`, `random_interval`, `auto_interval`, `randomizer`, `interval`, `messages` |
| `auto_message` | `AutoMessage` | `enabled`, `type`, `ignore_admin`, `auto_sb`, `loop_mode`, `interval`, `victim_uid`, `anti_exit`, `message`, `load`, `save`, `clear`, `start` |
| `auto_geiger` | `AutoGeiger` | `enabled`, `use_chocolate`, `insta_path`, `addWorld`, `removeWorld`, `addStorage`, `removeStorage`, `spread` |
| `auto_fish` | `AutoFish` | `enabled`, `auto_trash`, `auto_drill`, `auto_trawler`, `auto_gemonade`, `setRod`, `setBait`, `getRod`, `getBait`, `x`, `y`, `world`, `item_storage`, `reward_storage` |
| `auto_cook` | `AutoCook` | `is_cooking`, `x`, `y`, `interval`, `world`, `ingredient_storage`, `food_storage`, `setActive`, `setFood`, `setTemperature`, `start`, `stop` |
| `auto_crime` | `AutoCrime` | `enabled`, `auto_wave`, `auto_bbq`, `world`, `tool_storage`, `reward_storage`, `setActive` |
| `auto_carnival` | `AutoCarnival` | `enabled`, `auto_trash`, `auto_buy`, `selected_game`, `reward_storage`, `ticket_storage` |
| `auto_parkour` | `AutoParkour` | `enabled`, `type`, `limit`, `block`, `setWebhook`, `setTicket`, `setReward` |
| `auto_synth` | `AutoSynth` | `enabled`, `x`, `y`, `world`, `tool_storage`, `reward_storage` |
| `auto_npc` | `AutoNPC` | `enabled`, `auto_spawner`, `auto_sucker`, `collect_mode`, `spawner_limit`, `sucker_limit`, `main_world`, `item_storage`, `reward_storage` |
| `auto_transfer` | `AutoTransfer` | `enabled`, `drop_vertical`, `restock_vend`, `auto_vend`, `itemid`, `set_price`, `buy_price`, `per_item`, `input`, `output` |
| `auto_harvest` | `AutoHarvest` | `enabled`, `recycle`, `useFuel`, `storage`, `loop`, `add(world)`, `remove(world)` |
| `auto_plant` | `AutoPlant` | `enabled`, `storage`, `loop`, `add(world)`, `remove(world)` |
| `auto_event` | `AutoEvent` | `enabled`, `storage` |
| `auto_skin` | `AutoSkin` | `enabled`, `interval` |
| `auto_blarney` | `AutoBlarney` | `enabled`, `storage`; read-only `index`, `state`, `console` |
| `auto_set_pos` | `AutoSetPosition` | `enabled`, `warp_delay`, `move_delay`, `world` |
| `auto_clear` | `AutoClear` | `enabled`, `name_length`, `keep_jammer`, `keep_lock`, `no_number`, `output_path`, `item_storage`, `seed_storage` |
| `auto_build` | `AutoBuild` | `enabled`, build options, storage names, output path, and selected item IDs; read-only `console` |
| `auto_malady` | `AutoMalady` | `enabled`, treatment options, `disconnect_interval`, `storage`, `vial`, `addHospital`, `removeHospital` |
| `auto_zombie` | `AutoZombie` | `enabled`, `infect_mode`, `punch_mode`, `punch_door`, `cell_limit`, `zombie_world`, `gvirus_storage`, `reward_storage` |
| `auto_fossil` | `AutoFossil` | `enabled`, `loop_mode`, `use_rock`, `interval`, `tool_storage`, `fossil_storage` |
| `auto_combine` | `AutoCombine` | `enabled`, `output_id`, `world`, `ingredient_storage`, `output_world`, `setIngredient(index, id, count)` |
| `rotation` | `Rotation` | `enabled`, `status`, rest/fill/fossil/exchange options, storage suppression, break position, item IDs, and custom tiles |

Example:

```lua
local bot = getBot()
local fisher = bot.auto_fish

fisher.world = "FISHINGWORLD"
fisher.item_storage = "BAITSTORAGE"
fisher.reward_storage = "REWARDSTORAGE"
fisher:setRod(1234)
fisher:setBait(5678)
fisher.enabled = true
```

## Proxy management

```lua
local manager = getProxyManager()
manager:addProxy("127.0.0.1", 1080, "user", "pass")
manager:setLimit(3)
```

Global helpers are `getProxyManager()`, `getProxies()`, `addProxy(proxy_data)`, `addDataProxy(proxy_data, is_http)`, and `removeProxy(ip, port, username, password)`.

`ProxyManager` provides `proxies`, `addProxy`, `removeProxy`, `setLimit`, `spread`, `localize`, and settings named `auto_switch`, `auto_check`, `switch_on_shadow`, `switch_on_ban`, `switch_on_block`, `cooldown`, `bypass_cooldown`, `timeout`, and read-only `main_ip_reference`.

A proxy snapshot exposes read-only `ip`, `port`, `username`, and `password`. Managed proxies additionally expose `cooldown`, `country`, `reference`, `selected`, `setCooldown()`, `removeCooldown()`, and `hasCooldown()`.

Avoid printing proxy credentials or returning them from HTTP handlers.

## Switch and world managers

`getSwitchManager()` returns the global switch manager. Its public settings include automatic switch triggers, rest behavior, loop mode, limits, storage, and `drop_on_switch`. Its methods are `addBot`, `removeBot([name])`, `switch()`, and `exists(name)`.

`getWorldManager()` returns the rotation world manager. It supports:

- `addFarm(name)` and `removeFarm(name)`
- `addStorage(...)` and `removeStorage(name, storage_type)`
- `selectAll()` and `unselectAll()`
- `selectFarm(name)` and `unselectFarm(name)`
- `selectStorage(...)` and `unselectStorage(...)`
- `getRandomStorage(...)`

Use `StorageType.pack` or `StorageType.seed` when a storage type is required.

## HTTP client and webhook

### HTTP client

```lua
local client = HttpClient.new()
client.url = "https://example.invalid/status"
client:setMethod(Method.get)
client.timeout = 10

local result = client:request()
if result.error == 0 then
    print("HTTP " .. result.status)
end
```

`HttpClient` has writable `content`, `method`, `headers`, `url`, and `timeout`, plus `setMethod(method)`, `setProxy(type, ...)`, `removeProxy()`, and `request()`. The result has read-only `body`, `error`, `status`, and `getError()`.

Available methods are `Method.get`, `post`, `put`, `patch`, `delete`, and `head`. Proxy kinds are `Proxy.none`, `http`, `https`, and `socks5`.

### Webhook

```lua
local hook = Webhook.new("https://example.invalid/webhook")
hook.username = "Lucifer"
hook.content = "Script completed"
hook:send()
```

A webhook supports `url`, `message_id`, `content`, `username`, `avatar_url`, `embed1`, `embed2`, `makeContent()`, `send()`, and `edit()`.

Embeds support `use`, `color`, `title`, `type`, `description`, `url`, `thumbnail`, `timestamp`, `image`, `footer`, `author`, and `addField(name, value, inline)`. Never hard-code a real webhook URL in a script you plan to share.

### HTTP server

The main scripting context may expose `HttpServer`, `HttpRequest`, and `HttpResponse`. A server supports `get`, `post`, `put`, `delete`, `patch`, `setLogger`, and `listen(ip, port)`. Only one server can listen at a time.

Request handlers receive `(request, response)`. Use `request:getHeader(name)` and `request:getParam(name)` for lookups. Set a response with `response.status`, `response.body`, headers, or `response:setContent(body, content_type)`.

Bind only to trusted interfaces, validate all input, and never expose account or proxy data.

## Advanced protocol interface

Version 2.86 exposes `bot:sendPacket(type, text)` and `bot:sendRaw(packet)` for compatibility with advanced scripts. It also exposes packet and variant objects to event callbacks.

These interfaces are intentionally not expanded here. Their formats are protocol-version dependent, easy to misuse, and unsuitable as stable application APIs. Prefer typed bot actions, state queries, and event callbacks. A malformed packet may disconnect the bot or invalidate state.

## Enums

Enum members are symbolic; do not depend on their numeric values.

### `Platform`

`windows`, `android`, `macos`, `ios`, `ubiconnect`, `steam`, `apple`

### `Bubble`

`none`, `talk`, `brb`

### `Role`

`farm`, `build`, `surg`, `fish`, `cook`, `startopia`

### `Action`

`move`, `collect`, `hit`, `place`, `harvest`, `plant`, `warp`, `drop`, `trash`

### `GeigerArea`

`null`, `red`, `yellow`, `green`, `rapid`, `prize`

### `Event`

`variantlist`, `update_packet`, `track_packet`, `generic_text`, `game_message`, `mod_enter`, `got_punched`, `render`

### `BotStatus`

Common states are `offline`, `online`, `getting_server_data`, `retrieving_token`, `changing_subserver`, `captcha_requested`, and `stopped`.

Failure or restriction states include `wrong_password`, `account_banned`, `account_suspended`, `location_banned`, `version_update`, `advanced_account_protection`, `server_overload`, `too_many_login`, `maintenance`, `server_busy`, `guest_limit`, `ubi_disabled`, `account_restricted`, `network_restricted`, `http_block`, `bad_name_length`, `invalid_account`, `error_connecting`, `logon_fail`, `could_not_warp`, `bad_inventory`, `bad_gateway`, `server_issue`, `two_factor`, `google_logon_fail`, `steam_logon_fail`, `steam_not_linked`, `failed_captcha`, `banwave_detected`, and `sms_error`.

Informational states also include `mod_entered`, `player_entered`, `high_load`, `high_ping`, `bypassing_server_data`, `linking_steam`, and `solving_captcha`.

### `GoogleStatus`

`idle`, `processing`, `bad_server`, `bad_connection`, `invalid_key`, `missing_server`, `missing_local_server`, `timeout`, `driver_closed`, `init_error`, `invalid_credentials`, `invalid_secret`, `account_disabled`, `captcha_required`, `phone_required`, `recovery_required`, `otp_required`, `invalid_challenge`, `couldnt_verify`, `unknown_url`, `redirect`

## Troubleshooting

### A lookup returns `nil`

The requested bot, player, tile, object, NPC, inventory item, or catalog item does not exist in the current snapshot. Check for `nil` before reading it.

### World values suddenly changed

A warp, redirect, reconnect, or new map can replace world state. Query again after the transition and do not retain old state objects for long-running work.

### An action returned `nil`

Most action methods are fire-and-forget. `nil` is normal and does not indicate success or failure. Confirm the result through a later state update or event.

### Events never run

Make sure callbacks are registered before `listenEvents(timeout_seconds)`. Listening is bounded and blocking; call it again if the script needs another listening window.

### A script does not stop immediately

Long callbacks and blocking calls delay cooperative shutdown. Keep callbacks short and use bounded operations.

## Compatibility note

This document targets Lucifer Lua v2.86. Scripts should feature-check optional values when they may run on another release:

```lua
if bot.getSignal ~= nil then
    local signal = bot:getSignal()
    print(signal.x .. ", " .. signal.y)
end
```
