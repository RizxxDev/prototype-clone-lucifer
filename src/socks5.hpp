#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <cstdint>

// ============================================================
// Lucifer SOCKS5 Proxy Client Engine (RFC 1928 & RFC 1929)
// Supports No-Auth, Username/Password Auth, IPv4 and Domain Name
// ============================================================

struct Socks5Config {
    bool enabled = false;
    std::string host;
    int port = 1080;
    std::string username;
    std::string password;

    // Parse string format: "host:port", "host:port:user:pass", atau "socks5://user:pass@host:port"
    static Socks5Config parse(const std::string& str);
    std::string toString() const;
    bool isValid() const { return enabled && !host.empty() && port > 0 && port <= 65535; }
};

class Socks5Client {
public:
    // Melakukan negosiasi SOCKS5 CONNECT (TCP Tunneling) pada socket yang sudah terhubung ke proxy
    static bool performConnect(SOCKET sock,
                               const std::string& target_host,
                               uint16_t target_port,
                               const Socks5Config& config,
                               std::string& out_err);

    // Melakukan negosiasi SOCKS5 UDP ASSOCIATE (untuk tunneling paket UDP)
    static bool performUdpAssociate(SOCKET tcp_ctrl_sock,
                                   const Socks5Config& config,
                                   std::string& out_relay_ip,
                                   uint16_t& out_relay_port,
                                   std::string& out_err);

    // Tes konektivitas proxy SOCKS5
    static bool testProxy(const Socks5Config& config, std::string& out_msg);

    // Buat socket TCP terhubung melalui SOCKS5 proxy ke target_host:target_port
    static SOCKET createConnectedSocket(const std::string& target_host,
                                        uint16_t target_port,
                                        const Socks5Config& config,
                                        int timeout_ms,
                                        std::string& out_err);
};
