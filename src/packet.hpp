#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

// ============================================================
// Konstanta paket Growtopia (sebagian, untuk Fase 1)
// Referensi: H-pun/growtopia-docs, GrowDocs
// ============================================================
namespace GtPacket {
    // Tipe paket network-level
    constexpr uint32_t NET_MESSAGE_UNKNOWN          = 0;
    constexpr uint32_t NET_MESSAGE_SERVER_HELLO     = 1;
    constexpr uint32_t NET_MESSAGE_GENERIC_TEXT     = 2;
    constexpr uint32_t NET_MESSAGE_GAME_MESSAGE     = 3;
    constexpr uint32_t NET_MESSAGE_GAME_PACKET      = 4;
    constexpr uint32_t NET_MESSAGE_ERROR            = 5;
    constexpr uint32_t NET_MESSAGE_TRACK            = 6;
    constexpr uint32_t NET_MESSAGE_CLIENT_LOG_RESPONSE = 7;
}

// ============================================================
// Helper untuk menulis/membaca data biner
// ============================================================
class ByteWriter {
public:
    std::vector<uint8_t> buf;

    void u8(uint8_t v) { buf.push_back(v); }

    void u16(uint16_t v) {
        buf.push_back(v & 0xFF);
        buf.push_back((v >> 8) & 0xFF);
    }

    void u32(uint32_t v) {
        buf.push_back(v & 0xFF);
        buf.push_back((v >> 8) & 0xFF);
        buf.push_back((v >> 16) & 0xFF);
        buf.push_back((v >> 24) & 0xFF);
    }

    void i32(int32_t v) { u32((uint32_t)v); }

    void f32(float v) {
        uint32_t tmp;
        std::memcpy(&tmp, &v, 4);
        u32(tmp);
    }

    void str(const std::string& s) {
        buf.insert(buf.end(), s.begin(), s.end());
    }

    void str16(const std::string& s) {
        u16((uint16_t)s.size());
        str(s);
    }

    void raw(const std::vector<uint8_t>& d) {
        buf.insert(buf.end(), d.begin(), d.end());
    }
};

class ByteReader {
public:
    const uint8_t* data;
    size_t size;
    size_t pos = 0;

    ByteReader(const uint8_t* d, size_t s) : data(d), size(s) {}

    bool ok() const { return pos <= size; }

    uint8_t u8() {
        if (pos >= size) return 0;
        return data[pos++];
    }

    uint16_t u16() {
        if (pos + 2 > size) return 0;
        uint16_t v = data[pos] | (data[pos + 1] << 8);
        pos += 2;
        return v;
    }

    uint32_t u32() {
        if (pos + 4 > size) return 0;
        uint32_t v = data[pos] | (data[pos + 1] << 8)
                   | (data[pos + 2] << 16) | (data[pos + 3] << 24);
        pos += 4;
        return v;
    }

    int32_t i32() { return (int32_t)u32(); }

    float f32() {
        uint32_t tmp = u32();
        float v;
        std::memcpy(&v, &tmp, 4);
        return v;
    }

    std::string str(size_t len) {
        if (pos + len > size) len = size - pos;
        std::string s((const char*)(data + pos), len);
        pos += len;
        return s;
    }

    std::string str16() {
        uint16_t len = u16();
        return str(len);
    }

    size_t remaining() const { return size - pos; }
};

struct Variant {
    uint8_t type = 0;
    int32_t int_val = 0;
    uint32_t uint_val = 0;
    float float_val = 0.0f;
    std::string str_val;
};

inline std::vector<Variant> parseVariantList(const uint8_t* data, size_t size) {
    std::vector<Variant> vars;
    if (size < 1) return vars;
    ByteReader reader(data, size);
    uint8_t count = reader.u8();
    for (uint8_t i = 0; i < count && reader.ok(); ++i) {
        if (reader.remaining() < 2) break;
        uint8_t idx = reader.u8();
        uint8_t type = reader.u8();
        Variant v;
        v.type = type;
        if (type == 1) { // float
            v.float_val = reader.f32();
        } else if (type == 2) { // string
            uint32_t len = reader.u32();
            v.str_val = reader.str(len);
        } else if (type == 3) { // vec2
            v.float_val = reader.f32();
            reader.f32();
        } else if (type == 4) { // vec3
            v.float_val = reader.f32();
            reader.f32();
            reader.f32();
        } else if (type == 5) { // uint
            v.uint_val = reader.u32();
        } else if (type == 9) { // int
            v.int_val = reader.i32();
        }
        vars.push_back(v);
    }
    return vars;
}