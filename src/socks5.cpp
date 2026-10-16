#include "socks5.hpp"
#include "logger.hpp"
#include <chrono>
#include <sstream>
#include <cstring>
#include <mswsock.h>

#pragma comment(lib, "ws2_32.lib")

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

static bool ensureWinsock() {
    static bool initialized = false;
    if (!initialized) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
            initialized = true;
        }
    }
    return initialized;
}

static void disableUdpConnReset(SOCKET s) {
    if (s == INVALID_SOCKET) return;
    BOOL bNewBehavior = FALSE;
    DWORD dwBytesReturned = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior),
             nullptr, 0, &dwBytesReturned, nullptr, nullptr);
}

static bool recvExact(SOCKET s, uint8_t* buf, int len) {
    int total = 0;
    while (total < len) {
        int r = recv(s, reinterpret_cast<char*>(buf + total), len - total, 0);
        if (r <= 0) return false;
        total += r;
    }
    return true;
}

// Periksa apakah IPv4 (dalam urutan byte jaringan / big-endian) adalah IP kosong, loopback, atau private NAT
static bool isInternalOrZeroIp(const uint8_t ip[4]) {
    if (ip[0] == 0) return true;                                     // 0.0.0.0/8
    if (ip[0] == 127) return true;                                   // 127.0.0.0/8 (Loopback)
    if (ip[0] == 10) return true;                                    // 10.0.0.0/8 (Private)
    if (ip[0] == 172 && (ip[1] >= 16 && ip[1] <= 31)) return true;   // 172.16.0.0/12 (Private)
    if (ip[0] == 192 && ip[1] == 168) return true;                   // 192.168.0.0/16 (Private)
    if (ip[0] == 100 && (ip[1] >= 64 && ip[1] <= 127)) return true;  // 100.64.0.0/10 (CGNAT)
    return false;
}

static void trimStr(std::string& s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
}

static bool isAllDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

Socks5Config Socks5Config::parse(const std::string& str) {
    Socks5Config cfg;
    if (str.empty()) return cfg;

    std::string s = str;
    trimStr(s);

    // Hapus prefix protokol umum jika ada
    if (s.rfind("socks5h://", 0) == 0) s = s.substr(10);
    else if (s.rfind("socks5://", 0) == 0) s = s.substr(9);
    else if (s.rfind("socks4://", 0) == 0) s = s.substr(9);
    else if (s.rfind("http://", 0) == 0) s = s.substr(7);
    else if (s.rfind("https://", 0) == 0) s = s.substr(8);
    trimStr(s);

    // Cek format user:pass@host:port
    size_t at_pos = s.find('@');
    if (at_pos != std::string::npos) {
        std::string auth_part = s.substr(0, at_pos);
        std::string host_part = s.substr(at_pos + 1);

        size_t colon_auth = auth_part.find(':');
        if (colon_auth != std::string::npos) {
            cfg.username = auth_part.substr(0, colon_auth);
            cfg.password = auth_part.substr(colon_auth + 1);
        } else {
            cfg.username = auth_part;
        }

        size_t colon_host = host_part.find(':');
        if (colon_host != std::string::npos) {
            cfg.host = host_part.substr(0, colon_host);
            try { cfg.port = std::stoi(host_part.substr(colon_host + 1)); } catch (...) {}
        } else {
            cfg.host = host_part;
        }
        trimStr(cfg.host);
        trimStr(cfg.username);
        trimStr(cfg.password);
        cfg.enabled = cfg.isConfigured();
        return cfg;
    }

    // Cek format colon-separated: host:port, host:port:user:pass, atau user:pass:host:port
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ':')) {
        trimStr(item);
        parts.push_back(item);
    }

    if (parts.size() >= 2) {
        if (parts.size() >= 4 && !isAllDigits(parts[1]) && isAllDigits(parts[3])) {
            // Format: user:pass:host:port
            cfg.username = parts[0];
            cfg.password = parts[1];
            cfg.host = parts[2];
            try { cfg.port = std::stoi(parts[3]); } catch (...) {}
        } else {
            // Format standar: host:port[:user:pass]
            cfg.host = parts[0];
            try { cfg.port = std::stoi(parts[1]); } catch (...) {}
            if (parts.size() >= 4) {
                cfg.username = parts[2];
                cfg.password = parts[3];
            }
        }
        trimStr(cfg.host);
        trimStr(cfg.username);
        trimStr(cfg.password);
        cfg.enabled = cfg.isConfigured();
    }

    return cfg;
}

std::string Socks5Config::toString() const {
    if (!enabled || host.empty()) return "Disabled";
    std::string s = host + ":" + std::to_string(port);
    if (!username.empty()) {
        s += " (" + username + ")";
    }
    return s;
}

bool Socks5Client::performConnect(SOCKET sock,
                                  const std::string& target_host,
                                  uint16_t target_port,
                                  const Socks5Config& config,
                                  std::string& out_err) {
    // 1. Method Selection
    std::vector<uint8_t> req;
    bool has_auth = !config.username.empty();
    if (has_auth) {
        req = {0x05, 0x02, 0x00, 0x02}; // 0x00=NoAuth, 0x02=UserPass
    } else {
        req = {0x05, 0x01, 0x00}; // 0x00=NoAuth
    }

    if (send(sock, reinterpret_cast<const char*>(req.data()), static_cast<int>(req.size()), 0) <= 0) {
        out_err = "Gagal kirim negosiasi method SOCKS5";
        return false;
    }

    uint8_t resp[2] = {0};
    if (!recvExact(sock, resp, 2)) {
        out_err = "Gagal terima respons method SOCKS5";
        return false;
    }

    if (resp[0] != 0x05) {
        out_err = "Bukan server SOCKS5 yang valid";
        return false;
    }

    if (resp[1] == 0xFF) {
        out_err = "Server SOCKS5 menolak metode autentikasi yang disediakan";
        return false;
    }

    // 2. Autentikasi Username/Password (RFC 1929) jika dipilih
    if (resp[1] == 0x02) {
        if (!has_auth) {
            out_err = "Server meminta autentikasi username/password tetapi belum diatur";
            return false;
        }

        std::vector<uint8_t> auth_pkt;
        auth_pkt.push_back(0x01); // Auth version
        auth_pkt.push_back(static_cast<uint8_t>(config.username.size()));
        auth_pkt.insert(auth_pkt.end(), config.username.begin(), config.username.end());
        auth_pkt.push_back(static_cast<uint8_t>(config.password.size()));
        auth_pkt.insert(auth_pkt.end(), config.password.begin(), config.password.end());

        if (send(sock, reinterpret_cast<const char*>(auth_pkt.data()), static_cast<int>(auth_pkt.size()), 0) <= 0) {
            out_err = "Gagal kirim kredensial SOCKS5";
            return false;
        }

        uint8_t auth_resp[2] = {0};
        if (!recvExact(sock, auth_resp, 2)) {
            out_err = "Gagal terima respons auth SOCKS5";
            return false;
        }

        if (auth_resp[1] != 0x00) {
            out_err = "Autentikasi SOCKS5 ditolak (Username / Password salah)";
            return false;
        }
    }

    // 3. Request CONNECT (CMD 0x01)
    std::vector<uint8_t> conn_req;
    conn_req.push_back(0x05); // SOCKS5
    conn_req.push_back(0x01); // CMD = CONNECT
    conn_req.push_back(0x00); // RSV

    // Periksa apakah target_host berupa IPv4 atau Domain
    in_addr addr4;
    if (inet_pton(AF_INET, target_host.c_str(), &addr4) == 1) {
        conn_req.push_back(0x01); // ATYP = IPv4
        uint8_t* p = reinterpret_cast<uint8_t*>(&addr4.s_addr);
        conn_req.insert(conn_req.end(), p, p + 4);
    } else {
        conn_req.push_back(0x03); // ATYP = DOMAINNAME
        conn_req.push_back(static_cast<uint8_t>(target_host.size()));
        conn_req.insert(conn_req.end(), target_host.begin(), target_host.end());
    }

    // Port (2 bytes, big-endian)
    conn_req.push_back(static_cast<uint8_t>((target_port >> 8) & 0xFF));
    conn_req.push_back(static_cast<uint8_t>(target_port & 0xFF));

    if (send(sock, reinterpret_cast<const char*>(conn_req.data()), static_cast<int>(conn_req.size()), 0) <= 0) {
        out_err = "Gagal kirim permintaan CONNECT SOCKS5";
        return false;
    }

    // Baca respons (minimal 4 bytes header)
    uint8_t conn_resp[4] = {0};
    if (!recvExact(sock, conn_resp, 4)) {
        out_err = "Gagal terima respons CONNECT SOCKS5";
        return false;
    }

    if (conn_resp[1] != 0x00) {
        std::ostringstream oss;
        oss << "SOCKS5 CONNECT gagal dengan kode kesalahan: 0x" << std::hex << (int)conn_resp[1];
        switch (conn_resp[1]) {
            case 0x01: oss << " (General SOCKS server failure)"; break;
            case 0x02: oss << " (Connection not allowed by ruleset)"; break;
            case 0x03: oss << " (Network unreachable)"; break;
            case 0x04: oss << " (Host unreachable)"; break;
            case 0x05: oss << " (Connection refused by destination)"; break;
            case 0x06: oss << " (TTL expired)"; break;
            case 0x07: oss << " (Command not supported)"; break;
            case 0x08: oss << " (Address type not supported)"; break;
        }
        out_err = oss.str();
        return false;
    }

    // Buang bound address & port dari buffer respons
    uint8_t atyp = conn_resp[3];
    int skip_bytes = 0;
    if (atyp == 0x01) {
        skip_bytes = 4 + 2; // IPv4 (4) + Port (2)
    } else if (atyp == 0x03) {
        uint8_t dlen = 0;
        if (!recvExact(sock, &dlen, 1)) return false;
        skip_bytes = dlen + 2; // Domain + Port (2)
    } else if (atyp == 0x04) {
        skip_bytes = 16 + 2; // IPv6 (16) + Port (2)
    }

    if (skip_bytes > 0) {
        std::vector<uint8_t> dummy(skip_bytes);
        recvExact(sock, dummy.data(), skip_bytes);
    }

    return true;
}

bool Socks5Client::performUdpAssociate(SOCKET tcp_ctrl_sock,
                                      const Socks5Config& config,
                                      std::string& out_relay_ip,
                                      uint16_t& out_relay_port,
                                      std::string& out_err) {
    // 1. Method Selection
    std::vector<uint8_t> req;
    bool has_auth = !config.username.empty();
    if (has_auth) {
        req = {0x05, 0x02, 0x00, 0x02};
    } else {
        req = {0x05, 0x01, 0x00};
    }

    if (send(tcp_ctrl_sock, reinterpret_cast<const char*>(req.data()), static_cast<int>(req.size()), 0) <= 0) {
        out_err = "Gagal kirim negosiasi method SOCKS5 UDP";
        return false;
    }

    uint8_t resp[2] = {0};
    if (!recvExact(tcp_ctrl_sock, resp, 2) || resp[0] != 0x05 || resp[1] == 0xFF) {
        out_err = "Negosiasi method SOCKS5 UDP ditolak";
        return false;
    }

    if (resp[1] == 0x02) {
        if (!has_auth) {
            out_err = "Server proxy meminta username/password untuk UDP Associate";
            return false;
        }
        std::vector<uint8_t> auth_pkt;
        auth_pkt.push_back(0x01);
        auth_pkt.push_back(static_cast<uint8_t>(config.username.size()));
        auth_pkt.insert(auth_pkt.end(), config.username.begin(), config.username.end());
        auth_pkt.push_back(static_cast<uint8_t>(config.password.size()));
        auth_pkt.insert(auth_pkt.end(), config.password.begin(), config.password.end());

        if (send(tcp_ctrl_sock, reinterpret_cast<const char*>(auth_pkt.data()), static_cast<int>(auth_pkt.size()), 0) <= 0) {
            out_err = "Gagal kirim paket auth SOCKS5 UDP";
            return false;
        }
        uint8_t auth_resp[2] = {0};
        if (!recvExact(tcp_ctrl_sock, auth_resp, 2) || auth_resp[1] != 0x00) {
            out_err = "Auth SOCKS5 UDP ditolak (Username/Password salah)";
            return false;
        }
    }

    // 2. Kirim UDP ASSOCIATE (CMD 0x03) dengan DST.ADDR = 0.0.0.0:0
    uint8_t udp_req[10] = {0x05, 0x03, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
    if (send(tcp_ctrl_sock, reinterpret_cast<const char*>(udp_req), 10, 0) <= 0) {
        out_err = "Gagal kirim permintaan UDP ASSOCIATE";
        return false;
    }

    uint8_t udp_resp[4] = {0};
    if (!recvExact(tcp_ctrl_sock, udp_resp, 4) || udp_resp[1] != 0x00) {
        uint8_t rep = udp_resp[1];
        if (rep == 0x07) {
            out_err = "Proxy ini adalah TCP-Only dan TIDAK mendukung UDP ASSOCIATE (Kode 0x07)";
        } else if (rep == 0x02) {
            out_err = "Server SOCKS5 memblokir UDP (Not Allowed by Ruleset)";
        } else if (rep == 0x04) {
            out_err = "Server SOCKS5: Host Unreachable";
        } else if (rep == 0x05) {
            out_err = "Server SOCKS5: Connection Refused";
        } else {
            out_err = "Server SOCKS5 menolak UDP ASSOCIATE (Kode: " + std::to_string(rep) + ")";
        }
        return false;
    }

    uint8_t atyp = udp_resp[3];
    if (atyp == 0x01) { // IPv4
        uint8_t ip_bytes[4] = {0};
        uint8_t port_bytes[2] = {0};
        if (!recvExact(tcp_ctrl_sock, ip_bytes, 4) || !recvExact(tcp_ctrl_sock, port_bytes, 2)) {
            out_err = "Gagal membaca alamat relay IPv4 dari respons UDP ASSOCIATE";
            return false;
        }

        char ip_str[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, ip_bytes, ip_str, sizeof(ip_str));
        out_relay_ip = ip_str;

        // Periksa apakah proxy host yang dikonfigurasi user adalah IP publik (bukan localhost/private)
        uint8_t cfg_ip[4] = {0};
        bool cfg_is_private = false;
        if (inet_pton(AF_INET, config.host.c_str(), cfg_ip) == 1) {
            cfg_is_private = isInternalOrZeroIp(cfg_ip);
        }

        // Banyak server SOCKS5 di VPS/Cloud mengembalikan IP internal (0.0.0.0, 127.0.0.1, 10.x.x.x, 192.168.x.x)
        // pada field BND.ADDR. Jika demikian, arahkan paket UDP ke IP publik proxy (config.host).
        if (isInternalOrZeroIp(ip_bytes) && !cfg_is_private) {
            Logger::log("[SOCKS5] Relay mengembalikan BND.ADDR internal (", out_relay_ip,
                        "), mengalihkan tujuan UDP ke host proxy publik: ", config.host);
            out_relay_ip = config.host;
        }

        out_relay_port = static_cast<uint16_t>((port_bytes[0] << 8) | port_bytes[1]);
        if (out_relay_port == 0) {
            out_relay_port = static_cast<uint16_t>(config.port);
        }
        return true;
    } else if (atyp == 0x03) { // Domain name
        uint8_t dlen = 0;
        if (!recvExact(tcp_ctrl_sock, &dlen, 1)) return false;
        std::vector<uint8_t> dname(dlen + 1, 0);
        if (!recvExact(tcp_ctrl_sock, dname.data(), dlen)) return false;
        uint8_t port_bytes[2] = {0};
        if (!recvExact(tcp_ctrl_sock, port_bytes, 2)) return false;
        out_relay_ip = reinterpret_cast<char*>(dname.data());
        if (out_relay_ip.empty() || out_relay_ip == "0.0.0.0" || out_relay_ip == "localhost") {
            out_relay_ip = config.host;
        }
        out_relay_port = static_cast<uint16_t>((port_bytes[0] << 8) | port_bytes[1]);
        if (out_relay_port == 0) {
            out_relay_port = static_cast<uint16_t>(config.port);
        }
        return true;
    } else if (atyp == 0x04) { // IPv6 (16 bytes IP + 2 bytes port)
        uint8_t ip6_bytes[16] = {0};
        uint8_t port_bytes[2] = {0};
        if (!recvExact(tcp_ctrl_sock, ip6_bytes, 16) || !recvExact(tcp_ctrl_sock, port_bytes, 2)) {
            out_err = "Gagal membaca alamat relay IPv6 dari respons UDP ASSOCIATE";
            return false;
        }
        // Gunakan IPv4/hostname proxy untuk pengiriman paket UDP IPv4
        out_relay_ip = config.host;
        out_relay_port = static_cast<uint16_t>((port_bytes[0] << 8) | port_bytes[1]);
        if (out_relay_port == 0) {
            out_relay_port = static_cast<uint16_t>(config.port);
        }
        return true;
    }

    out_err = "Relay address type tidak dikenal: " + std::to_string(atyp);
    return false;
}

SOCKET Socks5Client::createConnectedSocket(const std::string& target_host,
                                           uint16_t target_port,
                                           const Socks5Config& config,
                                           int timeout_ms,
                                           std::string& out_err) {
    if (!ensureWinsock()) {
        out_err = "WSAStartup gagal";
        return INVALID_SOCKET;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        out_err = "Gagal buat TCP socket";
        return INVALID_SOCKET;
    }

    DWORD tv = static_cast<DWORD>(timeout_ms);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    // Resolve alamat proxy
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(config.port);

    if (getaddrinfo(config.host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        closesocket(sock);
        out_err = "Gagal resolve hostname proxy: " + config.host;
        return INVALID_SOCKET;
    }

    if (connect(sock, res->ai_addr, static_cast<int>(res->ai_addrlen)) != 0) {
        freeaddrinfo(res);
        closesocket(sock);
        out_err = "Gagal terhubung ke proxy SOCKS5 " + config.host + ":" + port_str;
        return INVALID_SOCKET;
    }
    freeaddrinfo(res);

    if (!performConnect(sock, target_host, target_port, config, out_err)) {
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

SOCKET Socks5Client::createUdpAssociateControlSocket(const Socks5Config& config,
                                                     std::string& out_relay_ip,
                                                     uint16_t& out_relay_port,
                                                     int timeout_ms,
                                                     std::string& out_err) {
    if (!ensureWinsock()) {
        out_err = "WSAStartup gagal";
        return INVALID_SOCKET;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        out_err = "Gagal buat TCP control socket";
        return INVALID_SOCKET;
    }

    DWORD tv = static_cast<DWORD>(timeout_ms);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    // Aktifkan TCP Keep-Alive agar koneksi kontrol UDP ASSOCIATE tidak diputus karena idle
    BOOL keepalive = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&keepalive), sizeof(keepalive));

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(config.port);

    if (getaddrinfo(config.host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        closesocket(sock);
        out_err = "Gagal resolve hostname proxy: " + config.host;
        return INVALID_SOCKET;
    }

    if (connect(sock, res->ai_addr, static_cast<int>(res->ai_addrlen)) != 0) {
        freeaddrinfo(res);
        closesocket(sock);
        out_err = "Gagal terhubung ke proxy SOCKS5 " + config.host + ":" + port_str;
        return INVALID_SOCKET;
    }
    freeaddrinfo(res);

    if (!performUdpAssociate(sock, config, out_relay_ip, out_relay_port, out_err)) {
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

bool Socks5Client::testProxy(const Socks5Config& config, std::string& out_msg) {
    if (!config.isValid()) {
        out_msg = "Konfigurasi SOCKS5 tidak valid (Host atau Port kosong)";
        return false;
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    std::string err;
    SOCKET sock = createConnectedSocket("1.1.1.1", 80, config, 4000, err);
    if (sock == INVALID_SOCKET) {
        out_msg = "❌ Gagal TCP: " + err;
        return false;
    }
    closesocket(sock);

    // Uji apakah proxy juga mendukung UDP ASSOCIATE (wajib untuk ENet Growtopia)
    std::string relay_ip;
    uint16_t relay_port = 0;
    std::string udp_err;
    SOCKET udp_ctrl = createUdpAssociateControlSocket(config, relay_ip, relay_port, 4000, udp_err);
    auto end_time = std::chrono::high_resolution_clock::now();
    auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    if (udp_ctrl == INVALID_SOCKET) {
        out_msg = "⚠️ TCP OK (" + std::to_string(latency_ms) + "ms), tapi UDP GAGAL: " + udp_err;
        return false;
    }
    closesocket(udp_ctrl);

    out_msg = "✅ SOCKS5 TCP+UDP Aktif! Relay: " + relay_ip + ":" + std::to_string(relay_port) + " (" + std::to_string(latency_ms) + " ms)";
    return true;
}

bool Socks5Client::testConnection(const Socks5Config& config, float& out_latency_ms, std::string& out_msg) {
    if (!config.isValid()) {
        out_msg = "Konfigurasi SOCKS5 tidak valid (Host atau Port kosong)";
        out_latency_ms = -1.0f;
        return false;
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    std::string err;
    SOCKET sock = createConnectedSocket("1.1.1.1", 80, config, 4000, err);
    if (sock == INVALID_SOCKET) {
        out_msg = "❌ Gagal: " + err;
        out_latency_ms = -1.0f;
        return false;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    out_latency_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
    closesocket(sock);

    out_msg = "✅ SOCKS5 Terhubung! Latensi: " + std::to_string(static_cast<int>(out_latency_ms)) + " ms";
    return true;
}

// ============================================================
// Socks5UdpTunnel Implementation
// ============================================================

Socks5UdpTunnel::Socks5UdpTunnel() = default;

Socks5UdpTunnel::~Socks5UdpTunnel() {
    stop();
}

bool Socks5UdpTunnel::start(const Socks5Config& config,
                           const std::string& target_host,
                           uint16_t target_port,
                           std::string& out_err) {
    stop();

    if (!config.isValid()) {
        out_err = "Konfigurasi SOCKS5 tidak valid";
        return false;
    }
    if (target_host.empty() || target_port == 0) {
        out_err = "Target host atau port game tidak valid";
        return false;
    }

    target_host_ = target_host;
    target_port_ = target_port;

    // 1. Buat koneksi TCP Control & negosiasikan UDP ASSOCIATE
    tcp_ctrl_sock_ = Socks5Client::createUdpAssociateControlSocket(config, relay_ip_, relay_port_, 6000, out_err);
    if (tcp_ctrl_sock_ == INVALID_SOCKET) {
        return false;
    }

    // 2. Siapkan prefix 10 byte SOCKS5 UDP request header untuk target server GTPS:
    // [0x00, 0x00, 0x00, 0x01, Target_IPv4 (4 byte), Target_Port (2 byte)]
    target_header_prefix_[0] = 0x00; // RSV
    target_header_prefix_[1] = 0x00; // RSV
    target_header_prefix_[2] = 0x00; // FRAG
    target_header_prefix_[3] = 0x01; // ATYP = IPv4

    in_addr t_addr{};
    if (inet_pton(AF_INET, target_host_.c_str(), &t_addr) == 1) {
        std::memcpy(&target_header_prefix_[4], &t_addr.s_addr, 4);
    } else {
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(target_host_.c_str(), nullptr, &hints, &res) == 0 && res) {
            sockaddr_in* s = reinterpret_cast<sockaddr_in*>(res->ai_addr);
            std::memcpy(&target_header_prefix_[4], &s->sin_addr.s_addr, 4);
            freeaddrinfo(res);
        } else {
            stop();
            out_err = "Gagal resolve target host game: " + target_host_;
            return false;
        }
    }
    target_header_prefix_[8] = static_cast<uint8_t>((target_port_ >> 8) & 0xFF);
    target_header_prefix_[9] = static_cast<uint8_t>(target_port_ & 0xFF);

    // 3. Resolve alamat Relay Proxy
    std::memset(&relay_addr_, 0, sizeof(relay_addr_));
    relay_addr_.sin_family = AF_INET;
    relay_addr_.sin_port = htons(relay_port_);
    if (inet_pton(AF_INET, relay_ip_.c_str(), &relay_addr_.sin_addr) != 1) {
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(relay_ip_.c_str(), nullptr, &hints, &res) == 0 && res) {
            sockaddr_in* s = reinterpret_cast<sockaddr_in*>(res->ai_addr);
            relay_addr_.sin_addr = s->sin_addr;
            freeaddrinfo(res);
        } else {
            stop();
            out_err = "Gagal resolve alamat SOCKS5 UDP relay: " + relay_ip_;
            return false;
        }
    }

    // 4. Buat socket UDP lokal (loopback 127.0.0.1) untuk komunikasi dengan ENet client
    local_sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (local_sock_ == INVALID_SOCKET) {
        stop();
        out_err = "Gagal membuat socket UDP lokal";
        return false;
    }
    disableUdpConnReset(local_sock_);

    sockaddr_in local_bind{};
    local_bind.sin_family = AF_INET;
    local_bind.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local_bind.sin_port = 0; // Port dinamis dari OS
    if (bind(local_sock_, reinterpret_cast<sockaddr*>(&local_bind), sizeof(local_bind)) != 0) {
        stop();
        out_err = "Gagal bind socket UDP loopback";
        return false;
    }

    int slen = sizeof(local_bind);
    getsockname(local_sock_, reinterpret_cast<sockaddr*>(&local_bind), &slen);
    local_port_ = ntohs(local_bind.sin_port);

    // 5. Buat socket UDP upstream untuk mengirim/menerima paket dari proxy
    upstream_sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (upstream_sock_ == INVALID_SOCKET) {
        stop();
        out_err = "Gagal membuat socket UDP upstream";
        return false;
    }
    disableUdpConnReset(upstream_sock_);

    sockaddr_in up_bind{};
    up_bind.sin_family = AF_INET;
    up_bind.sin_addr.s_addr = htonl(INADDR_ANY);
    up_bind.sin_port = 0;
    bind(upstream_sock_, reinterpret_cast<sockaddr*>(&up_bind), sizeof(up_bind));

    // 6. Jalankan thread relay
    running_ = true;
    proxy_disconnected_ = false;
    has_client_ = false;
    relay_thread_ = std::thread(&Socks5UdpTunnel::relayLoop, this);

    Logger::log("[SOCKS5] 🚀 Terowongan UDP Relay aktif: 127.0.0.1:", local_port_,
                " <==> Relay ", relay_ip_, ":", relay_port_, " <==> Target ", target_host_, ":", target_port_);
    return true;
}

void Socks5UdpTunnel::stop() {
    running_ = false;

    if (local_sock_ != INVALID_SOCKET) {
        closesocket(local_sock_);
        local_sock_ = INVALID_SOCKET;
    }
    if (upstream_sock_ != INVALID_SOCKET) {
        closesocket(upstream_sock_);
        upstream_sock_ = INVALID_SOCKET;
    }
    if (tcp_ctrl_sock_ != INVALID_SOCKET) {
        closesocket(tcp_ctrl_sock_);
        tcp_ctrl_sock_ = INVALID_SOCKET;
    }

    if (relay_thread_.joinable()) {
        relay_thread_.join();
    }

    local_port_ = 0;
    has_client_ = false;
    proxy_disconnected_ = false;
}

void Socks5UdpTunnel::relayLoop() {
    std::vector<uint8_t> in_buf(65536);
    std::vector<uint8_t> out_buf(65536);

    while (running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        if (local_sock_ != INVALID_SOCKET) FD_SET(local_sock_, &read_fds);
        if (upstream_sock_ != INVALID_SOCKET) FD_SET(upstream_sock_, &read_fds);
        if (tcp_ctrl_sock_ != INVALID_SOCKET) FD_SET(tcp_ctrl_sock_, &read_fds);

        timeval tv{0, 10000}; // 10ms
        int ret = select(0, &read_fds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (!running_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if (ret == 0) {
            continue;
        }

        // Cek TCP Control Socket
        if (tcp_ctrl_sock_ != INVALID_SOCKET && FD_ISSET(tcp_ctrl_sock_, &read_fds)) {
            char peek_buf;
            int pr = recv(tcp_ctrl_sock_, &peek_buf, 1, MSG_PEEK);
            if (pr == 0 || (pr < 0 && WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAETIMEDOUT)) {
                proxy_disconnected_ = true;
                Logger::log("[SOCKS5] ⚠️ Koneksi TCP control proxy SOCKS5 terputus");
                break;
            }
        }

        // 1. Datagram dari ENet Client (127.0.0.1) -> Bungkus header SOCKS5 -> Kirim ke Relay
        if (local_sock_ != INVALID_SOCKET && FD_ISSET(local_sock_, &read_fds)) {
            sockaddr_in from_addr{};
            int from_len = sizeof(from_addr);
            int n = recvfrom(local_sock_, reinterpret_cast<char*>(in_buf.data()), static_cast<int>(in_buf.size()), 0,
                             reinterpret_cast<sockaddr*>(&from_addr), &from_len);
            if (n > 0) {
                client_enet_addr_ = from_addr;
                has_client_ = true;

                // Bungkus paket dengan prefix header 10 byte
                std::memcpy(out_buf.data(), target_header_prefix_, 10);
                std::memcpy(out_buf.data() + 10, in_buf.data(), n);

                sendto(upstream_sock_, reinterpret_cast<const char*>(out_buf.data()), n + 10, 0,
                       reinterpret_cast<sockaddr*>(&relay_addr_), sizeof(relay_addr_));
            }
        }

        // 2. Datagram dari Relay Proxy SOCKS5 -> Lepas header SOCKS5 -> Kirim ke ENet Client
        if (upstream_sock_ != INVALID_SOCKET && FD_ISSET(upstream_sock_, &read_fds)) {
            int n = recvfrom(upstream_sock_, reinterpret_cast<char*>(in_buf.data()), static_cast<int>(in_buf.size()), 0,
                             nullptr, nullptr);
            if (n >= 10 && has_client_) {
                // Pastikan FRAG == 0x00 sesuai RFC 1928
                if (in_buf[2] != 0x00) continue;

                int header_len = 10;
                uint8_t atyp = in_buf[3];
                if (atyp == 0x01) { // IPv4
                    header_len = 10;
                } else if (atyp == 0x03) { // Domain
                    uint8_t dlen = in_buf[4];
                    header_len = 4 + 1 + dlen + 2;
                } else if (atyp == 0x04) { // IPv6
                    header_len = 22;
                }

                if (n > header_len && local_sock_ != INVALID_SOCKET) {
                    sendto(local_sock_, reinterpret_cast<const char*>(in_buf.data() + header_len),
                           n - header_len, 0,
                           reinterpret_cast<sockaddr*>(&client_enet_addr_), sizeof(client_enet_addr_));
                }
            }
        }
    }
}