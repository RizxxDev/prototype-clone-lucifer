#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>

// ============================================================
// Growtopia TankPacket / GameUpdatePacket (56 Bytes Header)
// Referensi: H-pun/growtopia-docs, GrowDocs, Lucifer Client
// ============================================================

enum TankPacketType : uint8_t {
    PACKET_STATE                           = 0,
    PACKET_CALL_FUNCTION                   = 1,
    PACKET_UPDATE_STATUS                   = 2,
    PACKET_TILE_CHANGE_REQUEST             = 3,
    PACKET_SEND_MAP_DATA                   = 4,
    PACKET_SEND_TILE_UPDATE_DATA           = 5,
    PACKET_SEND_TILE_UPDATE_DATA_MULTIPLE  = 6,
    PACKET_TILE_ACTIVATE_REQUEST           = 7,
    PACKET_ITEM_CHANGE_OBJECT              = 8,
    PACKET_SEND_INVENTORY_STATE            = 9,
    PACKET_ITEM_ACTIVATE_REQUEST           = 10,
    PACKET_ITEM_ACTIVATE_OBJECT_REQUEST    = 11,
    PACKET_SEND_PLAYER_TRIBUTE_DATA        = 12,
    PACKET_SET_ICON_STATE                  = 13,
    PACKET_ITEM_EFFECT                     = 14,
    PACKET_SEND_ITEM_DATABASE_DATA         = 16,
    PACKET_SET_CHARACTER_STATE             = 20,
    PACKET_PING_REPLY                      = 21,
    PACKET_PING_REQUEST                    = 22,
    PACKET_GOT_PUNCHED                     = 24,
    PACKET_APP_INTEGRITY_FAIL              = 25,
    PACKET_DISCONNECT                      = 26,
    PACKET_BATTLE_JOIN                     = 27,
    PACKET_BATTLE_EVENT                    = 28,
    PACKET_USE_DOOR                        = 29,
    PACKET_PARENT_PURCHASE_RESULT          = 30,
    PACKET_PROGRESS                        = 31,
    PACKET_RESET_ALL_PUNCHES               = 32
};

namespace TankFlags {
    constexpr uint32_t NONE          = 0;
    constexpr uint32_t EXTRA_DATA    = 0x08;   // 8 (bit 3: extended payload present)
    constexpr uint32_t FACING_LEFT   = 0x10;   // 16 (0x10 is character facing left)
    constexpr uint32_t ON_GROUND     = 0x20;   // 32 (0x20 is standing on ground)
    constexpr uint32_t SWIMMING      = 0x40;   // 64
    constexpr uint32_t JUMPING       = 0x800;  // 2048
    constexpr uint32_t FLYING        = 0x1000; // 4096
}

#pragma pack(push, 1)
struct GameUpdatePacket {
    uint8_t  type = 0;              // TankPacketType
    uint8_t  obj_type = 0;          // Secondary / object type
    uint8_t  count1 = 0;            // Secondary count 1
    uint8_t  count2 = 0;            // Secondary count 2
    int32_t  net_id = -1;           // Player NetID
    int32_t  target_net_id = 0;     // Target NetID (0 for standard packets)
    uint32_t flags = 0;             // TankFlags bitmask
    float    float_var = 0.0f;      // Variable float (punch rate, etc.)
    int32_t  main_data = 0;         // Item ID / Tile ID / Object ID
    float    pos_x = 0.0f;          // Pixel X coordinate
    float    pos_y = 0.0f;          // Pixel Y coordinate
    float    speed_x = 0.0f;        // X Velocity
    float    speed_y = 0.0f;        // Y Velocity
    float    secondary_padding = 0; // Padding
    int32_t  int_x = 0;             // Tile X coordinate (0 when walking)
    int32_t  int_y = 0;             // Tile Y coordinate (0 when walking)
    uint32_t data_size = 0;         // Size of extended payload data
};
#pragma pack(pop)

static_assert(sizeof(GameUpdatePacket) == 56, "GameUpdatePacket header size must be exactly 56 bytes");

namespace TankPacketHelper {

    // Serialize TankPacket (56 bytes + optional extra data + 1 trailing null byte -> 57/61 bytes)
    inline std::vector<uint8_t> serialize(const GameUpdatePacket& pkt, const uint8_t* extra = nullptr, size_t extra_len = 0) {
        GameUpdatePacket copy = pkt;
        copy.data_size = static_cast<uint32_t>(extra_len);
        if (extra_len > 0) {
            copy.flags |= TankFlags::EXTRA_DATA;
        }

        // GTPS standard: 56 bytes GameUpdatePacket + extra_len + 1 trailing byte (57 bytes minimum payload, 61 bytes packet)
        std::vector<uint8_t> buffer(sizeof(GameUpdatePacket) + extra_len + 1, 0);
        std::memcpy(buffer.data(), &copy, sizeof(GameUpdatePacket));
        if (extra && extra_len > 0) {
            std::memcpy(buffer.data() + sizeof(GameUpdatePacket), extra, extra_len);
        }
        return buffer;
    }

    // Deserialize TankPacket from buffer
    inline bool deserialize(const uint8_t* data, size_t size, GameUpdatePacket& out_pkt, std::vector<uint8_t>& out_extra) {
        if (size < sizeof(GameUpdatePacket)) return false;

        std::memcpy(&out_pkt, data, sizeof(GameUpdatePacket));
        out_extra.clear();

        if (size > sizeof(GameUpdatePacket)) {
            size_t extra_size = size - sizeof(GameUpdatePacket);
            out_extra.resize(extra_size);
            std::memcpy(out_extra.data(), data + sizeof(GameUpdatePacket), extra_size);
        }
        return true;
    }

    // Create Tile Change Request (Punch, Place, Break, Wrench)
    inline GameUpdatePacket createTileChange(int net_id, int tile_x, int tile_y, int item_id, float player_x, float player_y) {
        GameUpdatePacket pkt{};
        pkt.type = PACKET_TILE_CHANGE_REQUEST;
        pkt.net_id = net_id;
        pkt.int_x = tile_x;
        pkt.int_y = tile_y;
        pkt.main_data = item_id; // 18 = punch/fist, 32 = wrench, otherwise item ID
        pkt.pos_x = player_x;
        pkt.pos_y = player_y;
        return pkt;
    }

    // Create Movement / State update
    inline GameUpdatePacket createState(int net_id, float x, float y, uint32_t flags = TankFlags::ON_GROUND) {
        GameUpdatePacket pkt{};
        pkt.type = PACKET_STATE;
        pkt.net_id = net_id;
        pkt.target_net_id = 0;
        pkt.flags = flags;
        pkt.pos_x = x;
        pkt.pos_y = y;
        pkt.speed_x = 0.0f;
        pkt.speed_y = 0.0f;
        pkt.int_x = 0;
        pkt.int_y = 0;
        pkt.data_size = 0;
        return pkt;
    }

    // Create Tile Activate Request (Enter door / portal)
    inline GameUpdatePacket createTileActivate(int net_id, int tile_x, int tile_y, float player_x, float player_y) {
        GameUpdatePacket pkt{};
        pkt.type = PACKET_TILE_ACTIVATE_REQUEST;
        pkt.net_id = net_id;
        pkt.int_x = tile_x;
        pkt.int_y = tile_y;
        pkt.pos_x = player_x;
        pkt.pos_y = player_y;
        return pkt;
    }

    // Create Item Change Object (Collect floating item / gem)
    inline GameUpdatePacket createCollectObject(int net_id, int object_uid, float player_x, float player_y) {
        GameUpdatePacket pkt{};
        pkt.type = PACKET_ITEM_CHANGE_OBJECT;
        pkt.net_id = net_id;
        pkt.main_data = object_uid;
        pkt.pos_x = player_x;
        pkt.pos_y = player_y;
        return pkt;
    }
}
