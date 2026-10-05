# 📜 Referensi Scripting Lua — Bothax & Lucifer API

Lucifer GTPS Bot dilengkapi mesin scripting **Lua 5.4.6** terintegrasi yang mendukung dua dialek scripting terpopuler di komunitas GTPS:
1. **Bothax Lua Docs API**: Standar modern berbasis objek tabel (`GetLocal()`, `GetWorld()`, `GetTiles()`, event hooks `OnVariant`, `OnSendPacket`, dll.).
2. **Lucifer Lua v2.86 API**: Standar ringkas berorientasi objek `bot:` (`bot:moveTo()`, `bot:hit()`, `bot:place()`, `log()`, `sleep()`).

---

## 1. Bothax Lua API Reference

### A. Struktur Objek Data (Bothax Structures)

#### `NetAvatar` (Data Karakter Pemain)
```lua
{
    name     = "GrowID",           -- Nama pemain (bersih dari color code)
    netid    = 1234,               -- NetID unik di world
    userid   = 5678,               -- UserID akun
    country  = "id",               -- Kode negara bendera
    isleft   = false,              -- true jika menghadap ke kiri
    pos      = { x = 640.0, y = 736.0 }, -- Koordinat pixel di world
    tile_x   = 20,                 -- Koordinat tile X (pos.x // 32)
    tile_y   = 23                  -- Koordinat tile Y (pos.y // 32)
}
```

#### `World` (Data World Saat Ini)
```lua
{
    name        = "START",         -- Nama world
    width       = 100,             -- Lebar world (standar: 100 tile)
    height      = 60,              -- Tinggi world (standar: 60 tile)
    tilecount   = 6000,            -- Total jumlah tile (width * height)
    objectcount = 15,              -- Jumlah item mengambang di world
    lastoid     = 102              -- UID objek terakhir yang dijatuhkan
}
```

#### `Tile` (Data Satu Petak Blok di World)
```lua
{
    x          = 10,               -- Koordinat tile X
    y          = 15,               -- Koordinat tile Y
    pos        = { x = 320.0, y = 480.0 }, -- Koordinat pixel
    fg         = 242,              -- Item ID foreground (242 = World Lock, 0 = kosong)
    bg         = 14,               -- Item ID background (14 = Cave Dirt)
    collidable = true,             -- true jika tidak bisa ditembus karakter
    coltype    = 1,                -- Tipe collision (0 = none, 1 = solid, 2 = platform, 3 = door)
    flags      = 0,                -- Visual bitflags
    extra      = 0                 -- Data tambahan (lock owner, seed growth)
}
```

#### `WorldObject` (Item Mengambang / Dropped Object)
```lua
{
    id     = 242,                  -- Item ID barang yang mengambang
    amount = 5,                    -- Jumlah tumpukan item
    flags  = 0,                    -- Object visual flags
    oid    = 18,                   -- Object UID unik untuk diambil (collect)
    pos    = { x = 320.0, y = 480.0 } -- Koordinat pixel
}
```

#### `Inventory` (Data Barang di Dalam Tas)
```lua
{
    id     = 18,                   -- Item ID
    amount = 1,                    -- Jumlah stok yang dimiliki bot
    flags  = 0                     -- Inventory slot flags
}
```

#### `ENetClient` (Informasi Jaringan Klien)
```lua
{
    address = "127.0.0.1",         -- Host server game
    port    = 17091,               -- Port server ENet
    ping    = 35                   -- RTT round trip time dalam milidetik
}
```

---

### B. Daftar Fungsi Global Bothax

| Fungsi | Return Type | Deskripsi |
|---|---|---|
| `GetLocal()` | `NetAvatar` | Mengambil data avatar bot kita sendiri. |
| `GetWorld()` | `World` | Mengambil status metadata world saat ini. |
| `GetInventory()` | `Inventory[]` | Mengambil array seluruh isi tas bot. |
| `GetTile(x, y)` | `Tile` | Mengambil data tile pada koordinat tile spesifik. |
| `GetTiles()` | `Tile[]` | Mengambil seluruh 6000 tile di world sebagai array. |
| `GetObjectList()` | `WorldObject[]`| Mengambil daftar semua dropped item di world. |
| `GetPlayerList()` | `NetAvatar[]` | Mengambil daftar semua pemain lain di world saat ini. |
| `GetPlayer(netid)`| `NetAvatar` | Mengambil satu pemain berdasarkan NetID. |
| `GetCamera()` | `WorldCamera` | Mengambil posisi kamera dan zoom level. |
| `GetClient()` | `ENetClient` | Mengambil alamat server, port, dan ping bot. |
| `FindPath(x, y)` | `boolean` | Berjalan ke tile `(x, y)` menggunakan pathfinding. |
| `CheckPath(x, y)`| `boolean` | Memeriksa apakah ada jalur bebas hambatan ke `(x, y)`. |
| `SendPacket(type, text)` | `void` | Mengirim paket teks (`type`: 2 = generic, 3 = game msg). |
| `SendPacketRaw(to_client, pkt)`| `void`| Mengirim Tank Packet biner mentah. |
| `SendVariantList(var, netid, delay)`| `void`| Mengirim VariantList RPC ke server/klien. |
| `Sleep(ms)` | `void` | Menghentikan eksekusi script selama `ms` milidetik. |
| `LogToConsole(str)` | `void` | Mencetak pesan ke console log GUI aplikasi. |
| `MakeRequest(url, method)` | `table` | Menjalankan HTTP GET/POST, mengembalikan `{status, content}`. |
| `Hash32(str)` | `number` | Menghitung hash integer 32-bit dari string. |
| `Hash64(str)` | `number` | Menghitung hash integer 64-bit dari string. |
| `GetItemByIDSafe(id)` | `table` | Mengambil metadata nama item berdasarkan ID. |

---

### C. Sistem Event Hook Bothax (`AddHook`)

Anda dapat mencegat paket masuk atau keluar, memproses pergerakan, atau memodifikasi visual render menggunakan fungsi hook:

```lua
AddHook(hook_name, hook_label, callback_function)
RemoveHook(hook_label)
RemoveHooks()
```

#### Daftar Nama Hook:

| Nama Hook | Argumen Callback | Keterangan |
|---|---|---|
| `"OnVariant"` | `(varlist, netid, delay)` | Terpicu saat server memanggil fungsi RPC VariantList. **Kembalikan `true` untuk memblokir paket agar tidak diproses game engine!** |
| `"OnSendPacket"` | `(type, text)` | Terpicu saat klien hendak mengirim paket teks ke server. Kembalikan `true` untuk membatalkan pengiriman. |
| `"OnSendPacketRaw"` | `(raw_packet_table)` | Terpicu saat Tank Packet mentah akan dikirim. |
| `"OnProcessTankUpdate"` | `(tank_struct)` | Terpicu saat ada paket biner TankPacket masuk dari server. |
| `"OnWorldTouch"` | `(pos_x, pos_y, tile_x, tile_y)` | Terpicu saat ada sentuhan atau interaksi di world. |
| `"OnDraw"` | `(delta_time)` | Terpicu setiap frame perenderan GUI (dapat menggambar ImGui tambahan). |
| `"OnInput"` | `(key, state)` | Terpicu saat ada penekanan tombol input keyboard. |

---

## 2. Lucifer Lua v2.86 API Reference

Dialek alternatif bergaya objek `bot:` untuk kemudahan otomasi:

| Method | Penjelasan |
|---|---|
| `bot:getLocal()` | Mengambil avatar bot (`NetAvatar`). |
| `bot:getWorld()` | Mengambil metadata world (`World`). |
| `bot:moveTo(tile_x, tile_y, speed)` | Berjalan ke koordinat tile dengan kecepatan langkah tertentu. |
| `bot:hit(tile_x, tile_y)` | Memukul / meninju tile blok (Punch Item ID = 18). |
| `bot:place(tile_x, tile_y, item_id)`| Memasang blok atau menanam benih di tile target. |
| `bot:wrench(tile_x, tile_y)` | Menggunakan kunci pas (wrench) pada tile target. |
| `bot:enter()` | Masuk ke dalam pintu / portal pada posisi bot berdiri. |
| `bot:collect(object_uid)` | Mengambil satu objek spesifik berdasarkan UID. |
| `bot:collectNearby(radius)` | Mengambil semua item yang berceceran di sekitar bot. |
| `bot:warp(world_name)` | Berpindah / masuk ke nama world target. |
| `bot:say(text)` | Mengirimkan pesan chat publik. |
| `bot:respawn()` | Mengirimkan permintaan respawn ke server. |
| `bot:setProxy(proxy_str)` | Mengatur proxy SOCKS5 (`host:port:user:pass`, `host:port`, atau `""` untuk Direct). |
| `bot:getProxy()` | Mengembalikan string konfigurasi proxy aktif saat ini. |
| `bot:testProxy(proxy_str)` | Menguji konektivitas proxy SOCKS5 dan mengembalikan `ok, latency_ms, msg`. |
| `sleep(ms)` | Menjeda eksekusi skrip selama `ms` milidetik. |
| `log(text)` | Mencetak pesan ke console log GUI Lucifer. |
| `setProxy(proxy_str)` | Global alias untuk `bot:setProxy()`. |
| `getProxy()` | Global alias untuk `bot:getProxy()`. |
| `testProxy(proxy_str)` | Global alias untuk menguji proxy SOCKS5 (`ok, latency_ms, msg`). |

---

## 3. Contoh Script Siap Pakai

### Contoh 1: Auto-Farm Blok Kayu (Wood Block / Dirt)
```lua
-- Template Auto-Farm Lucifer v2.86
local me = bot:getLocal()
local target_item_id = 2 -- Dirt Block (ganti sesuai target)
local farm_x = me.tile_x + 1
local farm_y = me.tile_y

log("🌾 Memulai Auto-Farm pada Tile: (" .. farm_x .. ", " .. farm_y .. ")")

for i = 1, 50 do
    -- 1. Pasang blok
    bot:place(farm_x, farm_y, target_item_id)
    sleep(160)

    -- 2. Pukul sampai pecah (3 kali pukulan)
    for hit_count = 1, 3 do
        bot:hit(farm_x, farm_y)
        sleep(160)
    end

    -- 3. Ambil item hasil panen
    bot:collectNearby(3.0)
    sleep(120)
end

log("✅ Siklus 50 blok selesai!")
```

### Contoh 2: Detektor & Radar World Lock (Bothax API)
```lua
-- Memindai seluruh World Lock di world saat ini
LogToConsole("🔍 Memulai pemindaian World Lock...")
local tiles = GetTiles()
local found = 0

for _, tile in pairs(tiles) do
    if tile.fg == 242 then -- 242 adalah Item ID World Lock
        found = found + 1
        LogToConsole("🔒 Ditemukan World Lock #" .. found .. " pada Tile (" .. tile.x .. ", " .. tile.y .. ")")
    end
end

if found == 0 then
    LogToConsole("ℹ️ Tidak ada World Lock yang ditemukan di world ini.")
else
    LogToConsole("🎉 Total World Lock ditemukan: " .. found)
end
```

### Contoh 3: Mencegat & Menyembunyikan Pesan Tertentu (`OnVariant`)
```lua
-- Blokir dialog pesan yang mengganggu dari server
AddHook("OnVariant", "MuteSpamHook", function(vars, netid, delay)
    if vars and #vars > 1 and vars[1].str_val == "OnConsoleMessage" then
        local msg = vars[2].str_val
        if msg:find("spam") or msg:find("broadcast") then
            LogToConsole("🛡️ Pesan spam diblokir: " .. msg)
            return true -- Blokir agar tidak diproses engine
        end
    end
    return false -- Teruskan paket normal
end)
```

### Contoh 4: Rotasi & Pengujian Proxy SOCKS5 Dinamis
```lua
local proxy_list = {
    "127.0.0.1:1080:user1:pass1",
    "192.168.1.50:1080",
    "10.0.0.1:1080:proxyuser:secret"
}

log("🌐 Menguji daftar proxy SOCKS5...")
for _, p in ipairs(proxy_list) do
    local ok, ping_ms, msg = testProxy(p)
    if ok and ping_ms < 250 then
        log(string.format("✅ Memilih proxy terbaik: %s (Ping: %.1f ms)", p, ping_ms))
        bot:setProxy(p)
        break
    else
        log(string.format("⚠️ Proxy lambat/gagal: %s (Info: %s)", p, msg))
    end
end
```

### Contoh 5: Menambahkan Bot Massal via `addBot(info)` (Template Lucifer v2.85)
```lua
local list_surat = [[
over_0000000000000000@
over_0000000000000000@
over_0000000000000000@
]]

for line in string.gmatch(list_surat, "([^\r\n]+)") do
    local email, token, mac, rid, wk = line:match("([^|]+)|([^|]+)|([^|]+)|([^|]+)|([^|]+)")
    if email and token and mac and rid and wk then
        local info = {
            display = email,
            secret = email,
            name = token,
            mac = mac,
            rid = rid,
            wk = wk,
            platform = 0,
        }
        local bot = addBot(info)
    end
end
```

