# GTPS Bot — Custom Growtopia Private Server Bot

> Bot client untuk **Growtopia Private Server (GTPS)**, dibangun dari nol sebagai alternatif yang dapat dikustomisasi dari Lucifer.
>
> 🚧 **Status:** Fase 1 (CLI) — implementasi dasar sudah dibuat, tetapi **belum diverifikasi pada GTPS nyata**.

---

## 🎯 Tentang Project

**GTPS Bot** adalah bot client berbasis **C++17** yang dirancang untuk berkomunikasi dengan server GTPS melalui dua jalur utama:

- **HTTP/HTTPS** untuk proses login dan mendapatkan informasi koneksi.
- **ENet** untuk koneksi game dan pertukaran paket Growtopia.

Saat ini project berfokus pada fondasi **CLI (Command Line Interface)**. Tahap berikutnya mencakup parser paket, action dasar, scripting Lua, multi-bot, automation, hingga GUI.

### Visi

Target jangka panjang project ini adalah membangun bot GTPS yang memiliki kemampuan setara secara konsep dengan Lucifer, tetapi tetap **open, modular, dan dapat dikustomisasi** untuk kebutuhan GTPS.

---

## 📌 Progress

| Fase | Fokus | Status |
|:---:|---|:---:|
| **1** | CLI, HTTP login, ENet connect, kirim/terima paket, auto-warp | ✅ Terverifikasi pada GTPS nyata (CreativePS) |
| **2** | Parser paket: world tiles, inventory, player tracking, dropped objects | ✅ Selesai (TankPacket 56b & WorldState) |
| **3** | Action: `move`, `punch`, `place`, `wrench`, `collect`, `drop`, `trash`, `farm` | ✅ Selesai (BotActions & AutoFarm) |
| **4** | Scripting Lua + API bergaya Lucifer Lua v2.86 | ⏳ Tahap Berikutnya |
| **5** | Multi-bot, auto-reconnect, proxy | ⏳ Belum |
| **6** | GUI menggunakan Dear ImGui + DirectX 11 (Native Desktop Window) | ✅ Selesai (Lucifer Desktop Suite) |

> **Status build:** ✅ Terverifikasi & berhasil dibuild di Windows (`build/Release/gtps_bot.exe`).
> **Status Fase 1:** ✅ Berhasil terhubung ke GTPS, melewati web validation & Discord auto-auth, dan auto-warp ke world tujuan.

---

## 🏗️ Arsitektur

```text
┌──────────────────────────────────────────────────────────┐
│                      GTPS Bot (C++)                      │
│                                                          │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────┐  │
│  │   HTTP Client  │  │   ENet Client  │  │ CLI / REPL │  │
│  │  cpp-httplib   │  │     libenet    │  │            │  │
│  └───────┬────────┘  └───────┬────────┘  └─────┬──────┘  │
│          │                   │                  │         │
│          │ login             │ paket game       │ command │
│          ▼                   ▼                  ▼         │
│  ┌────────────────────────────────────────────────────┐  │
│  │              Main Loop (poll + dispatch)           │  │
│  └────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
                │ HTTPS/HTTP          │ UDP (ENet)
                ▼                     ▼
     ┌────────────────────┐   ┌────────────────────────┐
     │  GTPS HTTP Server  │   │   GTPS ENet Server    │
     │   login, akun      │   │ game world, chat      │
     └────────────────────┘   └────────────────────────┘
```

### Komponen

| Komponen | File / Library | Fungsi |
|---|---|---|
| HTTP Client | `cpp-httplib` | Login ke GTPS dan mengambil token + server IP |
| ENet Client | `libenet` | Koneksi UDP dan kirim/terima paket game |
| Logger | `logger.hpp` | Timestamp, hexdump, dan logging thread-safe |
| Packet Helpers | `packet.hpp` | `ByteReader` / `ByteWriter` untuk encode/decode |
| CLI | `cli.hpp/cpp` | REPL interaktif pada thread terpisah dari main loop |

---

## 📁 Struktur Project

```text
gtps-bot/
├── CMakeLists.txt              # Build config + FetchContent
├── .env.example                # Template konfigurasi environment
├── .env                        # Konfigurasi runtime (git-ignored)
├── README.md                   # Dokumentasi project
└── src/
    ├── main.cpp                # Entry point
    ├── env.hpp                 # Parser file .env
    ├── cli.hpp / cli.cpp       # REPL dan command
    ├── logger.hpp              # Logging + hexdump
    ├── packet.hpp              # ByteReader/Writer + konstanta paket
    ├── http_client.hpp/cpp     # HTTP login ke GTPS
    └── enet_client.hpp/cpp     # Wrapper libenet
```

---

## 🔧 Dependencies

Dependency utama diambil otomatis melalui **CMake `FetchContent`**.

| Library | Versi | Fungsi |
|---|---|---|
| [ENet](https://github.com/lsalzman/enet) | `v1.3.17` | Networking UDP |
| [nlohmann/json](https://github.com/nlohmann/json) | `v3.11.3` | Parsing JSON |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | `v0.15.3` | HTTP/HTTPS client |
| OpenSSL | System | HTTPS — opsional |

### Prasyarat

- Compiler **C++17**: MSYS2/MinGW, Visual Studio, `g++`, atau `clang`
- **CMake 3.16+**
- **Git**
- **OpenSSL** — opsional, untuk HTTPS
- Koneksi internet saat dependency pertama kali di-fetch

---

## 🚀 Build

### Windows — MSYS2 + MinGW

Direkomendasikan untuk workflow Windows.

```bash
pacman -S --needed \
    mingw-w64-x86_64-toolchain \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-git \
    mingw-w64-x86_64-openssl

cd gtps-bot
mkdir -p build
cd build

cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
mingw32-make -j4

./gtps_bot.exe ../config.json
```

### Windows — Visual Studio 2022

1. Buka **Visual Studio 2022**.
2. Pilih **File → Open → CMake**.
3. Buka `CMakeLists.txt`.
4. Jalankan **Build → Build All** (`Ctrl+Shift+B`).
5. Jalankan binary dari:

```text
out/build/x64-Release/gtps_bot.exe config.json
```

### Linux / macOS

#### Linux

```bash
sudo apt install -y build-essential cmake git libssl-dev

mkdir build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

./gtps_bot ../config.json
```

#### macOS

```bash
xcode-select --install
brew install cmake openssl

mkdir build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)

./gtps_bot ../config.json
```

---

## ⚙️ Konfigurasi

Edit `config.json` sesuai GTPS yang digunakan:

```json
{
  "gtps": {
    "login_url": "http://127.0.0.1/api/login",
    "use_https": false,
    "game_server": "127.0.0.1",
    "game_port": 17091
  },
  "account": {
    "name": "mygrowid",
    "password": "mypassword"
  },
  "client": {
    "version": "5.30",
    "platform": "windows"
  },
  "logging": {
    "verbose": true,
    "hexdump": false
  }
}
```

### Field Configuration

| Field | Deskripsi |
|---|---|
| `login_url` | URL HTTP login GTPS; endpoint dapat berbeda tiap server |
| `use_https` | `true` jika GTPS menggunakan HTTPS |
| `game_server` | IP/domain server ENet |
| `game_port` | Port ENet; default yang digunakan konfigurasi ini adalah `17091` |
| `account.name` | GrowID / username GTPS |
| `account.password` | Password akun |
| `client.version` | Versi client yang diklaim, contoh `5.30` |
| `client.platform` | `windows`, `android`, `ios`, atau `macos` |
| `logging.verbose` | Menampilkan log umum |
| `logging.hexdump` | Menampilkan hexdump setiap paket untuk debugging |

> ⚠️ **Catatan penting:** endpoint login, nama field request, dan format handshake dapat berbeda antar-GTPS. Contoh di atas adalah baseline project, bukan jaminan kompatibilitas dengan semua GTPS.

---

## 🎮 Penggunaan CLI

Setelah build dan menjalankan bot:

```text
========================================
  GTPS Bot CLI — Fase 1
========================================

[12:00:00.001] [*] Login URL : http://127.0.0.1/api/login
[12:00:00.001] [*] Game      : 127.0.0.1:17091
[12:00:00.001] [*] Akun      : mygrowid

--- HTTP Login ---
[12:00:00.010] [HTTP] Status: 200
[12:00:00.150] [+] HTTP login OK

--- ENet Connect ---
[12:00:00.160] [ENet] ✅ Terhubung

========================================
  Bot siap. Ketik 'help' untuk command.
========================================

> help
```

### Command Suite (Lucifer Style)

| Kategori | Command | Alias | Fungsi |
|---|---|---|---|
| **Movement & Tiles** | `pos` | `where` | Menampilkan koordinat bot (pixel & tile) |
| | `move <x> <y>` | `m` | Berjalan halus (*smooth walk*) ke tile (x, y) |
| | `punch <x> <y>` | `p`, `hit` | Memukul / menghancurkan blok pada tile (x, y) |
| | `place <id> <x> <y>` | — | Memasang blok / bibit dengan ID pada tile (x, y) |
| | `wrench <x> <y>` | — | Membuka dialog wrench pada tile (x, y) |
| | `enter` | — | Mengaktifkan pintu / portal di posisi bot |
| **Backpack & Items** | `inv` | `inventory` | Melihat daftar item di backpack & jumlah slot |
| | `collect [radius]` | `c` | Mengambil drop item di sekitar (default: 4 tile) |
| | `drop <id> <count>` | — | Menjatuhkan item dari backpack |
| | `trash <id> <count>` | — | Membuang / trash item dari backpack |
| **World & Players** | `world` | — | Menampilkan info world, dimensi, dan tile saat ini |
| | `players` | `who` | Menampilkan daftar pemain di world (NetID, UserID, Pos) |
| | `warp <world>` | `w` | Berpindah ke world lain |
| | `say <text>` | `s` | Mengirim pesan chat ke world |
| | `respawn` | — | Respawn bot kembali ke pintu awal world |
| **Automation** | `farm <id> <x> <y> [ms]` | — | Menjalankan loop auto-farm Lucifer (place + punch + collect) |
| | `stopfarm` | — | Menghentikan loop auto-farm |
| **Sistem & Debug** | `help` | `?` | Menampilkan daftar command |
| | `status` | `st` | Menampilkan status koneksi & state bot |
| | `raw <hex>` | — | Mengirim paket mentah untuk debugging |
| | `clear` | `cls` | Membersihkan layar konsol |
| | `quit` | `exit`, `q` | Menghentikan bot dan disconnect |

### Contoh

```text
> say Halo dunia!
[ENet] C->S type=2 len=13
[ENet] S->C type=3 len=20
  💬 mygrowid: Halo dunia!

> warp START
[ENet] C->S type=2 len=32

> raw 03000000050048656C6C6F
[*] Raw send type=3 len=5
```

---

## 🧠 Detail Teknis

### Format Paket Growtopia melalui ENet

Setiap paket yang diproses project ini menggunakan struktur dasar:

```text
[uint32 type] [payload bytes...]
```

`type` disimpan sebagai **little-endian `uint32`**.

### Network Message Type

| Konstanta | Nilai | Fungsi |
|---|---:|---|
| `NET_MESSAGE_SERVER_HELLO` | `1` | Server greeting |
| `NET_MESSAGE_GENERIC_TEXT` | `2` | Text packet — chat, login, warp |
| `NET_MESSAGE_GAME_MESSAGE` | `3` | Dialog / chat dari server |
| `NET_MESSAGE_GAME_PACKET` | `4` | Action packet — move, place, hit |
| `NET_MESSAGE_ERROR` | `5` | Error dari server |
| `NET_MESSAGE_TRACK` | `6` | Track data |

### Text Packet

Untuk packet type yang menggunakan format text:

```text
[uint16 length] [string bytes]
```

String berupa raw bytes dan **bukan null-terminated**. Panjang string ditentukan oleh field `length`.

### Game Packet

Struktur dasar yang digunakan project:

```text
[uint32 net_id] [uint8 action_type] [payload sesuai action...]
```

Beberapa action type yang masih perlu diverifikasi:

| Nilai | Action |
|---:|---|
| `0` | `STATE` |
| `1` | `CALL_FUNCTION` |
| `3` | `TILE_CHANGE_REQUEST` |
| `7` | `TILE_ACTIVATE_REQUEST` |
| `22` | `PING_REQUEST` |
| `23` | `PING_REPLY` |

> 📖 Daftar packet/action yang lengkap sebaiknya divalidasi kembali menggunakan dokumentasi protokol dan source GTPS yang relevan.

---

## 🔐 Handshake Login — Fase 1

Setelah koneksi ENet dibuat, bot mengirim `GENERIC_TEXT` yang berisi informasi seperti:

```text
protocol|177
ltoken|<token dari HTTP login>
platform|windows
version|5.30
```

⚠️ Format handshake **belum tentu cocok untuk semua GTPS**. Jika server tidak merespons `SERVER_HELLO`, periksa implementasi login/handshake pada GTPS yang digunakan dan bandingkan trafik dengan client asli.

---

## ⚠️ Known Issues

Saat ini beberapa bagian Fase 1 masih perlu verifikasi:

- Login endpoint masih menggunakan asumsi `/api/login`.
- Format request login (`growId`, `password`, dan field terkait) bisa berbeda.
- Format handshake setelah ENet connect belum diverifikasi terhadap semua GTPS.
- Broadcast `S->C` ke semua client untuk skenario multi-bot belum tersedia.
- Auto-reconnect belum tersedia.
- Graceful shutdown dari `Ctrl+C` ketika berada di `getline` belum tersedia.

---

## 🗺️ Roadmap

### Fase 2 — Packet Parser

- [ ] Parser lengkap `NET_MESSAGE_GAME_PACKET`
- [ ] State management: world, inventory, player, NPC, dropped items
- [ ] Event system seperti `on_chat`, `on_world_enter`, `on_player_join`
- [ ] Decode text packet menjadi structured data
- [ ] Command tambahan: `enter`, `move`, `place`, `hit`, `wrench`

### Fase 3 — Actions

- [ ] `bot:moveTo(x, y)`
- [ ] `bot:place(x, y, item_id)`
- [ ] `bot:hit(x, y)`
- [ ] `bot:enter()`
- [ ] Pathfinding sederhana

### Fase 4 — Lua

- [ ] Integrasi Lua melalui Sol2 atau Lua C API
- [ ] API bergaya Lucifer Lua v2.86
- [ ] Hot-reload script
- [ ] Sandbox untuk membatasi filesystem dan network

### Fase 5 — Advanced

- [ ] Multi-bot dengan thread pool
- [ ] Proxy SOCKS5
- [ ] Auto-reconnect dengan exponential backoff
- [ ] Auto-warp
- [ ] Auto-farm
- [ ] Auto-collect
- [ ] Mekanisme anti-detection seperti delay/randomization dan device fingerprint

### Fase 6 — GUI

- [ ] ImGui native atau web UI
- [ ] Bot manager — start/stop per bot
- [ ] Log viewer
- [ ] Script editor

---

## 📚 Referensi

### Dokumentasi Protokol

- [H-pun/growtopia-docs](https://github.com/H-pun/growtopia-docs) — dokumentasi HTTP, ENet, dan struktur paket
- [GrowDocs — eikarna](https://github.com/eikarna/growdocs) — dokumentasi komunitas

### Project Referensi

- [ZTzTopia/GTProxy](https://github.com/ZTzTopia/GTProxy) — proxy Growtopia C++
- [GuckTubeYT/GTProxy](https://github.com/GuckTubeYT/GTProxy) — alternatif proxy Growtopia
- [CLOEI/Mori](https://github.com/CLOEI/Mori) — bot Growtopia Rust dengan multi-bot dan web GUI
- [playingoDEERUX/growbrewproxy](https://github.com/playingoDEERUX/growbrewproxy) — proxy C# dengan GUI
- [Gurotopia](https://github.com/Gurotopia) — server GTPS C++

### API Target Lua

File:

```text
LUCIFER_LUA_V2_86.md
```

berisi dokumentasi API Lucifer Lua v2.86 yang dijadikan target kompatibilitas untuk Fase 4.

Beberapa bagian yang menjadi referensi:

- Object `Lucifer` dengan method seperti `:say()`, `:warp()`, `:moveTo()`, `:place()`, `:hit()`, `:getWorld()`, dan `:getInventory()`
- Global API seperti `getBot()`, `getBots()`, `addBot()`, `getInfo()`, `addEvent()`, dan `listenEvents()`
- Enum seperti `BotStatus`, `Platform`, `Event`, `Action`, dan `Bubble`
- Automation module seperti `bot.auto_farm`, `bot.auto_fish`, dan `bot.auto_geiger`

---

## 🐛 Debugging

### 1. Aktifkan Hexdump

Di `config.json`:

```json
{
  "logging": {
    "verbose": true,
    "hexdump": true
  }
}
```

Contoh output:

```text
[12:00:05.250] [ENet] S->C type=3 len=20
  0000  05 00 48 65 6C 6C 6F 00 00 00 00 00 00 00 00 00  |..Hello.........|
```

### 2. Bandingkan dengan Client Asli

Gunakan proxy dari project terpisah (`gtps_proxy.py`) untuk mengintercept trafik:

```text
Growtopia Client <-> Proxy <-> GTPS
```

Bandingkan paket dari client asli dengan paket yang dikirim bot.

### 3. Baca Source GTPS

Jika GTPS yang digunakan bersifat open source, periksa handler login dan handshake untuk mengetahui field serta format yang diharapkan server.

### 4. Periksa Server Log

Jika memiliki akses log GTPS, periksa error saat bot melakukan login dan connect.

---

## 🛠️ Troubleshooting

| Gejala | Kemungkinan | Solusi |
|---|---|---|
| `Config tidak ditemukan` | Path salah | Jalankan dari lokasi yang memiliki `config.json` |
| `Login GAGAL: No response` | HTTP server mati / URL salah | Cek server dan lakukan request manual |
| `Login HTTP 404` | Endpoint salah | Periksa dokumentasi GTPS |
| `Gagal connect ENet` | Port salah / server mati | Pastikan port ENet tersedia; contoh `17091` |
| Connect OK tetapi server diam | Handshake salah | Cek source GTPS dan bandingkan client asli |
| `ENet not found` saat build | FetchContent gagal | Periksa internet, lalu clean build |
| Error OpenSSL | OpenSSL belum tersedia | Install OpenSSL sesuai OS |
| Bot disconnect sendiri | Server menolak paket / versi tidak sesuai | Aktifkan hexdump dan periksa handshake |

Contoh clean build:

```bash
rm -rf build
cmake .. -DCMAKE_BUILD_TYPE=Release
```

---

## 🤖 Panduan untuk Developer / AI yang Melanjutkan Project

### Urutan pengerjaan yang disarankan

**1. Pahami project**

```text
README.md
LUCIFER_LUA_V2_86.md
src/*
```

Baca README dan dokumentasi API terlebih dahulu, lalu pahami implementasi pada `src/`.

**2. Verifikasi Fase 1**

Lakukan build dan uji terhadap GTPS lokal. Validasi:

```text
HTTP login
   ↓
ENet connect
   ↓
SERVER_HELLO
   ↓
say / warp
```

Jangan melompat ke fase berikutnya sebelum masalah inti pada Fase 1 dipahami.

**3. Tentukan next task**

| Kondisi | Fokus berikutnya |
|---|---|
| Handshake belum bekerja | Login + handshake |
| Handshake bekerja, state belum terbaca | Fase 2 — packet parser |
| Parsing sudah bekerja | Fase 3 — actions |

### Konvensi Kode

- Gunakan **C++17**.
- Hindari exceptions untuk logika utama; gunakan return value.
- Gunakan header-only untuk utility seperti `logger.hpp` dan `packet.hpp`.
- Prinsip **1 class = 1 file**, kecuali utility kecil.
- I/O jaringan dibuat non-blocking / async-friendly.
- Semua log menggunakan `Logger::log()`.
- Prioritaskan pengujian di **MSYS2**, lalu verifikasi pada Visual Studio/Linux.

### Setiap Ada Perubahan

Update README bersamaan dengan fitur:

- [ ] Tabel **Progress**
- [ ] **Known Issues & TODO**
- [ ] Contoh command / usage
- [ ] Dokumentasi teknis bila format protokol berubah

---

## ⚖️ Catatan Penggunaan

Project ini ditujukan sebagai fondasi eksperimen, pengembangan, dan pembelajaran client GTPS. Pastikan penggunaan bot sesuai aturan server GTPS dan lingkungan tempat bot dijalankan.

---

## 📄 License

> **Belum ditentukan pada dokumentasi sumber.**

Tambahkan lisensi project di bagian ini ketika keputusan lisensi sudah dibuat.
