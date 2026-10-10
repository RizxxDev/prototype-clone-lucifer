# Agent Guidelines & Project Knowledge Policy

## 📌 Aturan Utama: Sinkronisasi Pengetahuan ke `docs/`
Setiap kali ada pengetahuan baru yang ditemukan atau diimplementasikan dalam repositori ini:
- Temuan reverse-engineering paket & protokol GTPS
- Arsitektur sistem, threading, deadlock prevention
- API Lua baru atau perubahan signature method
- Mekanisme anti-deteksi, rate-limit bypass, proxy
- Solusi atas bug atau *gotchas* teknis

👉 **WAJIB LANGSUNG DIPERBARUI KE DALAM FOLDER [docs/](file:///c:/Users/RIZIQ/Documents/testtt/docs/README.md).**

Dokumentasi di folder `docs/` adalah *single source of truth* untuk proyek ini agar setiap sesi kerja dan pengembang dapat melanjutkan proyek tanpa kehilangan konteks.

<!-- CODEGRAPH_START -->
## CodeGraph

In repositories indexed by CodeGraph (a `.codegraph/` directory exists at the repo root), reach for it BEFORE grep/find or reading files when you need to understand or locate code:

- **MCP tool** (when available): `codegraph_explore` answers most code questions in one call — the relevant symbols' verbatim source plus the call paths between them, including dynamic-dispatch hops grep can't follow. Name a file or symbol in the query to read its current line-numbered source. If it's listed but deferred, load it by name via tool search.
- **Shell** (always works): `codegraph explore "<symbol names or question>"` prints the same output.

If there is no `.codegraph/` directory, skip CodeGraph entirely — indexing is the user's decision.
<!-- CODEGRAPH_END -->
