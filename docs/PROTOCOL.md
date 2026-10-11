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
| `7` | `PACKET_TILE_ACTIVATE_REQUEST`| C→S | Memasuki pintu / portal pada posisi tile bot. |
| `8` | `PACKET_ITEM_CHANGE_OBJECT` | S→C | Item yang melayang/jatuh di world muncul atau diambil. |
| `9` | `PACKET_SEND_INVENTORY_STATE` | S→C | Data tas inventaris pemain yang dikirim saat masuk world. |
| `11`| `PACKET_ITEM_ACTIVATE_OBJECT_REQ` | C→S | Mengambil item yang mengambang di dekat bot (`collect`). |
| `18`| `PACKET_SET_ICON_STATE` | S→C | Pembaruan efek visual status ikon. |
| `20`| `PACKET_SET_CHARACTER_STATE` | S→C | Pembaruan pakaian, sayap, atau visual efek karakter. |
| `21`| `PACKET_PING_REPLY` | C→S | Balasan denyut jantung (heartbeat pong) dari bot ke server. |
| `22`| `PACKET_PING_REQUEST` | S→C | Permintaan denyut jantung (heartbeat ping) dari server ke bot. |
| `23`| `PACKET_PING_REQUEST_ALT` | S→C | Variasi paket ping request server. |

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

### A. Broadcast Pemunculan & Pengambilan Item (Server ke Bot)
Server GTPS menyiarkan pembaruan item terapung di world melalui Tank Packet:
- **Tipe Paket**: `PACKET_ITEM_CHANGE_OBJECT` (`8`) atau variasi event server (`14`).
- **Ketika Item Jatuh / Muncul Baru**:
  - `net_id`: UID objek yang dialokasikan server (atau `-1` pada beberapa GTPS non-resmi).
  - `target_net_id` / `main_data`: Item ID atau UID sekunder.
  - `count1`: Jumlah item yang jatuh.
  - `pos_x`, `pos_y`: Koordinat piksel item mengambang di world.
  - *Gotcha GTPS*: Jika server mengirim `net_id = -1`, bot membangkitkan UID fallback unik (`atomic<uint32_t>`) agar item tidak saling menimpa dalam `WorldState`.
- **Ketika Item Terambil / Menghilang**:
  - Disiarkan dengan kondisi `count1 == 0` atau item broadcast penghapusan.
  - Bot menghapus item dari memori lokal berdasarkan UID dan mencocokkan radius koordinat piksel terdekat (`removeDroppedItemByPos`).

### B. Paket Pengambilan Item Klien (Bot ke Server)
Klien resmi Growtopia dan Lucifer mengambil dropped item bukan dengan mengirim Tank Packet 8, melainkan:
- **Tipe Paket**: `PACKET_ITEM_ACTIVATE_OBJECT_REQUEST` (Tank Packet Type `11` / `0x0B`).
- **Struktur Field Kritis**:
  - `pkt.type = PACKET_ITEM_ACTIVATE_OBJECT_REQUEST` (`11`)
  - `pkt.net_id = object_uid`
  - `pkt.target_net_id = object_uid`
  - `pkt.main_data = object_uid`
  - `pkt.pos_x = player_pos_x`, `pkt.pos_y = player_pos_y` (koordinat piksel posisi berdiri bot saat mengambil)
- **Kompatibilitas Lua**:
  - `Collect(oid)`: Mengirim paket aktivasi pengambilan untuk UID objek spesifik.
  - `Collect(obj)`: Menerima langsung tabel `WorldObject` dari `GetObjectList()`, mengekstrak `oid`/`uid`.
  - `Collect(radius)` / `CollectNearby(radius)`: Memindai item di sekitar bot dalam jangkauan radius tertentu dan mengirim paket pengambilan secara otomatis.

