# 🛡️ Anti-Deteksi, Keamanan, & Jaringan Proxy — Lucifer GTPS Bot

Dokumen ini menjelaskan strategi mitigasi deteksi otomatis server (*anti-cheat evasion*), sistem fingerprinting hardware persisten, mekanisme pemulihan koneksi (*exponential backoff*), dan implementasi proxy SOCKS5.

---

## 1. Analisis Mekanisme Deteksi Server GTPS

Server Growtopia modern menerapkan beberapa lapisan deteksi otomatis untuk mengidentifikasi bot:
1. **Pemeriksaan Identitas Perangkat (Hardware Fingerprint)**:
   - Server mencatat MAC Address, RID (UUID perangkat), dan hash hardware.
   - Jika satu akun login dengan MAC/RID yang terus berganti setiap beberapa menit, server menandai akun tersebut sebagai anomali/spoofed.
   - Jika beberapa bot login secara bersamaan dengan MAC/RID yang sama persis, server langsung menjatuhkan ban IP atau membekukan akun.
2. **Analisis Waktu Tindakan (Timing Heuristics)**:
   - Pemain manusia memiliki variasi alami pada setiap klik/ketukan tombol (deviasi 10–50ms).
   - Bot tradisional yang menggunakan jeda konstan (misal tepat 180.00ms setiap pukulan) sangat mudah dideteksi oleh filter heuristik server.
3. **Thundering Herd saat Server Restart**:
   - Jika 50 bot mencoba menyambung ulang secara simultan di milidetik yang sama saat server baru menyala, firewall server akan memblokir alamat IP karena dianggap serangan DoS/DDoS.

---

## 2. Generator Fingerprint Perangkat (`DeviceFingerprint`)

Modul `anti_detection.hpp` menyediakan pembuat identitas perangkat realistis:

### Komponen Kunci Fingerprint:

| Parameter | Tipe & Format | Penjelasan Teknis |
|---|---|---|
| `mac` | String `XX:XX:XX:XX:XX:XX` | Alamat MAC acak. Byte pertama disetel dengan bit *Locally Administered* (`(byte & 0xFE) | 0x02`) dan *Unicast* agar valid menurut spesifikasi IEEE 802. |
| `rid` | String Hex (32 Karakter) | ID Registrasi perangkat Windows (format UUID/GUID tanpa strip pemisah). |
| `wk` | String Hex (32 Karakter) | Kunci token instalasi game (format MD5 hash). |
| `hash` | Signed 32-bit Integer | Nilai hash integer perangkat (rentang negatif, misal `-16777216`). |
| `hash2`| Signed 32-bit Integer | Hash sekunder sistem operasi Windows. |
| `fhash`| Signed 32-bit Integer | File integrity hash runtime game. |
| `fz` | Unsigned 32-bit Integer | Nilai seed integer acak positif. |
| `zf` | Signed 32-bit Integer | Nilai cermin persis dari `fz`: $zf = -fz$. |
| `country` | String (2 Karakter) | Kode negara ISO-3166-1 alpha-2 (misal `"id"`, `"us"`, `"sg"`). |
| `device_version` | Integer | Versi OS platform (`0` untuk Windows). |

### Sistem Penyimpanan Persisten (cache/fingerprints/)
- Setiap bot yang dibuat akan memeriksa folder `cache/fingerprints/<GrowID>.json`.
- **Jika sudah ada**: Fingerprint tersimpan dimuat kembali. Hal ini menjamin bahwa satu akun bot akan selalu menggunakan MAC, RID, dan hash yang konsisten di setiap sesi.
- **Migrasi Otomatis**: Jika file belum ada di `cache/fingerprints/` tetapi ada di `.fingerprints/` (versi terdahulu), sistem otomatis memindahkan/menyalin profil ke dalam folder cache baru.
- **Jika belum ada**: Fingerprint acak baru di-generate dan otomatis disimpan ke file JSON tersebut.
- Pengguna dapat me-reset fingerprint kapan saja melalui tombol **🎲 Acak Ulang Fingerprint** di tab GUI.

### Sub-Tab GUI: RID (Network & Hardware Identity Inspector)
Untuk memudahkan audit dan verifikasi identitas setiap bot, tersedia sub-tab **RID** tepat di samping sub-tab **Logs**:
1. **IP Terbaca di Game (`ip`)**: Tidak sekadar menampilkan teks "Direct", melainkan **alamat IP numerik asli** yang dibaca dan dilihat oleh server GTPS (misalnya `198.23.243.226` via SOCKS5 Proxy, atau IP publik ISP `182.9.36.244` jika koneksi langsung). Sistem dilengkapi deteksi otomatis via `HttpClient::fetchPublicIp`, verifikasi paket respon server game (`OnConsoleMessage`), tombol `📋 Copy`, dan tombol `🔄` untuk re-check IP instan.
2. **MAC Address (`mac`)**: Format `XX:XX:XX:XX:XX:XX`. Dilengkapi tombol `📋 Copy`.
3. **RID (`rid`)**: UUID unik identitas perangkat Windows 32-karakter. Dilengkapi tombol `📋 Copy`.
4. **Country Code (`country`)**: Kode negara akun (misalnya `id`, `us`). Dilengkapi tombol `📋 Copy`.
5. **WK Token (`wk`)**: Kunci otentikasi instalasi game 32-karakter. Dilengkapi tombol `📋 Copy`.
6. **Parameter Anti-Deteksi Lengkap**: Menampilkan nilai `hash`, `hash2`, `fhash`, `fz / zf`, dan `device_version`.
7. **Aksi Cepat & Sinkronisasi**:
   - `🎲 Acak Ulang Fingerprint Akun Ini`: Me-roll identitas baru untuk bot yang dipilih dan langsung memperbarui file `cache/fingerprints/<GrowID>.json`.
   - `💾 Simpan ke File JSON`: Menyimpan konfigurasi aktif ke file cache.
   - `📋 Salin Semua Info Akun`: Menyalin seluruh parameter akun & jaringan sekaligus ke clipboard dalam format teks terstruktur.

---

## 3. Acak Delay Tindakan (Human Jitter) & Micro-Pauses

Modul `AntiDetection` menyisipkan ketidakpastian alami pada eksekusi tindakan:

### A. Jitter Bounded Uniform Distribution
Fungsi `AntiDetection::sleepWithJitter(base_ms, jitter_ms)` menghitung delay acak:
$$\text{delay} = \text{UniformRandom}(\max(\text{min\_ms}, \text{base} - \text{jitter}), \text{base} + \text{jitter})$$

- Pada gerakan (`moveTo`): Jeda per tile diberi variasi $\pm25\%$.
- Pada pemukulan (`hit`) & penanaman (`place`): Delay diberi variasi $\pm15\%$.
- Menghasilkan pola paket yang tampak seperti ritme jari pemain manusia asli pada grafik analisis server.

### B. Simulasi Jeda Manusia (Micro-Pauses)
Fungsi `AntiDetection::maybeHumanPause(chance, min_ms, max_ms)`:
- Memiliki probabilitas $3\text{--}4\%$ di setiap siklus tindakan untuk mengambil jeda singkat $150\text{--}350\text{ ms}$.
- Mensimulasikan jeda sesaat saat pemain manusia ragu-ragu, membaca chat, atau mengalami fluktuasi input.

---

## 4. Auto-Reconnect dengan Exponential Backoff

Ketika bot terputus dari server akibat lag, server restart, atau disconnect event, `ReconnectController` mengelola pemulihan koneksi secara cerdas.

### Algoritma Perhitungan Waktu Tunggu:
$$\text{Delay}_{\text{base}} = \min\left(\text{MaxDelay}, \text{InitialDelay} \times (\text{Multiplier})^{\text{RetryCount}}\right)$$
$$\text{Delay}_{\text{aktual}} = \text{Delay}_{\text{base}} \pm (\text{Delay}_{\text{base}} \times \text{JitterRatio})$$

### Parameter Default:
- `InitialDelay`: 2.000 ms (2 detik)
- `Multiplier`: 2.0x
- `MaxDelay`: 30.000 ms (30 detik)
- `JitterRatio`: 0.15 ($\pm15\%$)

### Simulasi Penjadwalan Percobaan:
- **Percobaan 1**: $\sim 2\text{ detik}$
- **Percobaan 2**: $\sim 4\text{ detik} \pm 15\%$ ($3.4\text{--}4.6\text{ detik}$)
- **Percobaan 3**: $\sim 8\text{ detik} \pm 15\%$ ($6.8\text{--}9.2\text{ detik}$)
- **Percobaan 4**: $\sim 16\text{ detik} \pm 15\%$ ($13.6\text{--}18.4\text{ detik}$)
- **Percobaan 5 dst**: Mentok di $30\text{ detik} \pm 15\%$

### Reset Otomatis:
Counter percobaan (`retry_count`) otomatis di-reset ke `0` begitu bot berhasil menerima `PACKET_SEND_MAP_DATA` atau `OnSpawn`, menandakan bot telah kembali aktif di dalam game.

---

## 5. Protokol Proxy SOCKS5 (RFC 1928 & RFC 1929)

Untuk kebutuhan multi-bot skala besar dan isolasi IP alamat rumah, bot mendukung sistem proxy **SOCKS5** berfitur lengkap per-akun maupun global.

### A. Alur Negosiasi SOCKS5 CONNECT (TCP):
Digunakan untuk komunikasi HTTP login `server_data.php` dan pengujian latensi proxy:
```text
Client                                  Proxy Server
  │                                           │
  ├── 0x05, 0x01, 0x00 (Greeting) ───────────►│
  │◄── 0x05, 0x00 (Metode No Auth Diterima) ──┤
  │                                           │
  ├── [Opsional Auth: 0x01, User, Pass] ─────►│
  │◄── 0x01, 0x00 (Auth Sukses) ──────────────┤
  │                                           │
  ├── 0x05, 0x01 (CONNECT), ATYP, ADDR, PORT ─►│
  │◄── 0x05, 0x00 (Connection Established) ──┤
  │                                           │
  └─── [Mulai Alur Data Terowongan TCP] ─────►│
```

### B. Terowongan ENet UDP: SOCKS5 UDP ASSOCIATE Loopback Bridge (`Socks5UdpTunnel`)
Karena library `libenet` berkomunikasi murni melalui paket datagram UDP tanpa dukungan bawaan untuk proxy SOCKS5, Lucifer GTPS Bot mengimplementasikan abstraksi terowongan transparan [Socks5UdpTunnel](file:///c:/Users/RIZIQ/Documents/testtt/src/socks5.hpp#L68):

```text
[ ENet Client ] 
       │ (Raw UDP datagrams)
       ▼
[ Socks5UdpTunnel (127.0.0.1:ephemeral) ]
       │ 1. Tambahkan RFC 1928 UDP Request Header (10 bytes: RSV, FRAG, ATYP=1, Target IP, Target Port)
       │ 2. Pertahankan TCP Control Socket tetap terbuka (Keepalive)
       ▼ (Encapsulated UDP packets)
[ SOCKS5 Proxy Relay ]
       │
       ▼ (Remote Internet UDP)
[ GTPS Game Server ]
```

1. **TCP Control Socket**: Menghubungi proxy SOCKS5, menegosiasikan otentikasi RFC 1929 (jika ada user/pass), dan mengirim perintah `CMD 0x03` (`UDP ASSOCIATE`). Proxy merespons dengan alamat IP Relay dan Port Relay UDP.
2. **Local Loopback Relay**: Membuka socket UDP lokal pada `127.0.0.1` dengan port ephemeral dinamis.
3. **Transparent ENet Wrapping**: `ENetClient` menghubungkan peer-nya ke `127.0.0.1:<local_port>`. Setiap paket UDP keluar dari ENet secara otomatis dibungkus dengan header RFC 1928 10-byte lalu dikirim ke Relay Proxy. Paket balasan dari proxy di-decapsulate dan diteruskan kembali ke ENet lokal dengan overhead latensi ultra-rendah ($\sim 0.05\text{ ms}$).
4. **Dynamic Redirection**: Saat menerima event `OnSendToServer` (sub-server switching), tunnel otomatis menutup sesi lama dan membuka relay ke sub-server baru tanpa membocorkan IP asli bot.

### C. Fleksibilitas Format Konfigurasi Proxy (Quick Paste)
Parser konfigurasi proxy [Socks5Config::parse](file:///c:/Users/RIZIQ/Documents/testtt/src/socks5.cpp#L30) secara otomatis mengenali dan menoleransi berbagai format string dari berbagai provider proxy:
1. `host:port:username:password` (Format standar provider)
2. `username:password:host:port` (Format alternatif)
3. `host:port` (Proxy tanpa password / IP whitelist)
4. `socks5://username:password@host:port` (URL scheme RFC standard)
5. Mendukung trimming otomatis karakter spasi, newline, carriage return, dan quote.

### D. Distribusi Proxy Batch (Round-Robin) di GUI
Pada tab **🌐 Proxy** di Lucifer GUI:
- **Pusat Kontrol Proxy**: Menyediakan formulir uji cepat dengan tombol **⚡ Test Koneksi & Ping**, tombol **Jadikan Default**, dan tombol **Terapkan ke Bot Terpilih**.
- **Distribusi Batch (Round-Robin)**: Pengguna dapat menempelkan daftar banyak proxy pada textarea (satu proxy per baris), lalu klik **🔁 Distribusikan ke Semua Bot**. Sistem akan membagikan proxy secara bergantian (Round-Robin) ke seluruh bot di daftar.
- **Tabel Monitoring Proxy**: Menampilkan seluruh bot, ID, nama akun, status proxy (Direct / SOCKS5), dan tombol cepat **Uji Latensi** per-bot.

### E. Integrasi Lua Scripting API
Script Lua dapat membaca, mengubah, dan menguji proxy secara dinamis saat runtime:
```lua
-- Mengatur proxy bot (format string apa pun yang didukung)
bot:setProxy("127.0.0.1:1080:myuser:mypass")

-- Mengambil konfigurasi proxy saat ini
local current = bot:getProxy()
print("Proxy aktif: " .. current)

-- Menghapus proxy (kembali ke Direct)
bot:setProxy("")

-- Menguji koneksi proxy dan membaca latensi
local ok, ping_ms, msg = testProxy("127.0.0.1:1080:myuser:mypass")
if ok then
    print(string.format("Proxy OK! Latensi: %.1f ms", ping_ms))
else
    print("Proxy Gagal: " .. msg)
end
```

### F. Mode Hybrid (TCP-Only SOCKS5 + Direct UDP Fallback)
Sebagian besar penyedia proxy komersial (seperti Webshare, BrightData, Oxylabs, Smartproxy) hanya mengaktifkan `CMD 0x01` (TCP CONNECT) untuk web browsing, dan menonaktifkan `CMD 0x03` (UDP ASSOCIATE) sehingga mengembalikan error RFC 1928 `REP 0x07 (Command not supported)`.

Untuk mengatasi keterbatasan provider ini tanpa mengorbankan keamanan bot:
1. **Pemisahan Jalur Protokol**:
   - **HTTP Login & Web Auth (TCP)**: Tetap dialirkan 100% melalui SOCKS5 TCP CONNECT. Server GTPS (`server_data.php` dan `login.creativeps.eu`) mendeteksi IP proxy, sehingga akun bebas dari rate limiting dan web ban.
   - **Game Protocol (UDP ENet)**: Jika proxy menolak UDP ASSOCIATE, bot secara otomatis beralih (*graceful fallback*) ke Direct UDP untuk koneksi in-game.
2. **Flag Konfigurasi `udp_fallback_direct`**:
   - Nilai default: `true`.
   - Menjamin bot langsung bisa masuk ke dalam game dan tidak terjebak dalam siklus reconnect tak berujung meskipun menggunakan proxy TCP-Only.

---

## 5. Mitigasi Deteksi Spam & Rate Limiting Server (Anti-Spam / Anti-Macro)

### A. Analisis Masalah `Spam detected!`
Pada Growtopia / GTPS, server menerapkan filter frekuensi paket teks (`action|input` dan `action|wrench` / `dialog_return`):
- Jika klien mengirim chat/command berturut-turut tanpa jeda minimal ~1.5–2 detik, server otomatis menolak input dan membalas pesan konsol:
  `>>Spam detected! Please wait a bit before typing anything else. Please note, any form of bot/macro/auto-paste will get all of your accounts banned, so don't do it!`
- Pengiriman paket duplikat (`NET_MESSAGE_GENERIC_TEXT` bersamaan dengan `NET_MESSAGE_GAME_MESSAGE` pada milidetik yang sama) mempercepat pemicuan filter spam ini dan meningkatkan risiko autoban.

### B. Mekanisme Mitigasi & Staggered Delay
1. **Single-Packet Rule**:
   - Seluruh perintah teks (`action|wrench`, `action|dialog_return`, `action|input`) hanya dikirim satu kali menggunakan `NET_MESSAGE_GENERIC_TEXT`. Duplikasi paket `NET_MESSAGE_GAME_MESSAGE` dihilangkan.
2. **Jeda Pasca-Spawn (Post-Spawn Delay)**:
   - Setelah menerima `OnSpawn` di world, bot menunggu jeda **2500ms** (2.5 detik) sebelum mengirimkan `action|wrench` untuk mengambil statistik profil. Hal ini mensimulasikan waktu pemuatan map dan reaksi pemain manusia.
3. **Pemisahan Fallback `/status`**:
   - Bot tidak mengirim `/status` dan `/stats` secara bersamaan dengan wrench. Jeda **4000ms** (4 detik) diterapkan setelah wrench. Perintah `/status` hanya dikirim jika statistik akun (`level` dan `played`) belum berhasil terisi dari dialog profil.
4. **Humanized Dialog Response**:
   - Saat menerima `OnDialogRequest` (tipe `personal_info`), bot menunda balasan `buttonClicked|personal_info` sebesar **600ms** dan mengirim tepat satu paket balasan.
5. **Cooldown Rate-Limiter pada `BotActions::say()`**:
   - Method `BotActions::say()` diproteksi oleh `std::mutex` dan `last_say_time_` dengan jeda minimal **1800ms** antar panggilan. Jika ada script atau fungsi yang memanggil `say()` terlalu cepat, eksekusi otomatis tidur (*sleep*) hingga batas waktu cooldown terpenuhi.
6. **Cooldown 3000ms pada Sinkronisasi Manual GUI**:
   - Tombol GUI `[🔄 Sinkron Stats (/status)]` memiliki guard cooldown 3000ms dengan pesan log informatif untuk mencegah spam klik dari pengguna.



