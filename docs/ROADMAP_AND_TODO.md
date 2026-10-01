# 🗺️ Lucifer GTPS Bot — Development Roadmap & Technical Gotchas

Dokumen ini merinci status implementasi proyek saat ini, riwayat masalah teknis (*gotchas & quirks*) protokol GTPS beserta solusinya, petunjuk debugging, serta panduan implementasi untuk fitur-fitur masa depan (*future milestones*).

---

## 📊 Status Implementasi Saat Ini

| Fase | Fitur | Status | Modul Utama |
|---|---|---|---|
| **Fase 1** | Koneksi HTTP Login (`server_data.php`) & ENet Core Client | ✅ Selesai | [`src/http_client.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/http_client.cpp), [`src/enet_client.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/enet_client.cpp) |
| **Fase 2** | `GameUpdatePacket` (Tank Packet), `VariantList` RPC & Sub-server Redirection | ✅ Selesai | [`src/game_packet.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/game_packet.hpp), [`src/variant.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/variant.hpp), [`src/world_state.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/world_state.hpp) |
| **Fase 3** | Mesin Skrip Lua 5.4, Event Hooking, Kompatibilitas Bothax & Lucifer API | ✅ Selesai | [`src/lua_engine.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/lua_engine.hpp), [`src/lua_engine.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/lua_engine.cpp) |
| **Fase 4** | Antarmuka Grafis Dear ImGui v1.91.8 + Microsoft DirectX 11 + Win32 Dark Mode | ✅ Selesai | [`src/gui.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/gui.hpp), [`src/gui.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/gui.cpp) |
| **Fase 5.1** | Sistem Multi-Bot Berbasis `ThreadPool` & Isolasi `BotInstance` | ✅ Selesai | [`src/thread_pool.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/thread_pool.hpp), [`src/bot_instance.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/bot_instance.hpp), [`src/bot_manager.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/bot_manager.hpp) |
| **Fase 5.2** | Klien Proxy SOCKS5 (RFC 1928 / RFC 1929 No-Auth & User/Pass) | ✅ Selesai | [`src/socks5.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/socks5.hpp), [`src/socks5.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/socks5.cpp) |
| **Fase 5.3** | Kontroler Auto-Reconnect Finite State Machine dengan Exponential Backoff & Jitter | ✅ Selesai | [`src/reconnect_controller.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/reconnect_controller.hpp), [`src/reconnect_controller.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/reconnect_controller.cpp) |
| **Fase 5.4** | Generator Hardware Fingerprint Persisten (`.fingerprints/`) & Human Delay Jitter | ✅ Selesai | [`src/anti_detection.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/anti_detection.hpp), [`src/anti_detection.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/anti_detection.cpp) |

---

## ⚠️ Riwayat Permasalahan Teknis & Solusi (*Gotchas & Quirks*)

Di bawah ini adalah kumpulan masalah non-sepele dalam reverse-engineering dan pengembangan bot Growtopia/GTPS yang telah dianalisis dan diselesaikan di dalam codebase:

### 1. Aturan Validasi 61-Byte (*The 61-Byte Packet Rule*)
- **Masalah**: Pada server GTPS modern (GT v3.x - v5.x), jika bot mengirim paket tank state (`PACKET_STATE`, type 0) dengan panjang tepat 56 byte struct (atau total 60 byte termasuk 4-byte header), server langsung memutus koneksi (*instant disconnect / ban*).
- **Penyebab**: Server resmi memvalidasi `dataLength >= 61` (4 byte outer packet type + 56 byte `GameUpdatePacket` + minimal 1 byte trailing extra data / null terminator).
- **Solusi**: Di [`src/game_packet.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/game_packet.hpp) pada fungsi `TankPacketHelper::serialize()`, bila `extra_len == 0`, ukuran alokasi dipaksa menjadi minimal `1`, dan diisi nilai byte `0x00`:
  ```cpp
  uint32_t alloc_extra = (extra_len == 0) ? 1 : extra_len;
  std::vector<uint8_t> buf(4 + sizeof(GameUpdatePacket) + alloc_extra, 0);
  ```

### 2. Inkonsistensi Ekstraksi Token & UserID pada `VariantList`
- **Masalah**: Saat menerima fungsi RPC `OnSendToServer`, field `token` dan `userId` sering bernilai 0 jika hanya membaca `int_val`. Akibatnya bot gagal login ke sub-server.
- **Penyebab**: Implementasi server GTPS (khususnya private server berbasis C++ atau C#) kerap mengirim `token` atau `userId` sebagai unsigned integer (`uint_val`, type 5) bukannya signed integer (`int_val`, type 9).
- **Solusi**: Gunakan pembacaan fallback di [`src/world_state.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/world_state.hpp) dan [`src/bot_instance.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/bot_instance.cpp):
  ```cpp
  int token = (v.int_val != 0) ? v.int_val : static_cast<int>(v.uint_val);
  int userId = (v.int_val != 0) ? v.int_val : static_cast<int>(v.uint_val);
  ```

### 3. Parsing Format `ipDoor` pada `OnSendToServer`
- **Masalah**: String argumen kedua dari `OnSendToServer` sering berformat `IP|DOOR_ID` (misal: `192.168.1.50|3000` atau `127.0.0.1|door_id|extra`). Memasukkan seluruh string ke dalam `enet_address_set_host` menyebabkan parsing IP gagal. Selain itu, beberapa server GTPS mengirim IP `127.0.0.1` atau `0.0.0.0` meskipun server berjalan secara publik.
- **Solusi**: Tokenisasi string berdasarkan pemisah `|`:
  - Karakter sebelum `|` pertama adalah IP sub-server.
  - Karakter sesudah `|` pertama adalah Door ID untuk auto-enter saat map termuat.
  - Jika IP hasil ekstraksi adalah `127.0.0.1`, `0.0.0.0`, atau `localhost`, pertahankan host awal jika bot sedang terhubung ke domain/IP publik.

### 4. Respons Heartbeat Ping/Pong (Paket Type 21, 22, 23)
- **Masalah**: Bot sering mengalami timeout/disconnect 30 detik setelah masuk ke dalam world.
- **Penyebab**: Server mengirim `GameUpdatePacket` tipe 21, 22, atau 23 (`PACKET_PING_REQUEST` / `PACKET_PING_REPLY`) secara berkala.
- **Solusi**:
  1. Balas dengan tipe 21 (`PACKET_PING_REPLY`) dengan menyalin `packet->main_data`.
  2. Isi koordinat default `(64.0f, 64.0f)` dan kecepatan gerak `(1000.0f, 250.0f)`.
  3. Jika tipe yang diterima adalah 23, kirimkan juga paket tipe 22 untuk memastikan server mengenali bot masih aktif.

### 5. Deadlock pada Network Callback Event Loop ENet
- **Masalah**: Aplikasi mengalami freeze/hang ketika callback paket ENet mencoba memanggil fungsi bot yang juga mengakses mutex client atau memicu reconnect.
- **Penyebab**: `EnetClient::poll()` memegang `std::mutex mutex_` saat mengeksekusi event loop, dan memanggil callback pengguna (`onPacket`, `onDisconnect`) langsung di dalam scope lock.
- **Solusi**: Gandakan / salin callback functional di bawah lock, lepaskan lock, baru kemudian panggil callback pengguna:
  ```cpp
  std::function<void(const std::vector<uint8_t>&)> cb;
  {
      std::lock_guard<std::mutex> lock(mutex_);
      cb = on_packet_;
  }
  if (cb) cb(packet_data);
  ```

### 6. Konflik Tipe MSVC `<functional>` pada GUI
- **Masalah**: Kompilasi MSVC menghasilkan error `C2039: 'function': is not a member of 'std'` pada header `gui.hpp`.
- **Solusi**: Sertakan header `<functional>` secara eksplisit di awal file header sebelum mendeklarasikan alias callback GUI.

### 7. Mode Standalone Multi-Bot Tanpa Akun Default di `.env`
- **Masalah**: Ketika kredensial `ACCOUNT_NAME` di `.env` dikosongkan, bot sebelumnya langsung keluar (*exit code 1*) dan menolak membuka jendela antarmuka GUI.
- **Solusi**:
  1. Buat parameter `acc_name`, `acc_pass`, dan `target_world` bersifat opsional di `src/main.cpp`.
  2. Apabila `acc_name.empty()`, lewati tahapan HTTP Login tunggal dan koneksi awal ENet, lalu langsung buka jendela GUI Lucifer v2.85 (DirectX 11).
  3. Kelola penambahan bot secara dinamis melalui file `accounts.txt` atau tombol modal **"+ Add Bot"** di sidebar GUI.
  4. Bersihkan buffer input modal secara otomatis setelah penambahan bot berhasil.

---

## 🛠️ Panduan Debugging & Troubleshooting

### Mengaktifkan Logging Hexdump Paket
Bila server merespons dengan paket yang tidak dikenal atau struktur world gagal di-parse:
1. Buka file `.env` atau atur variabel lingkungan:
   ```env
   LOG_LEVEL=DEBUG
   LOG_HEXDUMP=true
   ```
2. Terminal akan mencetak seluruh representasi heksadesimal dari setiap paket yang masuk dan keluar secara mendetail.

### Menguji Proxy SOCKS5 Secara Terisolasi
Anda dapat menguji apakah proxy SOCKS5 bekerja sebelum menggunakannya pada bot:
1. Jalankan aplikasi GUI.
2. Buka tab **"🛡️ Anti-Detection & Reconnect"** -> sub-bagian **"SOCKS5 Proxy Tester"**.
3. Masukkan Host, Port, Username, dan Password, lalu klik tombol **"Test Proxy Connection"**.
4. Hasil handshake RFC 1928 / RFC 1929 akan ditampilkan seketika.

### Menghapus Cache Hardware ID
Jika akun terkena suspend atau Anda ingin membuat profil hardware yang baru:
1. Masuk ke direktori `.fingerprints/`.
2. Hapus file JSON yang sesuai dengan GrowID Anda, misal: `.fingerprints/BotAccount1.json`.
3. Saat bot dijalankan kembali, profil baru akan di-generate secara acak dan disimpan ulang secara permanen.

---

## 🚀 Roadmap Masa Depan (*Future Milestones*)

Berikut adalah panduan langkah demi langkah untuk melanjutkan implementasi fitur-fitur tingkat lanjut:

### Milestone 6.1: Algoritma A* Pathfinding (`FindPath`)
**Tujuan**: Mengizinkan bot bernavigasi secara otomatis dari koordinat `(startX, startY)` ke `(targetX, targetY)` dengan menghindari rintangan, duri (*spikes*), lava, dan memperhitungkan gravitasi.

**Langkah Implementasi**:
1. Buka file [`src/world_state.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/world_state.hpp).
2. Tambahkan representasi Node untuk grid $100 \times 60$:
   ```cpp
   struct PathNode {
       int x, y;
       float g_cost, h_cost;
       PathNode* parent = nullptr;
       float f_cost() const { return g_cost + h_cost; }
   };
   ```
3. Implementasikan fungsi `std::vector<std::pair<int, int>> FindPath(int startX, int startY, int endX, int endY)`:
   - Gunakan `std::priority_queue` untuk Open Set.
   - Evaluasi apakah sebuah blok padat menggunakan `isTileSolid(x, y)`:
     - Tile padat (misal ID 2: Dirt, ID 4: Granite, ID 8: Bedrock).
     - Tile platform (ID 18: Wooden Platform - hanya padat dari atas jika bot sedang turun).
     - Tile berbahaya (ID 28: Lava, ID 34: Spike - beri penalti biaya tinggi atau tandai sebagai rintangan).
4. Sambungkan ke Lua engine pada `bot:findPath(x, y)` dan jalankan interpolasi pergerakan menggunakan `bot:moveTo(x, y)` dengan delay human-like.

---

### Milestone 6.2: Parsing & Caching Database Item (`items.dat`)
**Tujuan**: Menyimpan informasi nama item, tipe blok, waktu panen, properti seed, dan collision type secara offline.

**Langkah Implementasi**:
1. Buat file baru `src/items_database.hpp` dan `src/items_database.cpp`.
2. Format biner `items.dat` Growtopia:
   - 2 byte: `version` (misal 11, 14, 19).
   - 4 byte: `item_count`.
   - Loop per item:
     - 4 byte: `item_id`.
     - 1 byte: `properties`.
     - 1 byte: `item_type`.
     - 1 byte: `material`.
     - 2 byte length + string: `item_name` (didekripsi dengan XOR menggunakan key `"PBG892FYM0POOK тяжело"` atau shift cipher sesuai versi game).
     - 2 byte length + string: `texture_path`.
     - 4 byte: `texture_hash`.
     - 1 byte: `visual_type`.
     - 4 byte: `cook_time` / `grow_time`.
3. Buat antarmuka pencarian cepat:
   ```cpp
   const ItemDefinition* getItem(uint32_t id) const;
   const ItemDefinition* findItemByName(const std::string& name) const;
   ```
4. Ekspor ke Lua: `GetItemInfo(itemId)` yang mengembalikan tabel berisi `{ name, type, growTime, rarity }`.

---

### Milestone 6.3: Lua Sandbox & Keamanan Skrip
**Tujuan**: Membatasi akses skrip Lua agar tidak dapat memodifikasi sistem operasi atau menjalankan binary berbahaya (*arbitrary code execution*).

**Langkah Implementasi**:
1. Di [`src/lua_engine.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/lua_engine.cpp), modifikasi `luaL_openlibs(L)`:
   - Buka hanya pustaka standar yang aman: `base`, `table`, `string`, `math`, `utf8`.
   - Blokir atau batasi pustaka `os` (hapus `os.execute`, `os.remove`, `os.rename`, `os.getenv`).
   - Batasi pustaka `io` (hanya izinkan pembacaan file di dalam direktori `scripts/`).
   - Hapus fungsi global `loadfile` dan `dofile` yang mengakses path absolut di luar workspace.

---

### Milestone 6.4: Dukungan Headless / Docker Server Linux
**Tujuan**: Mengizinkan Lucifer Bot berjalan di lingkungan Linux (Ubuntu/Debian) tanpa kartu grafis atau display server, cocok untuk deployment multi-bot 24/7 di VPS.

**Langkah Implementasi**:
1. Pada `CMakeLists.txt`, tambahkan kondisi:
   ```cmake
   option(ENABLE_GUI "Build with DirectX 11 ImGui desktop interface" ON)
   if(NOT WIN32 OR NOT ENABLE_GUI)
       # Nonaktifkan DirectX 11, gunakan Terminal CLI REPL
       add_definitions(-DHEADLESS_MODE)
   endif()
   ```
2. Modifikasi `main.cpp` agar jika argumen `--headless` atau `-h` diberikan, aplikasi tidak memanggil fungsi inisialisasi DirectX 11 dan langsung menjalankan event loop CLI / multi-bot manager.
3. Buat `Dockerfile`:
   ```dockerfile
   FROM ubuntu:22.04
   RUN apt-get update && apt-get install -y cmake g++ libenet-dev
   WORKDIR /app
   COPY . .
   RUN cmake -B build -DENABLE_GUI=OFF && cmake --build build
   CMD ["./build/gtps_bot", "--headless"]
   ```

---

### Milestone 6.5: Pustaka Skrip Otomasi Bawaan (*Auto-Farming Suite*)
**Tujuan**: Menyediakan pustaka skrip Lua yang siap pakai untuk tugas-tugas farming yang umum.

**Contoh Skrip Template yang Direncanakan**:
- `scripts/auto_dirt_farm.lua`: Menanam, memanen, dan memukul pohon dirt untuk bot pemula.
- `scripts/auto_clear_world.lua`: Membersihkan seluruh blok dan background di world baru dari atas ke bawah.
- `scripts/auto_drop_collect.lua`: Mengambil item di lantai, mendeteksi seed, dan memindahkannya ke storage world melalui sub-server warp.

---

*Dokumentasi ini terus diperbarui seiring dengan perkembangan basis kode Lucifer GTPS Bot.*
