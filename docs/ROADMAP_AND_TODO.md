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
| **Fase 5.4** | Generator Hardware Fingerprint Persisten (`cache/fingerprints/`) & Human Delay Jitter | ✅ Selesai | [`src/anti_detection.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/anti_detection.hpp), [`src/anti_detection.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/anti_detection.cpp) |
| **Fase 5.5** | Sistem Direktori Terpadu `cache/` (Fingerprints, `accounts.txt`, `items.dat` Parser & Auto-Sync) | ✅ Selesai | [`src/item_database.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/item_database.hpp), [`src/item_database.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/item_database.cpp) |
| **Fase 5.6** | Pelacakan Live Statistik Akun (Level, Gems, Played, Age) & Sinkronisasi Persisten | ✅ Selesai | [`src/bot_stats.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/bot_stats.hpp), [`src/bot_stats.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/bot_stats.cpp), [`src/gui.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/gui.cpp) |
| **Fase 6.1** | Algoritma A* Pathfinding (`FindPath`/`CheckPath`) & Grid World Collision | ✅ Selesai | [`src/world.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/world.hpp), [`src/world.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/world.cpp), [`src/lua_engine.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/lua_engine.cpp) |
| **Fase 6.2** | Emulasi Offsets Standalone (15 Offsets) & Binary Collision `items.dat` | ✅ Selesai | [`src/item_database.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/item_database.cpp), [`src/bot_actions.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/bot_actions.cpp), [`src/lua_engine.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/lua_engine.cpp) |
| **Fase 6.3** | Full Bothax 43 Functions, 16 Structures, CamelCase Aliases & Multi-Hook Engine | ✅ Selesai | [`src/lua_engine.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/lua_engine.hpp), [`src/lua_engine.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/lua_engine.cpp), [`src/world.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/world.hpp) |

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
### 8. Web Login Token (`ltoken`) & Visibilitas Paket Masuk pada `BotInstance`
- **Masalah**: Bot berhasil terhubung ke server ENet dan menerima paket balasan (`type=3` dan `type=4`), tetapi bot terdiam tanpa masuk ke world, dan tidak ada informasi error sama sekali yang muncul di terminal.
- **Penyebab**:
  1. Token hasil validasi web (`/player/growid/login/validate`) tidak disimpan dan tidak disuntikkan sebagai `ltoken|<token>` ke dalam paket handshake ENet.
  2. Implementasi `BotInstance` sebelumnya tidak mencetak isi pesan teks game (`NET_MESSAGE_GAME_MESSAGE`), error paket (`NET_MESSAGE_ERROR`), dan fungsi variant (`OnDialogRequest`, `OnConsoleMessage`).
  3. Ketika `target_world` kosong, bot sudah berhasil masuk ke Lobby/World Select Menu, namun karena tidak ada instruksi visual, tampak seolah bot belum login.
- **Solusi**:
  1. Simpan `login_resp.token` ke `cached_token_` dan sertakan baris `ltoken|<token>\n` di `buildHandshake()`.
  2. Tambahkan logging lengkap untuk `Server Text`, `Variant Function`, `OnConsoleMessage`, `OnDialogRequest`, dan `NET_MESSAGE_ERROR`.
  3. Tambahkan status `"Lobby"` saat `OnRequestWorldSelectMenu` diterima dan beri tahu pengguna bahwa bot sudah siap untuk di-warp.

### 9. Persistensi Akun & Bug Penimpaan File Kosong (`cache/accounts.txt`)
- **Masalah**: Akun yang pernah ditambahkan ke bot hilang atau terhapus dari `cache/accounts.txt` ketika program ditutup dan dibuka kembali.
- **Penyebab**:
  1. *Empty-Pool Overwrite*: Fungsi `saveAccountsToFile()` dipanggil pada saat shutdown (`LuciferGui::shutdown()` dan akhir `main()`). Jika memori bot bernilai 0 (misalnya karena parser akun sebelumnya terlalu ketat menolak baris tanpa password), `saveAccountsToFile()` membuka file dengan mode truncate `std::ofstream`, menimpa seluruh isi file `cache/accounts.txt` dengan header kosong saja.
  2. *Strict Parser*: Parser lama mensyaratkan minimal 2 token (`GrowID:Password`), sehingga input nama saja tanpa password diabaikan.
- **Solusi**:
  1. Tambahkan proteksi *empty-pool guard* di `saveAccountsToFile()`: jika `bots_.empty()`, sistem memeriksa apakah file tujuan sudah memiliki entri akun. Jika ada, penimpaan dibatalkan.
  2. Buat parser menerima format fleksibel (`GrowID`, `GrowID:Pass`, `GrowID:Pass:World:...`) dan perbarui password/target world jika ditemukan entri baru.
  3. Tambahkan fitur *Auto-Recovery*: jika daftar bot di memori kosong saat startup, program otomatis memindai `cache/fingerprints/*.json` untuk memulihkan akun (`<GrowID>`) yang pernah dibuat sebelumnya.
  4. Sediakan editor kredensial interaktif di GUI (`SelectedBotDetail`) agar pengguna dapat memperbarui dan menyimpan password serta target world langsung ke `cache/accounts.txt`.

### 10. Sanitasi Output Terminal & Reduksi Kebisingan Log (*Clean Console UX*)
- **Masalah**: Terminal dibanjiri teks berulang dan format yang rusak:
  1. Paket ENet masuk `[ENet] S->C type=4 len=...` dicetak puluhan kali per detik.
  2. Variant function umum seperti `⚡ Variant Function: OnNameChanged` dan `OnSetPos` dicetak setiap detik.
  3. Server generic text seperti efek suara internal (`action|play_sfx`), ping/pong, dan markup form dialog raksasa (`add_label_with_icon...`) di-dump mentah-mentah ke stdout hingga memotong teks baris lain.
  4. Log status henti bot `🛑 Bot dihentikan.` dan penyimpanan file dicetak berulang kali (3-4 kali) saat shutdown.
- **Penyebab**:
  - Logger di level network (`enet_client.cpp`) dan packet dispatcher mencetak semua event tanpa filter level kepentingan.
  - Kode warna bawaan Growtopia (`` `2 `w `b `5 ``) tidak dibersihkan sehingga merusak baris terminal.
  - Siklus shutdown memanggil `stop()` di `gui.shutdown()`, `main()`, `~BotManager()`, dan `~BotInstance()`.
- **Solusi**:
  1. Tambahkan `Logger::logDebug` dan setel default `Logger::verbose = false`. Pindahkan log paket raw ENet (`S->C`), ping/pong, dan variant minor ke `logDebug`.
  2. Implementasikan fungsi `Logger::stripColorCodes(std::string)` untuk membersihkan kode format warna Growtopia agar teks yang dicetak di terminal bersih dan rapi.
  3. Saring paket teks server: abaikan audio SFX (`action|play_sfx`) dan definisi form dialog raksasa (`add_label`).
  4. Tambahkan proteksi transisi state pada `BotInstance::stop()` dan `BotManager::stopAll()`: bot yang sudah offline/nonaktif tidak akan mencetak log berulang kali saat destructors berjalan.
  5. Buat penyimpanan akun di background/shutdown berjalan secara tenang (*quiet save*), dan hanya menampilkan log konfirmasi saat pengguna menekan tombol *Save* atau *Export* secara manual.

### 11. Deteksi Alamat IP Riil yang Terbaca oleh Server Game (Bukan Sekadar Label "Direct")
- **Masalah**: Pada tab identitas akun / RID, alamat IP bot sebelumnya hanya menampilkan string statis `"Direct"` atau host proxy secara mentah, tanpa menampilkan IP publik asli yang terdeteksi di internet dan terbaca oleh game server.
- **Penyebab**: Klien game tidak memiliki listener untuk menangkap IP publik dari pesan server maupun resolver IP publik otomatis untuk koneksi langsung.
- **Solusi**:
  1. Implementasikan `HttpClient::fetchPublicIp()` dan `HttpClient::fetchDirectIp()` melalui endpoint terpercaya (`api.ipify.org`, `icanhazip.com`, `ifconfig.me`).
  2. Lakukan inspeksi paket `OnConsoleMessage` dan pesan selamat datang server untuk mengekstrak alamat IP klien yang dicatat server (`extractIpFromText()`).
  3. Tampilkan alamat IP dengan status prioritas berjenjang di GUI: (1) Terverifikasi dari respon server game, (2) Terdeteksi via SOCKS5 proxy gateway, (3) Terdeteksi dari IP publik ISP lokal.

### 12. Pelacakan Live & Akumulasi Statistik Akun (Level, Gems, Played, Age)
- **Masalah**: Nilai Level, Gems, Played Time, dan Account Age di kartu Bot Information sebelumnya bersifat statis atau hardcoded (`Level: 1`, `Played 0.00 hours. Age 0 days.`).
- **Penyebab**: Protokol Growtopia menyebarkan statistik akun di berbagai jenis paket yang berbeda tergantung implementasi server GTPS (ada yang melalui `OnSetBumperCoinValues`, `OnSetGems`, `OnSetLevel`, inventory item 112, dialog status `/status`, pesan `/me`, atau `OnConsoleMessage`). Selain itu, server sering tidak menyimpan waktu bermain (played hours) dan umur akun (age) di paket cepat.
- **Solusi**:
  1. Buat modul arsitektur `BotStats` ([`src/bot_stats.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/bot_stats.hpp), [`src/bot_stats.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/bot_stats.cpp)) yang thread-safe.
  2. **Multi-Source Parser**: Tangkap nilai Gems & Level dari:
     - `OnSetBumperCoinValues` (Standar protokol: `var[1]` = gems, `var[2]` = level/bumper).
     - `OnSetGems` dan `OnSetLevel`.
     - Paket inventory (`PACKET_SEND_INVENTORY_STATE` untuk item 112).
     - Parser teks regex & substring case-insensitive dari dialog `/status`, pesan konsol, dan talk bubble `/me`.
  3. **Continuous Session Time Accumulation**: Hitung durasi bermain aktif bot sejak `OnSpawn` / `PACKET_SEND_MAP_DATA`, akumulasikan ke `total_played_seconds`, dan simpan secara persisten ke `cache/stats/<account_name>.json`.
  4. **Dynamic Account Age**: Hitung umur akun dalam hari berdasarkan selisih waktu sekarang dengan stempel waktu pembuatan fingerprint / profil akun (`first_seen_epoch`), dengan fallback nilai eksplisit dari respon server.
  5. Sediakan tombol sinkronisasi manual interaktif `[ 🔄##query_stats ]` di GUI yang mengirim perintah `/status` dan `/me` ke server untuk memperbarui seluruh data secara instan.

### 13. Emulasi Standalone Offsets Eksekusi & Native Collision Checking
- **Masalah**: Skrip Bothax/Lucifer di komunitas sering kali mengandalkan memory offsets biner Growtopia (`Growtopia.exe` client injection) untuk fungsi-fungsi penting seperti `RequestTileChange`, `HandleTouchAtWorldCoordinates`, `IsCollidable`, `IsDoor`, dan cheat toggles (`ModFly`, `NoName`, `CanSeeGhost`). Jika bot berjalan dalam mode *standalone headless* (tanpa `Growtopia.exe`), pemanggilan fungsi ini sebelumnya berupa no-op stub kosong atau gagal dieksekusi.
- **Penyebab**: Offsets memori biner mengasumsikan adanya process memory space `Growtopia.exe`, pointer GameLogic, dan BaseApp asli.
- **Solusi**:
  1. **Standalone Emulation Architecture**: Alih-alih melakukan memory read/write crash-prone ke proses eksternal, bot mengemulasikan perilaku semantik dari setiap offset secara native:
     - `O_REQUESTTILECHANGE` (`0xA1ED90`) & `O_HANDLETOUCHATWORLDCOORDINATES` (`0x90A090`): Dipetakan ke `BotActions::requestTileChange()` dan `handleTouch()`, yang membangun dan mendispatch Tank Packet state (`PACKET_STATE`, type 0) berformat 56-byte lengkap dengan tile position `(x, y)` dan item ID.
     - `O_ISCOLLIDABLE` (`0x57FEE0`) & `O_ISDOOR` (`0x582650`): Mengkueri database item lokal `ItemDatabase` yang mem-parse byte `collision_type` biner dari `items.dat` (0 = none, 1 = solid, 2 = platform, 3 = door).
     - `O_MODFLY` (`0x9BE5A0`): Menyetel state `mod_fly_` pada bot. Setiap kali bot berpindah via `moveTo()`, flag `TankFlags::FLYING` (0x02) secara otomatis disuntikkan ke dalam paket pergerakan, dan pergerakan diabaikan dari penalti gravitasi atau rintangan.
     - `O_NONAME` (`0x8E8F50`): Mengatur flag `no_name_` pada bot actions.
     - `O_GETGAMELOGIC` (`0x51E220`) & `O_GETBASEAPP` (`0x515810`): Mengembalikan base address emulasi yang valid (`0x10000000` dan `0x20000000`), sehingga skrip yang memvalidasi pointer tidak throw error nil.
  2. **Grid A* Pathfinding 2D**: Mengimplementasikan algoritma A* pada grid $100 \times 60$ world dengan priority queue dan heuristik Manhattan. Fungsi `isTileSolid(x, y)` mengombinasikan data tile foreground/background world dengan collision database dari `items.dat`.
  3. **Tabel Offsets di Lua**: Seluruh 15 nilai hex offset dari `offsets.txt` diekspor baik ke tabel `Offsets` maupun variabel global `O_*`, menjamin 100% kompatibilitas skrip.

### 14. Pencegahan Crash / Deadlock Mutex Rekursif & Non-Blocking Render Loop pada Mesin Lua
- **Masalah**: Skrip Lua yang mengeksekusi loop dengan packet sending seperti `while true do SendPacket(2, "action|input\ntext|".. textSpam) Sleep(delaySpam) end` menyebabkan bot crash seketika (Exit code 1) atau jendela GUI DirectX 11 menjadi beku (*Not Responding*).
- **Penyebab**:
  1. **Self-Deadlock / std::system_error pada Mutex**: `LuaEngine::workerThread` mengakuisisi `lua_mtx_` selama menjalankan `luaL_dostring()`. Saat skrip mengeksekusi `SendPacket()`, C++ callback `bothaxSendPacket` memanggil hook dispatcher `triggerOnSendPacket()` pada thread yang sama. Pada implementasi awal, `lua_mtx_` adalah `std::mutex` non-rekursif. Memanggil `lock()` kembali pada thread yang sudah memegang `std::mutex` dideteksi oleh MSVC STL runtime dan melempar `std::system_error` (*resource deadlock avoided* / *device or resource busy*). Karena tidak ditangkap di thread worker, aplikasi langsung memanggil `std::terminate()` dan crash.
  2. **Render Loop Blocking**: `triggerOnDraw(ImGui::GetIO().DeltaTime)` dipanggil 60 kali per detik dari thread rendering DirectX 11 di GUI. Jika `lua_mtx_` dipegang oleh thread worker selama eksekusi skrip, thread GUI terblokir menunggu mutex, membekukan jendela aplikasi.
  3. **Stack Corruption pada `lua_next`**: Ketika `lua_pcall()` di dalam dispatcher hook mengembalikan kegagalan (misalnya fungsi hook mengandung error), string error yang ditaruh Lua di puncak stack tidak di-pop. Akibatnya, iterasi `lua_next()` berikutnya membaca string error tersebut sebagai `key` tabel, memicu fatal panic `"invalid key to 'next'"`.
- **Solusi**:
  1. **Recursive Mutex**: Mengubah `lua_mtx_` menjadi `std::recursive_mutex` pada [`src/lua_engine.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/lua_engine.hpp) dan [`src/lua_engine.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/lua_engine.cpp). Hal ini memungkinkan callback C++ yang dipicu dari dalam eksekusi Lua (seperti `SendPacket`, `SendPacketRaw`, `RequestTileChange`) untuk memanggil trigger dispatcher secara reentrant tanpa deadlock atau exception.
  2. **Zero-Overhead Hook Counter (`hook_count_`)**: Menambahkan penghitung atomic `hook_count_`. Jika skrip tidak mendaftarkan hook (kasus mayoritas skrip seperti spammer dan farming), seluruh trigger method (`triggerOnVariant`, `triggerOnSendPacket`, `triggerOnDraw`, dll.) langsung mengembalikan nilai tanpa menyentuh mutex sama sekali.
  3. **Non-Blocking GUI Render Hook**: Mengubah penguncian pada `triggerOnDraw` dan `triggerOnInput` menggunakan `std::unique_lock<std::recursive_mutex> lock(lua_mtx_, std::try_to_lock)`. Jika mutex sedang sibuk, frame render DirectX 11 tetap berjalan mulus pada 60 FPS tanpa jeda mikrodetik pun.
  4. **Reentrancy Guards & Safe Packet Routing**: Menambahkan pelindung `thread_local bool in_send_packet_hook` untuk mencegah rekursi tak hingga jika hook `OnSendPacket` memanggil `SendPacket()`. Menambahkan auto-routing cerdas: jika `self->enet_` belum terkoneksi tetapi bot di `BotManager` sedang online di world, pengiriman paket dialihkan otomatis ke ENet peer bot aktif.
  5. **Clean Lua Stack Restoration**: Memastikan seluruh hasil eksekusi `lua_pcall()` (baik sukses maupun error) di-pop secara tuntas dari stack sebelum memanggil iterasi `lua_next()` berikutnya.
  6. **Alias Global Bothax**: Mendaftarkan alias `Log` (huruf kapital) mendampingi `LogToConsole`, `log`, dan `print`.

### 15. Manajemen Siklus Hidup Hook & Deteksi Liveness pada Lua Worker Thread
- **Masalah**: Thread worker script (`LuaEngine::workerThread`) menggantung (hang/stuck) dan tidak pernah selesai meskipun seluruh baris kode script telah selesai dieksekusi atau hook telah dihapus dengan `RemoveHook(label)` / `RemoveHooks()`.
- **Penyebab**: Loop liveness worker thread memeriksa apakah ada hook terdaftar dengan melakukan iterasi `lua_next()` pada sub-tabel registry (`OnVariant`, `OnSendPacket`, dll.). Ketika skrip memanggil `RemoveHook()`, field fungsi di dalam sub-tabel disetel ke `nil`. Namun, sub-tabel kategori itu sendiri masih ada di dalam tabel registry induk. Akibatnya, `lua_next()` pada registry tabel tetap mengembalikan sub-tabel kosong tersebut sebagai elemen yang ada, sehingga loop liveness mengira masih ada hook yang aktif dan tidak pernah keluar.
- **Solusi**:
  1. Gunakan penghitung atomic `std::atomic<int> hook_count_` pada `LuaEngine`.
  2. Setiap kali `bothaxAddHook` menambahkan entri baru, inkrementasikan `hook_count_`.
  3. Saat `bothaxRemoveHook` (baik varian 1 argumen `(label)` maupun 2 argumen `(event, label)`) menghapus entri, dekrementasikan `hook_count_`. Pada `bothaxRemoveHooks`, setel ulang `hook_count_ = 0`.
  4. Loop worker thread cukup memeriksa `hook_count_ > 0`. Bila tidak ada hook yang aktif, worker thread langsung keluar secara bersih (*graceful exit*), mencegah thread zombie atau kebuntuan saat evaluasi skrip selesai.

### 16. Inisialisasi Dinamis dan Batas Array Grid Tile pada `WorldState`
- **Masalah**: Skrip Lua yang memanggil `GetTile(x, y)` atau `SetTileFlags(x, y, flags)` sebelum peta world termuat penuh dari server atau saat bot berjalan dalam mode evaluasi skrip standalone memicu *out-of-range memory access* atau crash.
- **Penyebab**: Objek `WorldState` diinisialisasi dengan lebar `width_ = 100` dan tinggi `height_ = 60` (`total = 6000`), tetapi vektor `tiles_` dibiarkan kosong hingga paket peta biner (`PACKET_SEND_MAP_DATA`) diterima dari server. Jika skrip mengakses tile sebelum map termuat, `tiles_.size()` adalah `0`, sehingga kalkulasi indeks `y * width_ + x` melebihi kapasitas memori vektor.
- **Solusi**:
  1. Pada konstruktor `WorldState` dan fungsi `clearWorld()`, inisialisasikan `tiles_.resize(width_ * height_)`.
  2. Pada accessor `getTile(x, y, ...)` dan `setTileFlags(x, y, ...)`, tambahkan pengecekan batas dan auto-resize defensif: jika ukuran `tiles_` belum mencapai `width_ * height_`, lakukan resize secara otomatis sebelum mengakses elemen indeks.

### 17. Serialisasi & Deserialisasi Pemetaan Struktur `Variant` dan `GameUpdatePacket`
- **Masalah**:
  1. `Variant` pada protokol Growtopia/GTPS merepresentasikan data vektor 2D/3D bukan sebagai pointer ke struktur terpisah melainkan komponen skalar float individual: `float_val` (X), `float_val2` (Y), dan `float_val3` (Z), serta integer unsigned disimpan pada `uint_val`. Jika parser Lua mencoba mengakses `v.vec2_val` atau `v.vec3_val`, kompilator C++ akan melempar error kompilasi karena field tersebut tidak ada pada struct C++.
  2. `GameUpdatePacket` Bothax membutuhkan 21 nama field termasuk alias (`x`, `y`, `pos_x`, `pos_y`, `value`, `int_data`, `px`, `py`, `tx`, `ty`, `int_x`, `int_y`, `snetid`, `secondnetid`, `state`). Pada struct C++ GTPS, nilai `state` Bothax tersimpan pada `float_var`.
- **Solusi**:
  1. Buat helper `pushTankUpdatePacket()` di `lua_engine.cpp` yang mengisi seluruh 21 atribut dan alias secara lengkap.
  2. Pada pemrosesan `VariantList`, petakan tipe 3 (`Vec2`) ke `{ x = v.float_val, y = v.float_val2 }`, tipe 4 (`Vec3`) ke `{ x = v.float_val, y = v.float_val2, z = v.float_val3 }`, dan tipe 5 ke `v.uint_val`.
  3. Pada `bothaxSendVariantList`, dukung parsing tabel Lua dengan field `x`, `y`, `z` menjadi komponen vektor float `Variant` GTPS secara akurat.

### 18. Integritas Presisi 64-Bit pada `Hash64` dan Forwarding Multi-Argumen `RunDelayed`/`RunThread`
- **Masalah**:
  1. `Hash64(str)` sebelumnya menggunakan `lua_pushnumber()` yang mengonversi integer 64-bit menjadi IEEE 754 double precision float (53-bit mantissa), menyebabkan bit terendah mengalami pembulatan (*precision truncation*).
  2. `RunDelayed(delay, fn, ...)` dan `RunThread(fn, ...)` pada spesifikasi Bothax dapat menerima argumen variadik tambahan untuk diteruskan ke fungsi target. Menggunakan binding C++ sederhana sering kali hanya meneruskan parameter fungsi tanpa argumen tambahan.
- **Solusi**:
  1. Gunakan `lua_pushinteger()` pada `bothaxHash64()` yang pada Lua 5.4 64-bit secara bawaan merupakan tipe `int64_t` bertanda/tak-bertanda penuh, menjamin 100% presisi hash FNV-1a.
  2. Simpan seluruh argumen variadik ke dalam tabel sementara di registry Lua (`lua_createtable`, `lua_rawseti`), lalu di dalam invocation wrapper, gunakan loop `lua_rawgeti` untuk me-unpack seluruh argumen sebelum memanggil `lua_pcall()`.

### 19. Kebijakan Single-Instance Otomatis: Terminasi Instance Lama Saat Peluncuran Ulang
- **Masalah**: Pengguna yang menjalankan executable `gtps_bot.exe` lebih dari sekali (misal: saat restart atau pengujian skrip baru) sering kali mendapati instance lama masih berjalan di latar belakang (background process / zombie). Akibatnya, dua instance berjalan secara simultan, berebut port ENet, bersaing pada koneksi proxy SOCKS5, menghasilkan log yang bertubrukan di konsol, dan memicu error rendering DirectX 11.
- **Penyebab**: Windows secara bawaan mengizinkan eksekusi banyak proses dari binary yang sama secara independen jika tidak ada mekanisme eksklusi timbal balik (*mutual exclusion*) atau deteksi proses aktif sebelumnya.
- **Solusi**:
  1. Di [`src/main.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/main.cpp), implementasikan fungsi `terminatePreviousInstances()` yang berjalan saat startup:
     - Gunakan `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)` untuk memindai daftar proses aktif di Windows.
     - Dapatkan path file executable kita saat ini melalui `GetModuleFileNameA()`.
     - Identifikasi setiap proses dengan nama file dan image path yang sama dengan executable kita, tetapi memiliki PID berbeda (`th32ProcessID != GetCurrentProcessId()`).
     - Buka handle proses lama dengan `OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, ...)`.
     - Kirim `WM_CLOSE` ke seluruh jendela GUI instance lama via `EnumWindows()` agar proses lama dapat melakukan cleanup wajar.
     - Berikan waktu tenggang hingga 400 milidetik via `WaitForSingleObject()`.
     - Jika proses lama belum keluar (atau dalam mode CLI/stuck), panggil `TerminateProcess()` secara paksa untuk membebaskan seluruh port, socket, dan GPU buffer seketika.
  2. Tambahkan flag command-line `--allow-multiple` jika pengguna secara sadar ingin menjalankan multi-instance secara terpisah.

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

### Milestone 6.1: Algoritma A* Pathfinding (`FindPath` & `CheckPath`) — ✅ Selesai
**Status**: Diimplementasikan di [`src/world.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/world.hpp), [`src/world.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/world.cpp), dan diekspor ke [`src/lua_engine.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/lua_engine.cpp).
- Grid 2D $100 \times 60$ tile dengan `std::priority_queue` berbasis Manhattan distance.
- Memeriksa kepadatan tile secara akurat menggunakan `ItemDatabase` (`isCollidable`).
- Menyediakan `FindPath(x, y)` (berjalan melangkah ke target) dan `CheckPath(x, y)` (pengecekan konektivitas rute tanpa berpindah).
- Mendukung bypass gravitasi saat status `ModFly` aktif.

---

### Milestone 6.2: Parsing & Caching Database Item (`items.dat`) — ✅ Selesai
**Status**: Diimplementasikan di [`src/item_database.hpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/item_database.hpp) dan [`src/item_database.cpp`](file:///c:/Users/RIZIQ/Documents/testtt/src/item_database.cpp).
- Mendukung pemuatan otomatis file `items.dat` dari root direktori proyek atau subfolder `cache/`.
- Membaca nama item, byte `collision_type` (0 = none, 1 = solid, 2 = platform, 3 = door/portal).
- Menyediakan lookup cepat `isCollidable(item_id)` dan `isDoor(item_id)`.
- Terintegrasi penuh ke fungsi Lua `IsCollidable(id)`, `IsDoor(id)`, `GetItemByIDSafe(id)`, dan grid world.

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
