#include "enet_client.hpp"
#include "logger.hpp"
#include <cstring>
#include <iostream>

EnetClient::EnetClient() {
    if (enet_initialize() != 0) {
        Logger::log("[ENet] Gagal initialize");
        throw std::runtime_error("ENet init failed");
    }
}

EnetClient::~EnetClient() {
    disconnect();
    if (host_) {
        enet_host_destroy(host_);
        host_ = nullptr;
    }
    enet_deinitialize();
}

bool EnetClient::connect(const std::string& host, int port, int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    server_host_ = host;
    server_port_ = port;

    if (peer_) {
        enet_peer_reset(peer_);
        peer_ = nullptr;
    }
    if (host_) {
        enet_host_destroy(host_);
        host_ = nullptr;
    }

    host_ = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!host_) {
        Logger::log("[ENet] Gagal buat host");
        return false;
    }

    host_->checksum = enet_crc32;
    enet_host_compress_with_range_coder(host_);

    ENetAddress addr;
    enet_address_set_host(&addr, host.c_str());
    addr.port = port;

    Logger::log("[ENet] Connecting ke ", host, ":", port, "...");

    peer_ = enet_host_connect(host_, &addr, 2, 0);
    if (!peer_) {
        Logger::log("[ENet] Gagal buat peer");
        return false;
    }

    // Tunggu CONNECT event
    ENetEvent ev;
    if (enet_host_service(host_, &ev, timeout_ms) > 0
        && ev.type == ENET_EVENT_TYPE_CONNECT) {
        connected_ = true;
        Logger::log("[ENet] ✅ Terhubung ke ", host, ":", port);
        return true;
    }

    Logger::log("[ENet] ❌ Gagal connect (timeout/refused)");
    enet_peer_reset(peer_);
    peer_ = nullptr;
    return false;
}

void EnetClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (peer_) {
        enet_peer_disconnect_now(peer_, 0);
        peer_ = nullptr;
    }
    if (host_) {
        enet_host_destroy(host_);
        host_ = nullptr;
    }
    connected_ = false;
}

bool EnetClient::send(uint32_t type, const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || !peer_ || !host_) return false;

    // Format Growtopia: [uint32 type][payload]
    std::vector<uint8_t> pkt;
    pkt.reserve(4 + payload.size());
    pkt.push_back(type & 0xFF);
    pkt.push_back((type >> 8) & 0xFF);
    pkt.push_back((type >> 16) & 0xFF);
    pkt.push_back((type >> 24) & 0xFF);
    pkt.insert(pkt.end(), payload.begin(), payload.end());

    ENetPacket* ep = enet_packet_create(pkt.data(), pkt.size(),
                                         ENET_PACKET_FLAG_RELIABLE);
    if (!ep) return false;

    if (enet_peer_send(peer_, 0, ep) != 0) {
        enet_packet_destroy(ep);
        return false;
    }

    enet_host_flush(host_);

    // Log outgoing
    Logger::log("[ENet] C->S type=", type, " len=", payload.size());
    if (Logger::hexdump) Logger::hexdumpBytes("C->S", pkt);

    return true;
}

bool EnetClient::sendText(uint32_t type, const std::string& text) {
    std::vector<uint8_t> payload(text.begin(), text.end());
    payload.push_back(0); // Null-terminated string
    return send(type, payload);
}

void EnetClient::poll() {
    std::vector<GtPacketData> to_dispatch;
    PacketCallback cb_copy;
    EventCallback connect_cb_copy;
    EventCallback disconnect_cb_copy;
    bool triggered_connect = false;
    bool triggered_disconnect = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!host_) return;

        ENetEvent ev;
        while (enet_host_service(host_, &ev, 0) > 0) {
            switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT:
                connected_ = true;
                triggered_connect = true;
                Logger::log("[ENet] Connected event");
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
                connected_ = false;
                peer_ = nullptr;
                triggered_disconnect = true;
                Logger::log("[ENet] Disconnect event (",
                            ev.peer ? "peer" : "null", ")");
                break;

            case ENET_EVENT_TYPE_RECEIVE: {
                if (ev.packet->dataLength >= 4) {
                    uint32_t type;
                    std::memcpy(&type, ev.packet->data, 4);
                    std::vector<uint8_t> payload(
                        ev.packet->data + 4,
                        ev.packet->data + ev.packet->dataLength);

                    to_dispatch.push_back(GtPacketData{type, std::move(payload), false});

                    Logger::logDebug("[ENet] S->C type=", type,
                                    " len=", to_dispatch.back().payload.size());
                    if (Logger::hexdump) {
                        std::vector<uint8_t> full(
                            ev.packet->data,
                            ev.packet->data + ev.packet->dataLength);
                        Logger::hexdumpBytes("S->C", full);
                    }
                }
                enet_packet_destroy(ev.packet);
                break;
            }

            default: break;
            }
        }
        cb_copy = callback_;
        connect_cb_copy = connect_callback_;
        disconnect_cb_copy = disconnect_callback_;
    }

    if (triggered_connect && connect_cb_copy) {
        connect_cb_copy();
    }
    if (triggered_disconnect && disconnect_cb_copy) {
        disconnect_cb_copy();
    }

    // Dispatch callbacks outside of lock
    if (cb_copy) {
        for (const auto& pkt : to_dispatch) {
            cb_copy(pkt);
        }
    }
}