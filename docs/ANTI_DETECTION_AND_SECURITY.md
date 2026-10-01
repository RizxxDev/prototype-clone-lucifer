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

Untuk kebutuhan multi-bot skala besar, bot mendukung penggunaan proxy **SOCKS5** individual per-akun.

### A. Alur Negosiasi SOCKS5 CONNECT (TCP):
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

### B. Format Konfigurasi Proxy
Format string proxy yang didukung:
1. `host:port` (contoh: `127.0.0.1:1080`)
2. `host:port:user:pass` (contoh: `192.168.1.1:1080:myuser:secret123`)
3. `socks5://user:pass@host:port`

### C. Pengujian Proxy di GUI
Tab **🛡️ Anti-Detection & Reconnect** menyediakan panel **SOCKS5 Proxy Tester**:
- Masukkan alamat IP, port, dan kredensial proxy.
- Klik **🔍 Uji Konektivitas Proxy SOCKS5**.
- Sistem akan menguji koneksi TCP, negosiasi handshake, dan menampilkan status beserta latensi (ping) dalam milidetik.
