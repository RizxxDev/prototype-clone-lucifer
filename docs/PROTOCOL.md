# 📡 Spesifikasi Protokol Jaringan — Growtopia & GTPS

Dokumen ini mendokumentasikan spesifikasi teknis protokol jaringan Growtopia dan Growtopia Private Server (GTPS), struktur paket biner, layout memori, aturan validasi, dan mekanisme pertukaran data.

---

## 1. Lapisan Transport & ENet

Growtopia menggunakan library jaringan **ENet** yang berjalan di atas protokol transport **UDP**.

### Karakteristik Koneksi ENet
- **Port Standar**: Umumnya `17091` (atau rentang `17000–17200` pada sub-server).
- **Channels**: 2 channel komunikasi ENet (Channel 0 untuk paket andal/reliabel, Channel 1 untuk streaming data/unreliable).
- **Range Coder**: Kompresi bawaan ENet diaktifkan menggunakan `enet_host_compress_with_range_coder(host)`.
- **Checksum**: Checksum paket wajib disetel ke CRC32: `host->checksum = enet_crc32`.
- **Packet Delivery**: Paket game mayoritas dikirim dengan flag `ENET_PACKET_FLAG_RELIABLE`.

---

## 2. Format Paket Tingkat Teratas (Outer Packet)

Setiap paket yang dikirimkan atau diterima melalui ENet memiliki format pembungkus:

```text
┌──────────────────────┬──────────────────────────────────────────┐
│ uint32_t Type (4B)   │ Payload Data (N Bytes)                   │
└──────────────────────┴──────────────────────────────────────────┘
```

### Daftar Message Type:

| Type ID | Konstanta | Keterangan & Penggunaan |
|:---:|---|---|
| `0` | `NET_MESSAGE_UNKNOWN` | Paket tidak terdefinisi. |
| `1` | `NET_MESSAGE_SERVER_HELLO` | Dikirim pertama kali oleh server setelah koneksi ENet terbentuk. Menandakan server siap menerima handshake. |
| `2` | `NET_MESSAGE_GENERIC_TEXT` | String teks ASCII berakhiran `\0` (misal login handshake, perintah `action|...`, chat). |
| `3` | `NET_MESSAGE_GAME_MESSAGE` | Pesan game berbasis teks (misal join world request, menu click). |
| `4` | `NET_MESSAGE_GAME_PACKET` | **Tank Packet** (paket biner bervolume tinggi: pergerakan, aksi, peta world, inventaris, variant list). |
| `5` | `NET_MESSAGE_ERROR` | Pesan error dari server (misal akun sedang login di tempat lain). |
| `6` | `NET_MESSAGE_TRACK` | Paket pelacak metrik analitik. |
| `7` | `NET_MESSAGE_CLIENT_LOG_REQUEST` | Permintaan log dari server ke klien. |
| `8` | `NET_MESSAGE_CLIENT_LOG_RESPONSE`| Balasan log klien ke server. |

---

## 3. Struktur Tank Packet (`GameUpdatePacket`)

Jika `Type == 4` (`NET_MESSAGE_GAME_PACKET`), payload paket berukuran minimal 56 byte yang merepresentasikan `GameUpdatePacket`.

### Layout Memori Struct C++ (56 Byte):

```cpp
#pragma pack(push, 1)
struct GameUpdatePacket {
    uint8_t  type;              // Offset 0x00: Tipe aksi tank packet
    uint8_t  obj_type;          // Offset 0x01: Tipe objek / padding
    uint8_t  count1;            // Offset 0x02: Jumlah / punch strength
    uint8_t  count2;            // Offset 0x03: Item count kedua
    int32_t  net_id;            // Offset 0x04: NetID pemilik karakter/objek
    int32_t  target_net_id;     // Offset 0x08: NetID target
    uint32_t flags;             // Offset 0x0C: Bitflags karakter (on ground, facing left, dll)
    float    float_var;         // Offset 0x10: Variabel float (water speed, gravity modifier)
    int32_t  main_data;         // Offset 0x14: Item ID, tile extra data, atau ping value
    float    pos_x;             // Offset 0x18: Koordinat X pixel (Tile = pos_x / 32)
    float    pos_y;             // Offset 0x1C: Koordinat Y pixel (Tile = pos_y / 32)
    float    speed_x;           // Offset 0x20: Kecepatan X (kecepatan jalan)
    float    speed_y;           // Offset 0x24: Kecepatan Y (kecepatan lompat/jatuh)
    float    secondary_padding; // Offset 0x28: Padding float sekunder
    int32_t  tile_x;            // Offset 0x2C: Koordinat tile X interaksi
    int32_t  tile_y;            // Offset 0x30: Koordinat tile Y interaksi
    uint32_t data_size;         // Offset 0x34: Ukuran data tambahan yang mengekor struct (N Bytes)
};                              // Total ukuran struct: 56 Byte (0x38)
#pragma pack(pop)
```

### Daftar Nilai `TankPacket.type`:

| ID | Nama Konstanta | Arah | Penjelasan |
|:---:|---|:---:|---|
| `0` | `PACKET_STATE` | C↔S | Pembaruan status pergerakan dan posisi pemain. |
| `1` | `PACKET_CALL_FUNCTION` | C↔S | Memanggil RPC function menggunakan format `VariantList`. |
| `2` | `PACKET_UPDATE_STATUS` | S→C | Pembaruan status umum avatar pemain. |
| `3` | `PACKET_TILE_CHANGE_REQUEST` | C→S | Permintaan pukul blok (`hit`) atau pasang blok (`place`). |
| `4` | `PACKET_SEND_MAP_DATA` | S→C | Data biner peta seluruh world (ukuran dan seluruh tile). |
| `5` | `PACKET_SEND_TILE_UPDATE_DATA` | S→C | Pembaruan satu tile yang berubah di world. |
| `6` | `PACKET_SEND_TILE_UPDATE_MULTIPLE`| S→C | Pembaruan multi-tile sekaligus. |
| `7` | `PACKET_TILE_ACTIVATE_REQUEST`| C→S | Memasuki pintu / portal pada posisi tile bot. |
| `8` | `PACKET_TILE_APPLY_DAMAGE`    | C↔S | Efek kerusakan/pukulan pada blok (tile hit damage). |
| `9` | `PACKET_SEND_INVENTORY_STATE` | S→C | Data tas inventaris pemain yang dikirim saat masuk world. |
| `10`| `PACKET_ITEM_ACTIVATE_REQUEST`| C→S | Menggunakan item konsumsi atau alat dari inventaris. |
| `11`| `PACKET_ITEM_ACTIVATE_OBJECT_REQ` | C→S | Mengambil item yang mengambang di dekat bot (`collect`). Koordinat `pos_x` & `pos_y` WAJIB koordinat item, bukan pemain! |
| `12`| `PACKET_SEND_TILE_TREE_STATE` | S→C | Status pertumbuhan pohon (tree progress). |
| `13`| `PACKET_MODIFY_ITEM_INVENTORY`| S→C | Modifikasi slot inventaris pemain. |
| `14`| `PACKET_ITEM_CHANGE_OBJECT`   | S→C | Item yang melayang/jatuh di world muncul (`net_id == -1`) atau diambil (`net_id >= 0`). |
| `15`| `PACKET_SEND_LOCK`            | S→C | Informasi status kunci (world/small/big lock). |
| `18`| `PACKET_SET_ICON_STATE`       | S→C | Pembaruan efek visual status ikon. |
| `20`| `PACKET_SET_CHARACTER_STATE`  | S→C | Pembaruan pakaian, sayap, atau visual efek karakter. |
| `21`| `PACKET_PING_REPLY`           | C→S | Balasan denyut jantung (heartbeat pong) dari bot ke server. |
| `22`| `PACKET_PING_REQUEST`         | S→C | Permintaan denyut jantung (heartbeat ping) dari server ke bot. |
| `23`| `PACKET_PING_REQUEST_ALT`     | S→C | Variasi paket ping request server. |

---

## 4. Dua Aturan Kritis Protokol GTPS (Wajib Diketahui!)

### A. Aturan Validasi Panjang Paket 61-Byte (`dataLength >= 61`)
Server GTPS modern memiliki validator buffer biner yang ketat pada paket `PACKET_STATE` (pergerakan):
```text
Ukuran Minimum = 4 (Outer Type Header) + 56 (GameUpdatePacket) + 1 (Trailing Byte) = 61 Byte
```
- **Masalah**: Jika klien mengirim paket pergerakan hanya berukuran 60 byte (tepat seukuran struct), server akan menganggap paket korup atau terpotong, lalu memutuskan koneksi (*instant disconnect*).
- **Solusi dalam Kode**: Pada `TankPacketHelper::serialize` di `game_packet.hpp`, array biner selalu ditambahkan 1 byte `0x00` di akhir paket jika tidak ada payload data tambahan (`extra_len == 0`), sehingga panjang total selalu $\ge 61$ byte.

### B. Aturan Balasan Heartbeat Keepalive (Ping / Pong)
Server GTPS mengirimkan paket denyut jantung secara berkala setiap 10–20 detik berupa Tank Packet Type `21`, `22`, atau `23`:
- Jika bot tidak merespons dalam waktu 15–30 detik, server menganggap koneksi *zombie/stale* dan melakukan pemutusan paksa.
- **Balasan yang Diharapkan Server**:
  1. Balas dengan Tank Packet Type `21` (`PACKET_PING_REPLY`).
  2. Set `net_id` sama dengan local NetID karakter bot.
  3. Salin nilai `main_data` persis seperti nilai `main_data` yang dikirim server.
  4. Koordinat `pos_x = 64.0f`, `pos_y = 64.0f`, `speed_x = 1000.0f`, `speed_y = 250.0f`.
  5. Jika tipe request server bernilai `23`, kirimkan juga balasan bertipe `22`.

---

## 5. Spesifikasi VariantList (Remote Procedure Call)

VariantList adalah mekanisme pemanggilan fungsi RPC yang dikemas dalam payload tambahan Tank Packet Type `1` (`PACKET_CALL_FUNCTION`).

### Format Buffer Biner VariantList:
```text
[uint8_t count]
  ├── [uint8_t index] [uint8_t type] [Value Payload]
  ├── [uint8_t index] [uint8_t type] [Value Payload]
  └── ...
```

### Tipe Data Variant (`VariantType`):

| Type ID | Tipe Data C++ | Format Nilai dalam Buffer |
|:---:|---|---|
| `0` | None / Void | Tidak ada payload tambahan |
| `1` | `float` | 4-byte IEEE 754 float |
| `2` | `std::string` | `[uint32_t length] [char characters...]` |
| `3` | `Vector2` (`float, float`) | 8-byte (2 float berurutan) |
| `4` | `Vector3` (`float, float, float`) | 12-byte (3 float berurutan) |
| `5` | `uint32_t` | 4-byte unsigned integer |
| `8` | `bool` | 4-byte integer (`1` untuk true, `0` untuk false) |
| `9` | `int32_t` | 4-byte signed integer |

### Fungsi Variant Kunci yang Dikirim Server:

1. **`OnSuperMainStartAcceptLogon`**:
   - Menandakan kredensial diterima dan bot diizinkan memasuki server. Bot membalas dengan `action|enter_game\n`.
2. **`OnSpawn`**:
   - Menandakan avatar pemain/bot telah dimuat di world. Payload parameter kedua berupa string berisi atribut karakter (`name`, `netID`, `userID`, `country`, posisi).
3. **`OnSetPos`**:
   - Mengatur ulang koordinat karakter bot (misal saat diteleportasi atau respawn).
4. **`OnSendToServer`**:
   - Pengalihan server (*Sub-Server Redirection*). Dijelaskan detail pada bagian berikut.
5. **`OnTalkBubble`**:
   - Balon pesan chat yang muncul di atas kepala pemain (`[0] NetID`, `[1] Pesan teks`).
6. **`OnConsoleMessage`**:
   - Pesan sistem/chat yang dicetak pada kotak dialog console klien.

---

## 6. Protokol Sub-Server Redirection (`OnSendToServer`)

Dalam arsitektur server Growtopia, world-world game disebar di antara puluhan proses sub-server ENet yang berbeda. Saat bot berpindah world atau baru masuk dari lobi, server utama mengirimkan variant function `OnSendToServer`:

### Argumen Variant `OnSendToServer`:
- `vars[0]`: `"OnSendToServer"` (string)
- `vars[1]`: `sub_port` (int: port tujuan ENet sub-server)
- `vars[2]`: `token` (int: token sesi one-time autentikasi)
- `vars[3]`: `userId` (int: ID unik pengguna akun)
- `vars[4]`: `ipDoor` (string: format `"sub_ip|door_id"` atau `"sub_ip"`)
- `vars[5]`: `lmode` (int: login mode flag)

### Prosedur Penanganan Klien:
1. Parse IP dan Door ID dari string `ipDoor`.
2. Putuskan koneksi ENet saat ini: `enet.disconnect()`.
3. Bersihkan memori world lokal: `world.clearWorld()`.
4. Rancang string handshake baru untuk sub-server:
   ```text
   tankIDName|<GrowID>
   tankIDPass|<Password>
   requestedName|<GrowID>
   f|1
   protocol|225
   game_version|5.58
   user|<userId>
   token|<token>
   doorID|<door_id>
   lmode|<lmode>
   [DeviceFingerprint fields: mac, rid, wk, hash, hash2, fhash, fz, zf...]
   ```
5. Buka koneksi ENet baru ke `sub_host` : `sub_port`.
6. Kirim handshake sub-server setelah menerima `NET_MESSAGE_SERVER_HELLO`.

---

## 7. Web Dashboard Authentication & `ltoken`

Pada GTPS modern yang menggunakan sistem Dashboard / Web Validation (seperti CreativePS dengan `loginurl|login.creativeps.eu`):
1. **HTTP Pre-login**:
   - `server_data.php` mengembalikan field `loginurl|...` dan `meta|...`.
   - Bot melakukan POST ke `https://<loginurl>/player/growid/login/validate` dengan body `_token=<b64>&growId=<user>&password=<pass>`.
   - Server mengembalikan JSON: `{"status":"success","token":"<auth_token>"}`.
2. **Handshake ENet Injection**:
   - Nilai token tersebut adalah **Login Token (`ltoken`)**.
   - Handshake login ENet wajib menyertakan baris:
     ```text
     tankIDName|<GrowID>
     tankIDPass|<Password>
     ltoken|<token_dari_web_validate>
     ```
   - Tanpa baris `ltoken`, server utama akan menahan bot di pre-auth, mengirimkan dialog verifikasi (`OnDialogRequest`), atau memutus koneksi secara pasif.

---

## 8. Protokol Dialog Profil & Ekstraksi Statistik (`OnDialogRequest`)

Untuk memperoleh statistik pemain (Level, Gems, Played Hours, Account Age) persis seperti klien resmi dan Lucifer v2.85:

### A. Alur Permintaan Wrench Profil Diri Sendiri
1. Bot menunggu jeda 2.5 detik pasca-spawn (`OnSpawn`) agar koneksi game stabil.
2. Bot mengirimkan tepat 1 paket teks `NET_MESSAGE_GENERIC_TEXT`:
   ```text
   action|wrench
   netid|<local_net_id>
   ```
3. Server membalas dengan paket variant `OnDialogRequest` berisi dialog profil lengkap.

### B. Struktur Payload Dialog Profil Khas GTPS
```text
set_default_color|`o
add_label_icon|big|`wActive effects:``|left|32|
add_textbox|`2<icon>`o Double Jump|left|
add_textbox|`2<icon>`o Enhanced Digging|left|
add_textbox|You have `w36`` backpack slots.|left|
add_textbox|Current world: `wRIZIQ`` (`w90``, `w25``) (`w2`` people)|left|
add_textbox|You have the Recycler.|left|
add_textbox|You are standing on the note "F".|left|
add_textbox|Total time played is `w14.85`` hours.  This account was created `w5`` days ago.|left|
end_dialog|profile|Continue||
```

### C. Pola Regex Ekstraksi Statistik:
- **Total Played Hours**:
  `R"((?:total\s+time\s+played|time\s+played|total\s+playtime|played\s+for|playtime|played)\s*(?:is|:|=)?\s*([0-9]+(?:\.[0-9]+)?)\s*hours?)"`
  Mengambil nilai `14.85` dari `"Total time played is 14.85 hours."`.
- **Account Age (Hari)**:
  `R"((?:account\s+was\s+created|was\s+created|created)\s+([0-9]+)\s*days?\s*ago)"`
  Mengambil nilai `5` dari `"This account was created 5 days ago."`.
- **Sanitasi Color Codes (Penting!)**:
  Dalam Growtopia, tag ``` adalah tag reset warna bawaan. Fungsi `stripColorCodes` wajib menghapus seluruh pasangan karakter '`' dan kode warnanya agar tidak meninggalkan karakter backtick liar yang dapat merusak parsing angka numerik (misal `14.85`` menjadi `14.85` `).
- **Auto-Dismiss Dialog**:
  Setelah dialog berhasil dibaca, bot mengirimkan penutup dialog setelah jeda manusia 800ms:
  ```text
  action|dialog_return
  dialog_name|<dname>
  buttonClicked|
  ```

---

## 9. Protokol Dropped Item & Auto Pickup / Collect

### A. Broadcast Pemunculan & Pengambilan Item (Server ke Bot - Tank Packet 14)
Server GTPS menyiarkan pembaruan item terapung di world melalui Tank Packet:
- **Tipe Paket**: `PACKET_ITEM_CHANGE_OBJECT` (`14` / variasi `8`).
- **Gotcha Kritis `count1 == 0` vs Koordinat Fisik**:
  - Pada struktur GTPS berbasis C++, `packetType` sering ditulis sebagai integer 32-bit (`int32_t type = 14`). Akibatnya, byte ke-2 dari header (`count1`) **selalu bernilai 0** pada paket drop masuk.
  - Mengevaluasi `if (count1 == 0)` sebagai tanda item terambil adalah **BUG FATAL** yang menyebabkan setiap item baru jatuh langsung dihapus seketika dari memori world bot.
- **Pembeda Mutlak Siklus Hidup (Lifecycle) & Resolusi UID Dropped Item**:
  - **Item Jatuh / Muncul di Lantai (`is_on_ground = true`)**:
    - Terdeteksi saat: `pos_x > 0.0f || pos_y > 0.0f || count1 > 0`.
    - `target_net_id`: **Player Avatar NetID (pelempar item)**, BUKAN Object UID! Menyetel `uid = target_net_id` adalah fatal karena menimpa Object UID dengan avatar NetID pemain di world (misal NetID=1 milik player lain).
    - `main_data`: Item ID (jenis item, misal 5032 / Water Essence).
    - `float_var` / `count1`: Jumlah item yang dijatuhkan (default 1 jika 0).
    - **Resolusi Object UID**: Mengikuti standar mesin server Growtopia (Proton/CPS), setiap world menyimpan `last_dropped_uid`. Saat item baru jatuh, client mengalokasikan UID sekuensial `uid = ++world->last_dropped_uid` (kecuali jika `target_net_id > last_dropped_uid` dan bukan NetID pemain aktif, yang menandakan server emulator mengirim UID eksplisit).
    - Bot memanggil `WorldState::addOrUpdateDroppedItem(uid, item_id, count, pos_x, pos_y)` dan segera memicu pemeriksaan serap proksimitas jika dekat dengan posisi bot (`<= 96 px`).
  - **Item Terambil / Menghilang dari Lantai (`is_on_ground = false`)**:
    - Terdeteksi saat: `pos_x == 0.0f && pos_y == 0.0f`.
    - `main_data` (atau fallback `target_net_id`): Object UID (`oid`) item yang terambil dari lantai (misal 558).
    - `net_id`: NetID avatar player yang menyerap item tersebut (misal NetID=33 milik bot).
    - Bot memanggil `WorldState::removeDroppedItem(uid)` serta membersihkan cache cooldown percobaan di `BotActions` untuk membersihkan memori.

### B. Paket Pengambilan Item Klien (Bot ke Server - Dual-Payload Dispatch)
Klien resmi Growtopia dan Lucifer mengambil dropped item bukan dengan mengirim Tank Packet 8, melainkan:
- **Tipe Paket**: `PACKET_ITEM_ACTIVATE_OBJECT_REQUEST` (Tank Packet Type `11` / `0x0B`).
- **Dual-Payload Dispatch Architecture (Anti-Miss)**:
  Karena variasi emulator GTPS (GT-Server, GrowtopiaServer, GTCpp, CPS) mengimplementasikan pembacaan field paket 11 yang berbeda-beda, bot mengirimkan **dua varian payload terkoordinasi**:
  1. **Format V1 (Standar Growtopia / Lucifer / Bothax)**:
     - `pkt.type = PACKET_ITEM_ACTIVATE_OBJECT_REQUEST` (`11`).
     - `pkt.net_id = -1` (Unset/wildcard net_id).
     - `pkt.target_net_id = 0`.
     - `pkt.main_data = object_uid` (Object UID di field `value/main_data`).
     - `pkt.pos_x = pos_x`, `pkt.pos_y = pos_y`.
     - `pkt.int_x = -1`, `pkt.int_y = -1`.
  2. **Format V2 (Fallback Tile Coordinate 0 / Modern CPS)**:
     - `pkt.type = PACKET_ITEM_ACTIVATE_OBJECT_REQUEST` (`11`).
     - `pkt.net_id = -1`.
     - `pkt.target_net_id = 0`.
     - `pkt.main_data = object_uid`.
     - `pkt.pos_x = item_x`, `pkt.pos_y = item_y`.
     - `pkt.int_x = 0`, `pkt.int_y = 0`.
- **Proteksi Sanitasi UID**:
  Paket tidak dikirim jika `object_uid <= 0` untuk menghindari spam penolakan server terhadap UID `-1`.

### C. Heuristik Parsing Dropped Items pada Map Data (Type 4)
Pada saat bot memasuki world (`PACKET_SEND_MAP_DATA`), daftar item yang jatuh terletak di akhir paket setelah seluruh petak world:
1. **Jalur A (Forward Parsing)**: Membaca setelah pembacaan tile selesai. Bila format tile standar, header `d_count` (uint32) dan `last_uid` (uint32) langsung terbaca.
2. **Jalur B (Reverse Signature Scanner)**: Jika terdapat extra data tile kustom GTPS yang menggeser pointer pembacaan, scanner memindai mundur dari akhir buffer hingga batas petak.
   - **Toleransi Trailing Bytes**: Diperluas hingga 4096 bytes untuk mengantisipasi data cuaca, world timer, atau string tambahan di akhir map data.
   - **Dukungan Gems & Untyped Drops (`item_id == 0`)**: Pada GTPS, Gems atau objek khusus sering memiliki `item_id == 0`. Validator tidak membuang array dropped item hanya karena ada item ID 0.
   - **UID Fallback Generator**: Jika server menyetel UID `0` pada dropped items di map data, bot mengalokasikan UID fallback unik lokal secara berurutan agar item tetap dapat diproses dan diserap.

### C. Mekanisme Movement-Triggered & Autonomous Collection (Tanpa Tombol Manual)
Server Growtopia/GTPS secara ketat memvalidasi jarak (*proximity check*) antara koordinat pemain dan koordinat item floating:
- **Trigger Terintegrasi Loop Pergerakan (`moveTo` & Pathfinding)**:
  - Setiap kali bot berpindah tile (baik 1 langkah maupun multi-langkah `FindPath`/`moveTo`), fungsi `triggerMovementCollect` langsung dipanggil pada koordinat tile tujuan dan setiap koordinat intermediate.
  - Bot memeriksa keberadaan floating item dalam radius jangkauan (default 1.5 tile, dapat diatur hingga 4.0 tile) dan langsung mengirimkan `PACKET_ITEM_ACTIVATE_OBJECT_REQUEST` (type 11).
  - Pengambilan dilakukan secara non-blocking sehingga tidak pernah menghentikan atau memperlambat kelancaran langkah bot.
- **Integrasi Keypad Global (WASD & Tombol Panah)**:
  - GUI menyediakan listener input keyboard terpadu untuk tombol `W`, `A`, `S`, `D` dan `Up`, `Down`, `Left`, `Right` (hanya aktif saat tidak sedang mengetik di input text).
  - Setiap input pergerakan keypad langsung menggerakkan bot dan seketika menyerap item yang berada di tile yang dilalui atau di sampingnya.
- **Interval & Penanganan Anomali Server**:
  - Delay interval antar request pickup diatur default ke **20 ms** ultra-cepat (dapat diatur runtime 10–500 ms).
  - Per-UID tracking mencegah pengiriman ganda pada frame yang sama, namun mengizinkan retry otomatis jika server sempat mengalami lag dan item belum terhapus dari world.
- **Background Proximity Daemon (Always-On Fallback)**:
  - Thread mandiri pada `AutoCollectController` (tick sinkron 20ms) mendeteksi item jika bot sedang diam/idle di samping floating item atau jika item dijatuhkan oleh player lain di dekat bot.
  - Untuk setiap item floating yang terdeteksi, bot memvalidasi kapasitas inventaris (`canPickupItem`):
    - **Gems (Item ID 112) & Item ID 0**: Selalu diambil tanpa syarat, karena Gems tidak mengonsumsi slot tas.
    - **Item Biasa**: Diambil jika bot masih memiliki slot inventaris kosong ATAU item tersebut sudah ada di tas dengan jumlah $< 250$ (mendukung server CPS/GTPS bertumpuk 250). Jika tas penuh, item dilewati dan bot tetap melanjutkan pengambilan Gems atau item lain yang muat.
- **Eliminasi Ketergantungan Tombol Manual**:
  - Tombol manual "Collect Nearby" digantikan oleh badge status otomatis `⚡ Auto-Collect on Move: AKTIF` pada UI D-Pad dan tab kontrol utama.

### D. Kompatibilitas Lua
- `getObjects()` / `bot:getObjects()`: Mengembalikan daftar seluruh item floating di world lengkap dengan `oid`, `itemid`, `count`, `pos`, `tile_x`, `tile_y`.
- `getObject(oid)` / `bot:getObject(oid)`: Mengembalikan data satu objek berdasarkan UID.
- `collectObject(oid, [range])` / `bot:collectObject(oid, [range])`: Mengambil dropped item via UID. Jika `range` diberikan dan jarak melebihi `range`, bot dapat melangkah ke item terlebih dahulu.
- `collectByID(item_id, [range])` / `bot:collectByID(item_id, [range])`: Mencari item floating terdekat dengan Item ID tertentu dan mengambilnya.
- `Collect(oid)` / `Collect(obj)` / `Collect([radius])` / `CollectNearby([radius])`: Kompatibel penuh dengan gaya penulisan Bothax.

---

## 23. Continuous High-Frequency Scanner & Dual-Payload Protocol Dispatch

### A. Latar Belakang & Analisis Masalah Pengambilan Item di GTPS
Pada pengujian langsung di berbagai core server GTPS (Proton, CPS, Lucifer, GrowServer), sering ditemukan anomali di mana dropped item tidak terambil meskipun log bot mencatat pengiriman paket:
1. **Divergensi Format Paket Type 11 (`PACKET_ITEM_ACTIVATE_OBJECT_REQUEST`)**:
   - **Format V1 (Classic Proton)**: Membaca `packet->m_netID` sebagai Object UID yang berada di lantai, dengan `pos_x, pos_y` sebagai posisi berdiri pemain.
   - **Format V2 (Modern CPS / Lucifer Core)**: Membaca `packet->m_netID` sebagai NetID bot pemain lokal, `packet->m_main_data` sebagai Object UID, dan `pos_x, pos_y` sebagai posisi tepat floating item (`item.x, item.y`).
   - *Dampak*: Jika bot hanya mengirim format V1 ke server bertipe CPS/Lucifer, server menolak paket karena mendeteksi NetID mismatch. Sebaliknya jika hanya format V2 ke Proton, packet handler Proton mengira itu entity yang salah.
2. **Divergensi Opcode Broadcast Drop**:
   - Mayoritas GTPS menggunakan Tank Type 14 (`PACKET_ITEM_CHANGE_OBJECT`).
   - Namun beberapa server kustom mengirimkan event kemunculan item melalui Tank Type 8 (`PACKET_TILE_APPLY_DAMAGE` / object mutation) dengan `count1 > 0`.
3. **Ketidaksinkronan Cache Memori vs Objek Nyata di Lantai**:
   - Jika paket broadcast drop terlewat atau terjadi desinkronisasi saat bot join world, daftar memori lokal kosong sehingga bot ragu mengirimkan paket collect.

### B. Solusi: Dual-Payload Protocol Dispatch
Bot sekarang mengeksekusi pengiriman ganda berurutan secara transparan pada setiap kali fungsi `collect(...)` dipanggil:
- **Payload 1 (Format V1 - Standar Growtopia / Lucifer / Bothax)**:
  `net_id = -1`, `target_net_id = 0`, `main_data = object_uid`, `pos = player_pos`, `int_x = -1`, `int_y = -1`
- **Payload 2 (Format V2 - Fallback Tile Coordinate 0 / Modern CPS)**:
  `net_id = -1`, `target_net_id = 0`, `main_data = object_uid`, `pos = item_pos`, `int_x = 0`, `int_y = 0`
- *Hasil*: Server varian apa pun akan selalu menerima setidaknya satu format yang cocok secara sempurna tanpa menolak request.

### C. Continuous Active Scanner Daemon (20ms – 50ms)
- Thread `passiveProximityWorker()` berjalan tanpa henti di latar belakang memindai dropped item dalam radius jangkauan bot (default 2.5 tile / 80px).
- Didukung **Wildcard Proximity Probe (`collect(-1, px, py)`)** yang otomatis terpicu secara berkala (throttled 200ms) saat bot melangkah, menyerap seluruh item di lantai meskipun cache UID memori belum terisi.

### D. Live GUI Telemetry Monitor
Panel visual realtime pada tab Automation menampilkan:
- Jumlah total item mengambang di seluruh world.
- Jumlah item dalam jangkauan avatar (<2.5 tile).
- Tracking item terdekat (Item ID, UID, dan jarak dalam tile).
- Status aktif detak scanner daemon (ms) dan counter total item yang berhasil diserap sesi ini.

---

## 25. GTPS Tile Extra Data Truncation, UID Overflow Prevention & Hitbox Boundary Tolerancing

### A. Gejala & Temuan Root Cause: Kegagalan Pengambilan Item Berulang
Melalui live debugging log dan trace paket ENet, ditemukan penyebab mengapa bot mengalami kegagalan total saat mengambil item di GTPS kustom (misal `snecks`):

1. **Early Termination Pembacaan Tile & False-Clean World Parser**:
   - Format tile GTPS custom (misal tipe lock/door khusus) menyebabkan `WorldState::parseMapData` berhenti lebih awal sebelum mencapai akhir buffer data tile (`remaining > 20,000` byte).
   - Jalur pembacaan langsung (Jalur A) sebelumnya membaca 8 byte pada posisi arbitrary tersebut: 4 byte bernilai `00 00 00 00` (`d_count == 0`), diikuti 4 byte `00 08 00 0E` (`last_uid == 234883072` / 234 juta).
   - Nilai 234883072 ini sesungguhnya adalah representasi byte tile Growtopia (`tile flags 0x0800` dan `item_id 14`), bukan UID dropped item!
   - Akibatnya:
     - Parser menetapkan `last_dropped_uid_ = 234883072` dan menganggap world bersih (0 items).
     - Signature Scanner (Jalur B) tidak pernah dieksekusi.
     - Setiap item yang baru jatuh dialokasikan dengan UID fantastis `234883073, 234883074, dst.`.
     - Server GTPS yang menggunakan counter integer kecil (1, 2, 3...) menolak seluruh paket collect karena UID 234883073 tidak ada di server.
     - Ketika pemain lain mengambil item (server broadcast Tank 14 dengan `main_data = 17`), fungsi `setLastDroppedUid(17)` mengabaikannya karena pengecekan `if (uid > last_dropped_uid_)` gagal (`17 > 234883072 == false`).

2. **Tile Boundary Truncation Bug pada Jarak 0-Tile**:
   - Posisi fisik item di Growtopia sering jatuh beberapa pixel di dekat tepi tile (misal $x = 542$ px, $y = 781$ px).
   - Sementara avatar bot berdiri di koordinat $x = 544$ px, $y = 768$ px (hanya selisih 2 pixel!).
   - Pembagian integer tile (`544 / 32 = 17` vs `542 / 32 = 16`) menyebabkan perbandingan `cur_tx == item_tx` menghasilkan `17 == 16` (FALSE).
   - Bot menolak menyerap item padahal secara fisik berdiri tepat di atas item tersebut!

### B. Solusi Komprehensif & Robustness Guard
1. **Sanitasi Keras UID (`< 5,000,000`) & Inisialisasi Ulang**:
   - `last_dropped_uid_ = 0` selalu direset bersih setiap kali map data baru dimuat.
   - Jalur A hanya menerima `d_count == 0` jika `rem <= 24` dan `last_uid < 5000000`.
   - Jalur B Signature Scanner memindai maju untuk seluruh world dengan floating items.
   - Jika world tidak memiliki floating items (`!parsed_dropped`), tail scanner memindai 64 byte terakhir buffer map data untuk menemukan header count 0 dan last_uid asli server.
   - `setLastDroppedUid` dan `allocateNextDroppedUid` memiliki clamp pelindung: jika counter $\ge 5000000$, counter seketika di-reset ke 0 atau disinkronkan ke UID valid dari server.
2. **Tolerance Box 24px pada Mode 0-Tile**:
   - Pengecekan jarak 0-tile diperbarui menjadi:
     `(cur_tx == item_tx && cur_ty == item_ty) || (std::abs(dx) <= 24.0f && std::abs(dy) <= 24.0f)`.
   - Ini menjamin item yang berada di perbatasan tile (selisih 1-24 pixel) diserap seketika tanpa terhalang pembulatan integer grid tile.
3. **Desync-Tolerant Item Removal**:
   - Saat menerima Tank 14 collect packet, jika UID eksak tidak ditemukan dalam cache memori lokal bot (karena desync counter), fungsi `removeDroppedItem` mencari dan menghapus item terdekat dengan selisih UID $\le 2$.
   - Mencegah timbulnya ghost items di memori bot.

