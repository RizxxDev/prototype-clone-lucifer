#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <thread>

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
    bool udp_fallback_direct = false; // Matikan fallback direct agar IP asli tidak pernah bocor ke game server

    // Parse string format: "host:port", "host:port:user:pass", atau "socks5://user:pass@host:port"
    static Socks5Config parse(const std::string& str);
    std::string toString() const;
    bool isConfigured() const { return !host.empty() && port > 0 && port <= 65535; }
    bool isValid() const { return enabled && isConfigured(); }
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

    // Tes konektivitas proxy SOCKS5 (TCP + UDP Associate)
    static bool testProxy(const Socks5Config& config, std::string& out_msg);
    static bool testConnection(const Socks5Config& config, float& out_latency_ms, std::string& out_msg);

    // Buat socket TCP terhubung melalui SOCKS5 proxy ke target_host:target_port
    static SOCKET createConnectedSocket(const std::string& target_host,
                                        uint16_t target_port,
                                        const Socks5Config& config,
                                        int timeout_ms,
                                        std::string& out_err);

    // Buat TCP control socket yang terhubung dan dinegosiasikan untuk UDP ASSOCIATE
    static SOCKET createUdpAssociateControlSocket(const Socks5Config& config,
                                                 std::string& out_relay_ip,
                                                 uint16_t& out_relay_port,
                                                 int timeout_ms,
                                                 std::string& out_err);
};

// ============================================================
// Lucifer SOCKS5 UDP Tunnel (Loopback Relay Bridge)
// Mentranslasikan paket UDP ENet (raw datagrams) ke protokol
// SOCKS5 UDP Relay (RFC 1928 Section 7) secara transparan
// ============================================================
class Socks5UdpTunnel {
public:
    Socks5UdpTunnel();
    ~Socks5UdpTunnel();

    // Jalankan terowongan loopback UDP
    bool start(const Socks5Config& config,
               const std::string& target_host,
               uint16_t target_port,
               std::string& out_err);

    // Hentikan terowongan loopback
    void stop();

    bool isRunning() const { return running_; }
    uint16_t getLocalPort() const { return local_port_; }
    std::string getLocalHost() const { return "127.0.0.1"; }
    bool isProxyDisconnected() const { return proxy_disconnected_; }
    std::string getRelayIp() const { return relay_ip_; }
    uint16_t getRelayPort() const { return relay_port_; }

private:
    void relayLoop();

    std::atomic<bool> running_{false};
    std::atomic<bool> proxy_disconnected_{false};
    SOCKET tcp_ctrl_sock_ = INVALID_SOCKET;
    SOCKET local_sock_ = INVALID_SOCKET;
    SOCKET upstream_sock_ = INVALID_SOCKET;
    uint16_t local_port_ = 0;

    std::string target_host_;
    uint16_t target_port_ = 0;
    std::string relay_ip_;
    uint16_t relay_port_ = 0;

    sockaddr_in relay_addr_{};
    sockaddr_in client_enet_addr_{};
    bool has_client_ = false;
    uint8_t target_header_prefix_[10] = {0};

    std::thread relay_thread_;
};