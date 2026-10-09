# 📜 Referensi Scripting Lua — Bothax & Lucifer API

Lucifer GTPS Bot dilengkapi mesin scripting **Lua 5.4.6** terintegrasi yang mendukung 100% spesifikasi resmi dokumentasi [Bothax Lua API](https://github.com/yeweroooo/bothax) serta dialek klasik **Lucifer Lua v2.86**.

Engine ini mendukung:
- **43 Fungsi Bothax** (tersedia dalam PascalCase dan camelCase alias).
- **16 Struktur Data Lengkap** (`NetAvatar`, `World`, `Tile`, `TileFlags`, `TileExtra`, `WorldObject`, `Inventory`, `PlayerItems`, `ClientNPC`, `WorldCamera`, `ENetClient`, `ItemInfo`, `GameUpdatePacket`/`TankUpdatePacket`, `Variant`, `Vec2`, `Vec3`).
- **Sistem Event Hooks** (`OnVariant`, `OnSendPacket`, `OnSendPacketRaw`, `OnProcessTankUpdate`, `OnWorldTouch`, `OnDraw`, `OnInput`) dengan kemampuan intercept/blocking (`return true`).
- **Enkripsi Skrip Bytecode / Obfuscation** (`Encrypt`, `EncryptFile`, `LoadEncrypt`, `LoadEncryptedFile`) berbasis XOR key dinamis.
- **Asinkron & Concurrency** (`RunDelayed`, `RunThread`) dengan pelestarian variadic arguments multi-tipe.

---

## 1. Bothax Lua API Reference

### A. 16 Struktur Objek Data (Bothax Structures)

#### 1. `NetAvatar` (Data Pemain / Karakter)
```lua
{
    name     = "GrowID",           -- Nama pemain (dibersihkan dari formatting/color code)
    netid    = 1234,               -- NetID unik di dalam world saat ini
    userid   = 5678,               -- UserID permanen akun
    country  = "id",               -- Kode negara bendera (2 huruf kecil)
    isleft   = false,              -- true jika menghadap ke kiri
    pos      = { x = 640.0, y = 736.0 }, -- Koordinat pixel di world (1 tile = 32.0 px)
    tile_x   = 20,                 -- Koordinat petak tile X (pos.x // 32)
    tile_y   = 23                  -- Koordinat petak tile Y (pos.y // 32)
}
```

#### 2. `World` (Metadata World Saat Ini)
```lua
{
    name        = "START",         -- Nama world aktif
    width       = 100,             -- Lebar world (standar: 100 tile)
    height      = 60,              -- Tinggi world (standar: 60 tile)
    tilecount   = 6000,            -- Total kapasitas tile (width * height)
    objectcount = 15,              -- Jumlah item terapung (dropped objects)
    lastoid     = 102              -- UID objek terakhir yang dijatuhkan
}
```

#### 3. `Tile` (Data Satu Petak Blok di World)
```lua
{
    x          = 10,               -- Koordinat tile X (0 .. width - 1)
    y          = 15,               -- Koordinat tile Y (0 .. height - 1)
    pos        = { x = 320.0, y = 480.0 }, -- Koordinat pixel
    fg         = 242,              -- Item ID foreground (242 = World Lock, 0 = kosong)
    bg         = 14,               -- Item ID background (14 = Cave Dirt, 0 = kosong)
    collidable = true,             -- true jika memiliki collision fisik
    coltype    = 1,                -- Collision type (0: none, 1: solid, 2: platform, 3: door)
    locktile   = 0,                -- Indeks tile lock pemilik area
    flags      = TileFlags,        -- Tabel bitflags visual (lihat struktur TileFlags)
    extra      = TileExtra         -- Tabel data tambahan (lihat struktur TileExtra)
}
```

#### 4. `TileFlags` (Bitflags Atribut Tile)
```lua
{
    locked   = false,              -- True jika tile terlindungi lock
    spliced  = false,              -- True jika pohon hasil persilangan benih (spliced)
    dropseed = false,              -- True jika tile menjatuhkan seed
    tree     = false,              -- True jika tile merupakan pohon yang sedang tumbuh
    flipped  = false,              -- True jika sprite tile terbalik horizontal
    enabled  = true,               -- Status toggle aktif tile
    public   = false,              -- True jika akses lock bersifat publik
    silenced = false,              -- True jika efek suara dinonaktifkan
    water    = false,              -- True jika tergenang air
    glue     = false,              -- True jika terlumuri lem
    burn     = false,              -- True jika terbakar api
    red      = false,              -- Paint flag merah
    green    = false,              -- Paint flag hijau
    blue     = false               -- Paint flag biru
}
```

#### 5. `TileExtra` (Data Tambahan Tile)
```lua
{
    fruitcount = 0,                -- Jumlah buah pada pohon (0 .. 4)
    growtime   = 0,                -- Waktu sisa pertumbuhan pohon dalam detik
    open       = false,            -- Status pintu/gerbang (terbuka / tertutup)
    expression = 0,                -- Ekspresi visual blok khusus
    label      = ""                -- Teks label / tulisan sign board
}
```

#### 6. `WorldObject` (Item Terapung / Dropped Item)
```lua
{
    id     = 242,                  -- Item ID barang
    amount = 5,                    -- Jumlah tumpukan item
    flags  = 0,                    -- Object bitflags
    oid    = 18,                   -- Object UID unik (digunakan untuk collect)
    pos    = { x = 320.0, y = 480.0 } -- Posisi pixel
}
```

#### 7. `Inventory` (Item Slot di Dalam Tas)
```lua
{
    id     = 18,                   -- Item ID
    amount = 1,                    -- Jumlah kepemilikan
    flags  = 0                     -- Inventory slot flags
}
```

#### 8. `PlayerItems` (Inventaris Tas & Status Perlengkapan Karakter)
```lua
{
    gems     = 50000,              -- Jumlah Gems bot
    backpack = {                   -- Status tas
        priority = 0,
        size     = 40,             -- Kapasitas maksimum tas
        selected = 18              -- Item ID yang sedang dipegang
    },
    hair     = 0,                  -- Item ID slot rambut
    shirt    = 0,                  -- Item ID slot baju
    pants    = 0,                  -- Item ID slot celana
    feet     = 0,                  -- Item ID slot sepatu
    face     = 0,                  -- Item ID slot wajah (kacamata dsb)
    hand     = 0,                  -- Item ID slot tangan
    back     = 0,                  -- Item ID slot sayap / punggung
    mask     = 0,                  -- Item ID slot topeng
    necklace = 0,                  -- Item ID slot kalung
    ances    = 0                   -- Item ID slot ancestor
}
```

#### 9. `ClientNPC` (Entitas NPC di World)
```lua
{
    type  = 1,                     -- Tipe jenis NPC
    pos   = { x = 320.0, y = 480.0 }, -- Posisi pixel saat ini
    dest  = { x = 400.0, y = 480.0 }, -- Posisi pixel target tujuan
    speed = 100.0,                 -- Kecepatan pergerakan NPC
    netid = 10                     -- NetID unik NPC
}
```

#### 10. `WorldCamera` (Posisi & Konfigurasi Kamera)
```lua
{
    pos  = { x = 640.0, y = 736.0 }, -- Koordinat tengah kamera
    size = { x = 1280.0, y = 720.0 },-- Dimensi viewport layar
    zoom = 1.0                     -- Rasio perbesaran zoom
}
```

#### 11. `ENetClient` (Status Jaringan ENet Klien)
```lua
{
    address = "127.0.0.1",         -- Host/IP server game yang terhubung
    port    = 17091,               -- Port server ENet
    ping    = 35                   -- RTT latency (Round Trip Time) dalam ms
}
```

#### 12. `ItemInfo` (Metadata Item dari `items.dat`)
```lua
{
    id           = 242,            -- Item ID
    name         = "World Lock",   -- Nama resmi item
    rarity       = 999,            -- Nilai kelangkaan
    growtime     = 0,              -- Durasi waktu panen (detik)
    breakhit     = 100,            -- Jumlah pukulan untuk menghancurkan
    clothingtype = 0,              -- Tipe pakaian / wearable
    collidable   = true,           -- true jika tidak bisa ditembus
    isdoor       = false           -- true jika berfungsi sebagai pintu/portal
}
```

#### 13. `GameUpdatePacket` / `TankUpdatePacket` (Paket Biner 56-Byte)
```lua
{
    type       = 0,                -- Tipe paket (0: state, 3: tile change, dsb)
    pos        = { x = 640.0, y = 736.0 },
    x          = 640.0,            -- Alias koordinat X
    y          = 736.0,            -- Alias koordinat Y
    pos_x      = 640.0,            -- Alias pos_x
    pos_y      = 736.0,            -- Alias pos_y
    value      = 0,                -- Item ID / target data value
    int_data   = 0,                -- Alias int_data
    netid      = 1234,             -- NetID pelaku
    snetid     = -1,               -- Secondary NetID
    secondnetid= -1,               -- Alias secondnetid
    flags      = 0,                -- Movement & action bitflags
    state      = 0,                -- State modifier
    dropped    = 0,                -- Dropped object identifier
    xspeed     = 0.0,              -- Kecepatan horizontal
    yspeed     = 0.0,              -- Kecepatan vertikal
    px         = 20,               -- Punch/place tile X
    tx         = 20,               -- Alias target X
    int_x      = 20,               -- Alias int_x
    py         = 23,               -- Punch/place tile Y
    ty         = 23,               -- Alias target Y
    int_y      = 23                -- Alias int_y
}
```

#### 14. `Variant` (Objek Varian RPC Data)
Tipe data individual dalam `VariantList`:
- `type = 1`: String (`"teks"`)
- `type = 2`: Float (`123.45`)
- `type = 3`: Vec2 (`{ x = 100.0, y = 200.0 }`)
- `type = 4`: Vec3 (`{ x = 100.0, y = 200.0, z = 300.0 }`)
- `type = 5`: Unsigned Int 32-bit (`uint_val`)
- `type = 9`: Signed Int 32-bit (`int_val`)

#### 15. `Vec2`
`{ x = 100.0, y = 200.0 }`

#### 16. `Vec3`
`{ x = 100.0, y = 200.0, z = 300.0 }`

---

### B. 43 Fungsi Global Bothax

Semua fungsi di bawah ini dapat dipanggil dengan **PascalCase** (standar Bothax) maupun **camelCase**:

| No | Fungsi (PascalCase) | Alias (camelCase) | Return Type | Deskripsi Singkat |
|:---|:---|:---|:---|:---|
| 1 | `AddHook(event, label, fn)` | `addHook` | `void` | Mendaftarkan listener callback untuk event jaringan/engine. |
| 2 | `RemoveHook(label)` atau `(event, label)` | `removeHook` | `void` | Menghapus callback hook berdasarkan label identifier. |
| 3 | `RemoveHooks()` | `removeHooks` | `void` | Menghapus semua hook yang pernah didaftarkan. |
| 4 | `GetLocal()` | `getLocal` | `NetAvatar` | Mengambil data avatar bot kita sendiri. |
| 5 | `GetWorld()` | `getWorld` | `World` | Mengambil metadata world saat ini. |
| 6 | `GetTile(x, y)` | `getTile` | `Tile` | Mengambil data tile pada koordinat spesifik `(x, y)`. |
| 7 | `GetTiles()` | `getTiles` | `Tile[]` | Mengambil array seluruh 6000 tile di world. |
| 8 | `SetTileFlags(x, y, flags)` | `setTileFlags` | `void` | Mengubah bitflags visual tile secara langsung. |
| 9 | `GetObjectList()` | `getObjectList` | `WorldObject[]` | Mengambil daftar semua dropped item di world. |
| 10 | `GetPlayerList()` | `getPlayerList` | `NetAvatar[]` | Mengambil daftar semua pemain lain di world saat ini. |
| 11 | `GetPlayer(id_or_name)` | `getPlayer` | `NetAvatar \| nil` | Mengambil pemain berdasarkan NetID atau nama (case-insensitive). |
| 12 | `GetPlayerInfo()` | `getPlayerInfo` | `PlayerItems` | Mengambil status tas, gems, dan slot equipment karakter. |
| 13 | `GetPlayerItems()` | `getPlayerItems` | `PlayerItems` | Alias identik dari `GetPlayerInfo()`. |
| 14 | `GetInventory()` | `getInventory` | `Inventory[]` | Mengambil array isi tas bot. |
| 15 | `GetCamera()` | `getCamera` | `WorldCamera` | Mengambil posisi kamera dan zoom level viewport. |
| 16 | `GetClient()` | `getClient` | `ENetClient` | Mengambil alamat host server, port, dan ping (ms). |
| 17 | `GetNPC(netid)` | `getNPC` | `ClientNPC \| nil` | Mengambil data NPC berdasarkan NetID. |
| 18 | `GetNPCList()` | `getNPCList` | `ClientNPC[]` | Mengambil array seluruh NPC di world. |
| 19 | `FindPath(x, y)` | `findPath` | `boolean` | Melangkah menuju tile `(x, y)` menggunakan A* pathfinding. |
| 20 | `CheckPath(x, y)` | `checkPath` | `boolean` | Mengecek apakah ada rute bebas hambatan ke `(x, y)` via A*. |
| 21 | `SendPacket(type, text)` | `sendPacket` | `void` | Mengirim paket teks ENet (`type = 2`: generic, `3`: game msg). |
| 22 | `SendPacketRaw(to_client, pkt)` atau `(pkt)` | `sendPacketRaw` | `void` | Mengirim paket biner TankPacket mentah ke jaringan. |
| 23 | `SendVariantList(varlist, [netid], [delay])` | `sendVariantList` | `void` | Mengirim RPC VariantList dengan packing otomatis. |
| 24 | `Sleep(ms)` | `sleep` | `void` | Menjeda eksekusi thread script selama `ms` milidetik. |
| 25 | `LogToConsole(str)` | `logToConsole` | `void` | Mencetak pesan ke console log GUI Lucifer dan terminal. |
| 26 | `MakeRequest(url, [method], [headers])` | `makeRequest` | `table` | Melakukan HTTP GET/POST (`{content, status, headers, error}`). |
| 27 | `Hash32(str)` | `hash32` | `integer` | Menghitung hash integer 32-bit (FNV-1a / Jenkins). |
| 28 | `Hash64(str)` | `hash64` | `integer` | Menghitung hash integer 64-bit presisi penuh. |
| 29 | `Encrypt(str, [key])` | `encrypt` | `string` | Mengenkripsi string plaintext menjadi byte-string terenkripsi. |
| 30 | `EncryptFile(path, [key])` | `encryptFile` | `boolean` | Mengenkripsi file skrip di disk. |
| 31 | `LoadEncrypt(str, [key])` | `loadEncrypt` | `function \| nil` | Mendekripsi dan meng-compile chunk string menjadi fungsi. |
| 32 | `LoadEncryptedFile(path, [key])` | `loadEncryptedFile` | `function \| nil` | Membaca file terenkripsi dan meng-compile fungsi yang siap dipanggil. |
| 33 | `RunDelayed(delay_ms, fn, ...)` | `runDelayed` | `void` | Menjalankan fungsi setelah jeda waktu tertentu (mendukung multi-args). |
| 34 | `RunThread(fn, ...)` | `runThread` | `void` | Menjalankan fungsi di worker thread terpisah secara asinkron. |
| 35 | `GetItemByIDSafe(id)` | `getItemByIDSafe` | `ItemInfo` | Mengambil metadata item dari `items.dat` secara aman. |
| 36 | `IsCollidable(id)` | `isCollidable` | `boolean` | Mengecek apakah suatu item ID padat/dapat ditabrak. |
| 37 | `IsDoor(id)` | `isDoor` | `boolean` | Mengecek apakah suatu item ID merupakan pintu/portal. |
| 38 | `RequestTileChange(x, y, id)` | `requestTileChange` | `void` | Melakukan interaksi penempatan/pemukulan tile via paket game. |
| 39 | `HandleTouchAtWorldCoordinates(x, y, id)` | `handleTouchAtWorldCoordinates` | `void` | Melakukan interaksi sentuh tile di world. |
| 40 | `ModFly(enable)` | `modFly` | `void` | Mengaktifkan/menonaktifkan mode terbang bebas hambatan. |
| 41 | `CanSeeGhost(enable)` | `canSeeGhost` | `void` | Mengaktifkan/menonaktifkan visibilitas entitas hantu. |
| 42 | `IsDarkened(enable)` | `isDarkened` | `void` | Mengatur visual mode gelap di world. |
| 43 | `NoName(enable)` | `noName` | `void` | Mengaktifkan mode nama anonim pada paket pergerakan bot. |

#### Utilitas Tambahan
- `ChangeValue(key, value)` / `changeValue`: Mengubah cheat internal (`modfly`, `noname`, `canseeghost`, `speed`, `punchrange`, `buildrange`, `autoreconnect`).
- `GetGameLogic()` / `getGameLogic`: Mengembalikan base pointer logika game (emulasi pointer runtime aman).
- `GetBaseApp()` / `getBaseApp`: Mengembalikan base pointer aplikasi (emulasi pointer runtime aman).

---

### C. Sistem Event Hook Bothax (`AddHook` / `RemoveHook`)

```lua
AddHook(event_name, hook_label, callback_function)
RemoveHook(hook_label)                -- Mendukung signature 1 argumen
RemoveHook(event_name, hook_label)    -- Mendukung signature 2 argumen
RemoveHooks()                         -- Menghapus semua hook
```

#### Event Names & Callback Signatures

| Event Name | Parameter Callback | Efek Kembalian `return true` |
|:---|:---|:---|
| `"OnVariant"` | `(varlist, netid, delay)` | **Memblokir** variant agar tidak diproses oleh game engine. |
| `"OnSendPacket"` | `(type, text)` | **Membatalkan** pengiriman paket teks ke server. |
| `"OnSendPacketRaw"` | `(tank_packet_table)` | **Membatalkan** pengiriman paket TankPacket ke server. |
| `"OnProcessTankUpdate"` | `(tank_packet_table)` | **Memblokir** paket tank masuk dari server. |
| `"OnWorldTouch"` | `(pos_x, pos_y, tile_x, tile_y)` | **Memblokir** proses sentuhan interaksi. |
| `"OnDraw"` | `(delta_time)` | Non-blockable. Digunakan untuk drawing custom ImGui. |
| `"OnInput"` | `(key, state)` | **Memblokir** propagasi tombol keyboard. |

---

## 2. Lucifer Lua v2.86 API Reference

Dialek berorientasi objek `bot:` untuk kemudahan tugas otomasi:

| Method | Penjelasan |
|:---|:---|
| `bot:getLocal()` | Mengambil avatar bot (`NetAvatar`). |
| `bot:getWorld()` | Mengambil metadata world (`World`). |
| `bot:moveTo(tile_x, tile_y, speed)` | Berjalan ke koordinat tile dengan interval langkah tertentu. |
| `bot:findPath(tile_x, tile_y)` | Berjalan ke tile `(x, y)` menggunakan A* pathfinding. |
| `bot:checkPath(tile_x, tile_y)` | Mengecek apakah rute ke `(x, y)` dapat dicapai tanpa rintangan. |
| `bot:hit(tile_x, tile_y)` | Memukul / meninju tile blok (Punch Item ID = 18). |
| `bot:place(tile_x, tile_y, item_id)` | Memasang blok atau menanam benih di tile target. |
| `bot:wrench(tile_x, tile_y)` | Menggunakan kunci pas (wrench) pada tile target. |
| `bot:enter()` | Masuk ke pintu / portal pada posisi bot berdiri. |
| `bot:requestTileChange(x, y, id)` | Mengubah tile secara langsung (RequestTileChange emulation). |
| `bot:handleTouch(x, y, id)` | Menjalankan interaksi sentuh tile di world. |
| `bot:isCollidable(item_id)` | Mengecek apakah suatu item ID solid/dapat ditabrak. |
| `bot:isDoor(item_id)` | Mengecek apakah suatu item ID merupakan pintu. |
| `bot:modFly(enable)` | Mengaktifkan mode terbang (ModFly). |
| `bot:canSeeGhost(enable)` | Mengaktifkan kemampuan melihat hantu di world. |
| `bot:isDarkened(enable)` | Mengatur mode kegelapan world. |
| `bot:noName(enable)` | Mengaktifkan mode nama anonim. |
| `bot:collect(object_uid)` | Mengambil satu objek spesifik berdasarkan UID. |
| `bot:collectNearby(radius)` | Mengambil semua item yang berceceran di sekitar bot. |
| `bot:warp(world_name)` | Berpindah / masuk ke nama world target. |
| `bot:say(text)` | Mengirimkan pesan chat publik. |
| `bot:respawn()` | Mengirimkan permintaan respawn ke server. |
| `bot:setProxy(proxy_str)` | Mengatur proxy SOCKS5 (`host:port:user:pass`, `host:port`, atau `""`). |
| `bot:getProxy()` | Mengembalikan string konfigurasi proxy aktif. |
| `bot:testProxy(proxy_str)` | Menguji konektivitas proxy SOCKS5 (`ok, latency_ms, msg`). |
| `sleep(ms)` | Menjeda eksekusi skrip selama `ms` milidetik. |
| `log(text)` | Mencetak pesan ke console log GUI Lucifer. |
| `setProxy(proxy_str)` | Global alias untuk `bot:setProxy()`. |
| `getProxy()` | Global alias untuk `bot:getProxy()`. |
| `testProxy(proxy_str)` | Global alias untuk `bot:testProxy()`. |

---

## 3. Contoh Script Terverifikasi (Official Bothax Examples)

### Contoh 1: Radar & Pelacak Pemain di World
```lua
local local_player = GetLocal()
LogToConsole("Nama Saya: " .. local_player.name)
LogToConsole("Posisi Saya: " .. local_player.tile_x .. ", " .. local_player.tile_y)

local players = GetPlayerList()
LogToConsole("Jumlah pemain di world: " .. #players)

for _, p in ipairs(players) do
    LogToConsole(string.format("Pemain: %s (NetID: %d) di tile (%d, %d)", 
        p.name, p.netid, p.tile_x, p.tile_y))
end
```

### Contoh 2: Detektor & Pemindaian Tile World Lock
```lua
LogToConsole("🔍 Memulai pemindaian World Lock...")
local tiles = GetTiles()
local found = 0

for _, tile in pairs(tiles) do
    if tile.fg == 242 then -- 242 = World Lock
        found = found + 1
        LogToConsole(string.format("🔒 World Lock #%d di tile (%d, %d) [Collidable: %s]",
            found, tile.x, tile.y, tostring(tile.collidable)))
    end
end

if found == 0 then
    LogToConsole("ℹ️ Tidak ada World Lock di world ini.")
end
```

### Contoh 3: Intersepsi & Filter Paket (`OnVariant` Hook)
```lua
AddHook("OnVariant", "DialogBlocker", function(var, netid, delay)
    if var and (var[0] == "OnDialogRequest" or (var[1] and var[1].str_val == "OnDialogRequest")) then
        LogToConsole("🛡️ Dialog popup diblokir dari server!")
        return true -- Mengembalikan true membatalkan dialog
    end
    return false
end)
```

### Contoh 4: Enkripsi Script & Dynamic Execution (`Encrypt` & `LoadEncrypt`)
```lua
local script_code = [[
    LogToConsole("Halo dari kode terenkripsi!")
    local me = GetLocal()
    LogToConsole("Posisi tile: " .. me.tile_x .. ", " .. me.tile_y)
]]

-- 1. Enkripsi string kode dengan custom seed
local encrypted = Encrypt(script_code, "Rahasia123")
LogToConsole("Panjang byte terenkripsi: " .. #encrypted)

-- 2. Dekripsi dan compile menjadi fungsi yang dapat dieksekusi
local fn = LoadEncrypt(encrypted, "Rahasia123")
if fn then
    fn() -- Eksekusi fungsi
    LogToConsole("✅ Skrip terenkripsi berhasil dijalankan!")
end
```

### Contoh 5: Timer & Asinkron Multithreading (`RunDelayed` & `RunThread`)
```lua
-- 1. Menjalankan fungsi setelah jeda 1000 milidetik
RunDelayed(1000, function(tag, count)
    LogToConsole(string.format("⏰ Timer berbunyi! Tag: %s, Count: %d", tag, count))
end, "DelayedAlarm", 42)

-- 2. Menjalankan task latar belakang (background thread)
RunThread(function(target_x, target_y)
    LogToConsole("🧵 Thread latar belakang dimulai...")
    if CheckPath(target_x, target_y) then
        FindPath(target_x, target_y)
        LogToConsole("🎯 Bot berhasil mencapai tujuan di background thread!")
    end
end, 10, 20)
```
