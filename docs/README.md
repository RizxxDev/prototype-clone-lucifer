# 📖 Lucifer GTPS Bot — Master Knowledge Base & Documentation Index

Selamat datang di repositori dokumentasi dan basis pengetahuan teknis **Lucifer GTPS Bot**. Direktori `docs/` ini merangkum seluruh pengetahuan arsitektur, spesifikasi protokol jaringan Growtopia/GTPS, sistem internal, API scripting Lua, modul anti-deteksi, manajemen multi-bot, hingga petunjuk untuk melanjutkan pengembangan.

---

## 📑 Struktur Dokumentasi

Dokumentasi ini dibagi menjadi beberapa bagian tematik agar mudah dipelajari dan dilanjutkan oleh pengembang maupun agent AI berikutnya:

| Dokumen | Topik & Cakupan Utama |
|---|---|
| [**1. Arsitektur Sistem (`ARCHITECTURE.md`)**](file:///c:/Users/RIZIQ/Documents/testtt/docs/ARCHITECTURE.md) | Diagram arsitektur lengkap, alur data (Data Flow), thread model, integrasi DirectX 11 + Dear ImGui, abstraksi `BotInstance`, dan `BotManager` dengan `ThreadPool`. |
| [**2. Spesifikasi Protokol Jaringan (`PROTOCOL.md`)**](file:///c:/Users/RIZIQ/Documents/testtt/docs/PROTOCOL.md) | Anatomi paket ENet, format Tank Packet 56-byte, aturan validasi 61-byte, VariantList (serialization & deserialization), Sub-server redirect (`OnSendToServer`), dan heartbeat ping/pong. |
| [**3. Referensi Scripting Bothax & Lucifer Lua (`LUA_API.md`)**](file:///c:/Users/RIZIQ/Documents/testtt/docs/LUA_API.md) | Dokumentasi lengkap Lua 5.4 C API, Bothax structures (`NetAvatar`, `World`, `Tile`, `WorldObject`), event hooks (`OnVariant`, `OnSendPacket`, dll.), dan API Lucifer v2.86 (`bot:moveTo`, `bot:hit`, `sleep`, dll.). |
| [**4. Anti-Deteksi, Proxy SOCKS5, & Reconnect (`ANTI_DETECTION_AND_SECURITY.md`)**](file:///c:/Users/RIZIQ/Documents/testtt/docs/ANTI_DETECTION_AND_SECURITY.md) | Detail generator identitas hardware (`MAC`, `RID`, `WK`, `hash`), penyimpanan cache persisten per-akun di `cache/fingerprints/`, algoritma exponential backoff + jitter, dan implementasi SOCKS5 RFC 1928/1929. |
| [**5. Panduan Pengembangan & Roadmap (`ROADMAP_AND_TODO.md`)**](file:///c:/Users/RIZIQ/Documents/testtt/docs/ROADMAP_AND_TODO.md) | Status fitur yang sudah selesai (Fase 1-6), quirk & gotchas penting yang telah dipecahkan, sistem cache terpadu `cache/`, panduan build & debugging, serta roadmap fitur. |

---

## ⚡ Ringkasan Proyek (Executive Summary)

- **Bahasa & Standar**: C++17
- **Target OS**: Windows 10/11 (64-bit)
- **Komponen Jaringan**: 
  - `libenet` (UDP custom protocol dengan Range Coder & CRC32 checksum)
  - `cpp-httplib` & `WinHTTP` (HTTP/HTTPS login & web auth validator)
  - `Socks5Client` (Koneksi proxy RFC 1928 / RFC 1929 TCP Connect & UDP Associate)
- **Komponen Grafis (GUI)**:
  - Dear ImGui (v1.91.8)
  - Microsoft DirectX 11 Backend (`d3d11.lib`, `dxgi.lib`)
  - Win32 API Windowing (`dwmapi.lib`, Dark Mode titlebar)
- **Komponen Scripting**:
  - Lua 5.4.6 (Static library terintegrasi)
  - Dua dialek API yang kompatibel: **Lucifer Lua v2.86** dan **Bothax Lua Docs API**
- **Sistem Paralelisme**:
  - `ThreadPool` mandiri berbasis C++11/17 condition variables
  - Isolasi per bot melalui `BotInstance` (setiap bot memiliki socket ENet, state map, inventory, dan event loop sendiri)
  - Staggered connection scheduler untuk memitigasi IP rate-limiting

---

## 🛠️ Panduan Build Cepat

### Prasyarat
- Visual Studio 2019 / 2022 / 2026 dengan toolset C++ (MSVC)
- CMake 3.16 atau yang lebih baru (kompatibel penuh dengan CMake 3.30+ & CMake 4.x via policy `CMP0169` & `CMAKE_POLICY_VERSION_MINIMUM 3.10`)
- Windows SDK 10.0 atau 11.0

### Perintah Kompilasi
Kompilasi dilakukan melalui CMake dengan otomatis mengunduh dependency (`FetchContent`):

```powershell
# 1. Generate build project (jika belum ada)
cmake -B build -G "Visual Studio 17 2022" -A x64

# 2. Compile target Release
cmake --build build --config Release
```

Binary yang dihasilkan berada di:
```text
build/Release/gtps_bot.exe
```

---

## 🚀 Panduan Menjalankan

1. **Konfigurasi Akun Tunggal**:
   Salin file template `.env.example` menjadi `.env`, lalu lengkapi informasi server GTPS dan akun:
   ```env
   GTPS_LOGIN_URL=http://127.0.0.1/growtopia/server_data.php
   GTPS_USE_HTTPS=false
   GTPS_GAME_SERVER=127.0.0.1
   GTPS_GAME_PORT=17091
   ACCOUNT_NAME=MyBot
   ACCOUNT_PASSWORD=MyPassword123
   CLIENT_VERSION=5.58
   CLIENT_PROTOCOL=225
   TARGET_WORLD=START
   SOCKS5_PROXY=127.0.0.1:1080:user:pass
   ```

2. **Konfigurasi Multi-Bot (Opsional)**:
   Buat file `accounts.txt` (contoh tersedia di `accounts.txt.example`):
   ```text
   Bot1:Pass123:FARM:127.0.0.1:1080
   Bot2:Pass123:FARM:127.0.0.1:1080
   Bot3:Pass123:STORE
   ```

3. **Jalankan Aplikasi**:
   ```powershell
   ./build/Release/gtps_bot.exe
   ```
   Aplikasi akan otomatis menginisialisasi DirectX 11, membuka jendela Lucifer Desktop Suite, menghubungkan bot ke server, dan menampilkan antarmuka Dear ImGui. Jika dijalankan di lingkungan tanpa GUI/server headless, aplikasi otomatis beralih ke mode interaktif CLI REPL.
