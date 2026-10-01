#include "socks5.hpp"
#include "logger.hpp"
#include <chrono>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

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

Socks5Config Socks5Config::parse(const std::string& str) {
    Socks5Config cfg;
    if (str.empty()) return cfg;

    std::string s = str;
    // Hapus prefix socks5:// jika ada
    if (s.rfind("socks5://", 0) == 0) {
        s = s.substr(9);
    }

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
        cfg.enabled = true;
        return cfg;
    }

    // Cek format colon-separated: host:port atau host:port:user:pass
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ':')) {
        parts.push_back(item);
    }

    if (parts.size() >= 2) {
        cfg.host = parts[0];
        try { cfg.port = std::stoi(parts[1]); } catch (...) {}
        if (parts.size() >= 4) {
            cfg.username = parts[2];
            cfg.password = parts[3];
        }
        cfg.enabled = true;
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
    if (recv(sock, reinterpret_cast<char*>(resp), 2, 0) != 2) {
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
        if (recv(sock, reinterpret_cast<char*>(auth_resp), 2, 0) != 2) {
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
    if (recv(sock, reinterpret_cast<char*>(conn_resp), 4, 0) != 4) {
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
        recv(sock, reinterpret_cast<char*>(&dlen), 1, 0);
        skip_bytes = dlen + 2; // Domain + Port (2)
    } else if (atyp == 0x04) {
        skip_bytes = 16 + 2; // IPv6 (16) + Port (2)
    }

    if (skip_bytes > 0) {
        std::vector<char> dummy(skip_bytes);
        recv(sock, dummy.data(), skip_bytes, 0);
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
    if (recv(tcp_ctrl_sock, reinterpret_cast<char*>(resp), 2, 0) != 2 || resp[0] != 0x05 || resp[1] == 0xFF) {
        out_err = "Negosiasi method SOCKS5 UDP ditolak";
        return false;
    }

    if (resp[1] == 0x02) {
        std::vector<uint8_t> auth_pkt;
        auth_pkt.push_back(0x01);
        auth_pkt.push_back(static_cast<uint8_t>(config.username.size()));
        auth_pkt.insert(auth_pkt.end(), config.username.begin(), config.username.end());
        auth_pkt.push_back(static_cast<uint8_t>(config.password.size()));
        auth_pkt.insert(auth_pkt.end(), config.password.begin(), config.password.end());

        send(tcp_ctrl_sock, reinterpret_cast<const char*>(auth_pkt.data()), static_cast<int>(auth_pkt.size()), 0);
        uint8_t auth_resp[2] = {0};
        if (recv(tcp_ctrl_sock, reinterpret_cast<char*>(auth_resp), 2, 0) != 2 || auth_resp[1] != 0x00) {
            out_err = "Auth SOCKS5 UDP ditolak";
            return false;
        }
    }

    // 2. Kirim UDP ASSOCIATE (CMD 0x03)
    uint8_t udp_req[10] = {0x05, 0x03, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
    if (send(tcp_ctrl_sock, reinterpret_cast<const char*>(udp_req), 10, 0) <= 0) {
        out_err = "Gagal kirim permintaan UDP ASSOCIATE";
        return false;
    }

    uint8_t udp_resp[4] = {0};
    if (recv(tcp_ctrl_sock, reinterpret_cast<char*>(udp_resp), 4, 0) != 4 || udp_resp[1] != 0x00) {
        out_err = "Server SOCKS5 menolak UDP ASSOCIATE";
        return false;
    }

    uint8_t atyp = udp_resp[3];
    if (atyp == 0x01) { // IPv4
        uint8_t ip_bytes[4];
        uint8_t port_bytes[2];
        recv(tcp_ctrl_sock, reinterpret_cast<char*>(ip_bytes), 4, 0);
        recv(tcp_ctrl_sock, reinterpret_cast<char*>(port_bytes), 2, 0);

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, ip_bytes, ip_str, sizeof(ip_str));
        out_relay_ip = ip_str;
        if (out_relay_ip == "0.0.0.0") {
            out_relay_ip = config.host; // fallback ke host proxy
        }
        out_relay_port = (port_bytes[0] << 8) | port_bytes[1];
        return true;
    }

    out_err = "Relay address type selain IPv4 belum didukung";
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

bool Socks5Client::testProxy(const Socks5Config& config, std::string& out_msg) {
    if (!config.isValid()) {
        out_msg = "Konfigurasi SOCKS5 tidak valid (Host atau Port kosong)";
        return false;
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    std::string err;
    // Coba hubungkan ke DNS Google 8.8.8.8 port 53 atau website umum via SOCKS5
    SOCKET sock = createConnectedSocket("1.1.1.1", 80, config, 4000, err);
    if (sock == INVALID_SOCKET) {
        out_msg = "❌ Gagal: " + err;
        return false;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    closesocket(sock);

    out_msg = "✅ SOCKS5 Terhubung! Latensi: " + std::to_string(latency_ms) + " ms";
    return true;
}
