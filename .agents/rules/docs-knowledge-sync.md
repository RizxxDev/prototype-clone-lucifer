# Rules: Continuous Documentation & Knowledge Sync

**Mandatory Practice for Lucifer GTPS Bot Project**:
Setiap kali ada pengetahuan baru, investigasi reverse-engineering, perbaikan bug, penemuan struktur paket GTPS/Growtopia, perilaku server, fitur anti-cheat baru, atau API baru yang dipelajari atau diimplementasikan dalam proyek ini:

1. **WAJIB LANGSUNG DISIMPAN KE FOLDER `docs/`**:
   - Jangan hanya menyimpan informasi di chat atau memori sesi.
   - Perbarui atau buat file markdown yang relevan di direktori `docs/`:
     - **Protokol / Paket**: `docs/PROTOCOL.md` (format tank packet, variant types, sub-server redirection, keepalives, opcodes).
     - **Arsitektur / Threading**: `docs/ARCHITECTURE.md` (alur data, ENet loop, mutex, thread pool, memory management).
     - **Lua API & Scripting**: `docs/LUA_API.md` (metode baru, struktur data, hooks, script templates).
     - **Keamanan & Anti-Deteksi**: `docs/ANTI_DETECTION_AND_SECURITY.md` (fingerprint logic, timing, proxy SOCKS5, reconnect).
     - **Gotchas & Roadmap**: `docs/ROADMAP_AND_TODO.md` (quirks baru, tips debugging, langkah pengerjaan fitur selanjutnya).
     - **Index**: `docs/README.md` (tambahkan tautan jika membuat file baru di `docs/`).

2. **Format Standar**:
   - Bahasa: Indonesia yang jelas dan profesional dengan terminologi teknis baku / bilingual.
   - Menyertakan contoh kode / payload konkret jika berkaitan dengan paket atau algoritma.
   - Gunakan format link markdown GitHub yang valid (`file:///...`).
