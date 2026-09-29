#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <atomic>
#include <thread>

#include <enet/enet.h>
#include <mutex>

struct GtPacketData {
    uint32_t type;
    std::vector<uint8_t> payload;
    bool outgoing; // true = C->S, false = S->C
};

using PacketCallback = std::function<void(const GtPacketData&)>;

class EnetClient {
public:
    EnetClient();
    ~EnetClient();

    // Connect ke server GTPS
    bool connect(const std::string& host, int port,
                 int timeout_ms = 5000);

    // Disconnect
    void disconnect();

    // Kirim paket Growtopia: [uint32 type][payload]
    bool send(uint32_t type, const std::vector<uint8_t>& payload);

    // Kirim paket text (untuk chat / login)
    bool sendText(uint32_t type, const std::string& text);

    // Poll events sekali (non-blocking, timeout 10ms)
    void poll();

    // Set callback untuk paket masuk
    void onPacket(PacketCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = cb;
    }

    bool isConnected() const { return connected_; }

    // Getter
    ENetHost* host() { return host_; }
    ENetPeer* peer() { return peer_; }
    std::string getHost() const { return server_host_; }
    int getPort() const { return server_port_; }
    int getPing() const { return peer_ ? static_cast<int>(peer_->roundTripTime) : 0; }

private:
    ENetHost* host_ = nullptr;
    ENetPeer* peer_ = nullptr;
    std::atomic<bool> connected_{false};
    PacketCallback callback_;
    std::string server_host_;
    int server_port_ = 0;
    mutable std::mutex mutex_;
};