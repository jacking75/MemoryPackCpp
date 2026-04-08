#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <string>
#include <vector>

// ── Packet IDs ─────────────────────────────────────────────────────────────────
enum class PacketId : uint16_t {
    LoginRequest  = 1,
    LoginResponse = 2,
    PlayerState   = 3,
    ScoreUpdate   = 4,
    ChatMessage   = 5,
    InventoryData = 6,
    BufferData       = 7,
    IntArrayPacket   = 8,
    SkillSlotData    = 9,   // C fixed array: int32[] + float[]
    MapTileRow       = 10,  // C fixed array: uint8[] + int16[]
    MixedFormatPacket= 11,  // vector + C array + char array mixed
};

// ── Packet Header: [2B packetId][4B bodyLength] ────────────────────────────────
constexpr size_t PACKET_HEADER_SIZE = 6;

// ── Packet Structs ─────────────────────────────────────────────────────────────
// Member order MUST match C# [MemoryPackable] class declaration order.

struct LoginRequest {
    std::string username;
    int32_t     level = 0;
};

struct LoginResponse {
    bool        success  = false;
    int32_t     playerId = 0;
    std::string message;
};

struct PlayerState {
    int32_t     playerId = 0;
    float       posX     = 0.f;
    float       posY     = 0.f;
    float       posZ     = 0.f;
    std::string name;
};

struct ScoreUpdate {
    int32_t              playerId   = 0;
    std::vector<int32_t> scores;
    double               totalScore = 0.0;
};

struct ChatMessage {
    int32_t     senderId  = 0;
    std::string message;
    int64_t     timestamp = 0;
};

struct InventoryData {
    int32_t                   playerId = 0;
    std::vector<std::string>  itemNames;
    std::vector<int32_t>      itemCounts;
};

struct BufferData {
    uint8_t              tag   = 0;     // C# byte
    int8_t               grade = 0;     // C# sbyte (signed char)
    std::vector<uint8_t> rawData;       // C# byte[]  / List<byte>
    std::vector<int8_t>  charCodes;     // C# sbyte[] / List<sbyte>
};

struct IntArrayPacket {
    int32_t               id = 0;
    std::vector<int16_t>  shortArray;   // C# List<short>
    std::vector<int32_t>  intArray;     // C# List<int>
    std::vector<int64_t>  longArray;    // C# List<long>
};

// ── C fixed array packets ──────────────────────────────────────────────────────
// skillCount / tileCount / bonusCount / tagLength are LOCAL tracking only.
// They are NOT serialized — the count goes into the collection header.

// Pattern A: int32[] + float[] fixed arrays (typical game skill slots)
struct SkillSlotData {
    static constexpr int32_t MAX_SKILLS = 8;

    int32_t playerId = 0;
    int32_t skillIds[MAX_SKILLS]  = {};   // C# List<int>
    int32_t skillCount = 0;               // used count (not serialized)
    float   cooldowns[MAX_SKILLS] = {};   // C# List<float>
    int32_t cooldownCount = 0;            // used count (not serialized)
};

// Pattern B: uint8[] + int16[] fixed arrays (binary tile data)
struct MapTileRow {
    static constexpr int32_t MAX_TILES = 64;

    int32_t rowIndex = 0;
    uint8_t tiles[MAX_TILES]   = {};      // C# List<byte>
    int32_t tileCount = 0;                // used count (not serialized)
    int16_t heights[MAX_TILES] = {};      // C# List<short>
    int32_t heightCount = 0;              // used count (not serialized)
};

// Pattern C: vector + C array + char(int8) array mixed in one packet
struct MixedFormatPacket {
    static constexpr int32_t MAX_BONUSES = 4;
    static constexpr int32_t MAX_TAG_LEN = 16;

    int32_t              id = 0;
    std::vector<int32_t> dynamicScores;                 // std::vector (dynamic)
    int32_t              fixedBonuses[MAX_BONUSES] = {}; // C int32 array (fixed)
    int32_t              bonusCount = 0;
    int8_t               tag[MAX_TAG_LEN] = {};          // C char array (fixed)
    int32_t              tagLength = 0;
    double               multiplier = 0.0;
};

// ── IMemoryPackable Specializations ────────────────────────────────────────────
namespace memorypack {

// --- LoginRequest (memberCount=2) ---
template<> struct IMemoryPackable<LoginRequest> {
    static void Serialize(MemoryPackWriter& w, const LoginRequest* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(2);
        w.WriteString(v->username);
        w.WriteInt32(v->level);
    }
    static void Deserialize(MemoryPackReader& r, LoginRequest& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) { auto s = r.ReadString(); v.username = s.value_or(""); }
        if (cnt >= 2) v.level = r.ReadInt32();
    }
};

// --- LoginResponse (memberCount=3) ---
template<> struct IMemoryPackable<LoginResponse> {
    static void Serialize(MemoryPackWriter& w, const LoginResponse* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(3);
        w.WriteBool(v->success);
        w.WriteInt32(v->playerId);
        w.WriteString(v->message);
    }
    static void Deserialize(MemoryPackReader& r, LoginResponse& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.success  = r.ReadBool();
        if (cnt >= 2) v.playerId = r.ReadInt32();
        if (cnt >= 3) { auto s = r.ReadString(); v.message = s.value_or(""); }
    }
};

// --- PlayerState (memberCount=5) ---
template<> struct IMemoryPackable<PlayerState> {
    static void Serialize(MemoryPackWriter& w, const PlayerState* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(5);
        w.WriteInt32(v->playerId);
        w.WriteFloat(v->posX);
        w.WriteFloat(v->posY);
        w.WriteFloat(v->posZ);
        w.WriteString(v->name);
    }
    static void Deserialize(MemoryPackReader& r, PlayerState& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.playerId = r.ReadInt32();
        if (cnt >= 2) v.posX     = r.ReadFloat();
        if (cnt >= 3) v.posY     = r.ReadFloat();
        if (cnt >= 4) v.posZ     = r.ReadFloat();
        if (cnt >= 5) { auto s = r.ReadString(); v.name = s.value_or(""); }
    }
};

// --- ScoreUpdate (memberCount=3) ---
template<> struct IMemoryPackable<ScoreUpdate> {
    static void Serialize(MemoryPackWriter& w, const ScoreUpdate* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(3);
        w.WriteInt32(v->playerId);
        w.WriteVector(v->scores);
        w.WriteDouble(v->totalScore);
    }
    static void Deserialize(MemoryPackReader& r, ScoreUpdate& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.playerId   = r.ReadInt32();
        if (cnt >= 2) v.scores     = r.ReadVector<int32_t>();
        if (cnt >= 3) v.totalScore = r.ReadDouble();
    }
};

// --- ChatMessage (memberCount=3) ---
template<> struct IMemoryPackable<ChatMessage> {
    static void Serialize(MemoryPackWriter& w, const ChatMessage* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(3);
        w.WriteInt32(v->senderId);
        w.WriteString(v->message);
        w.WriteInt64(v->timestamp);
    }
    static void Deserialize(MemoryPackReader& r, ChatMessage& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.senderId  = r.ReadInt32();
        if (cnt >= 2) { auto s = r.ReadString(); v.message = s.value_or(""); }
        if (cnt >= 3) v.timestamp = r.ReadInt64();
    }
};

// --- InventoryData (memberCount=3) ---
template<> struct IMemoryPackable<InventoryData> {
    static void Serialize(MemoryPackWriter& w, const InventoryData* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(3);
        w.WriteInt32(v->playerId);
        w.WriteStringVector(v->itemNames);
        w.WriteVector(v->itemCounts);
    }
    static void Deserialize(MemoryPackReader& r, InventoryData& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.playerId   = r.ReadInt32();
        if (cnt >= 2) v.itemNames  = r.ReadStringVector();
        if (cnt >= 3) v.itemCounts = r.ReadVector<int32_t>();
    }
};

// --- BufferData (memberCount=4) ---
template<> struct IMemoryPackable<BufferData> {
    static void Serialize(MemoryPackWriter& w, const BufferData* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(4);
        w.WriteUInt8(v->tag);
        w.WriteInt8(v->grade);
        w.WriteVector(v->rawData);
        w.WriteVector(v->charCodes);
    }
    static void Deserialize(MemoryPackReader& r, BufferData& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.tag       = r.ReadUInt8();
        if (cnt >= 2) v.grade     = r.ReadInt8();
        if (cnt >= 3) v.rawData   = r.ReadVector<uint8_t>();
        if (cnt >= 4) v.charCodes = r.ReadVector<int8_t>();
    }
};

// --- IntArrayPacket (memberCount=4) ---
template<> struct IMemoryPackable<IntArrayPacket> {
    static void Serialize(MemoryPackWriter& w, const IntArrayPacket* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(4);
        w.WriteInt32(v->id);
        w.WriteVector(v->shortArray);
        w.WriteVector(v->intArray);
        w.WriteVector(v->longArray);
    }
    static void Deserialize(MemoryPackReader& r, IntArrayPacket& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.id         = r.ReadInt32();
        if (cnt >= 2) v.shortArray = r.ReadVector<int16_t>();
        if (cnt >= 3) v.intArray   = r.ReadVector<int32_t>();
        if (cnt >= 4) v.longArray  = r.ReadVector<int64_t>();
    }
};

// --- SkillSlotData (memberCount=3) ---
// C# members: PlayerId, SkillIds(List<int>), Cooldowns(List<float>)
template<> struct IMemoryPackable<SkillSlotData> {
    static void Serialize(MemoryPackWriter& w, const SkillSlotData* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(3);
        w.WriteInt32(v->playerId);
        w.WriteArray(v->skillIds,  v->skillCount);     // C array partial write
        w.WriteArray(v->cooldowns, v->cooldownCount);   // C array partial write
    }
    static void Deserialize(MemoryPackReader& r, SkillSlotData& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.playerId      = r.ReadInt32();
        if (cnt >= 2) v.skillCount    = r.ReadArray(v.skillIds,  SkillSlotData::MAX_SKILLS);
        if (cnt >= 3) v.cooldownCount = r.ReadArray(v.cooldowns, SkillSlotData::MAX_SKILLS);
    }
};

// --- MapTileRow (memberCount=3) ---
// C# members: RowIndex, Tiles(List<byte>), Heights(List<short>)
template<> struct IMemoryPackable<MapTileRow> {
    static void Serialize(MemoryPackWriter& w, const MapTileRow* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(3);
        w.WriteInt32(v->rowIndex);
        w.WriteArray(v->tiles,   v->tileCount);         // uint8 C array
        w.WriteArray(v->heights, v->heightCount);        // int16 C array
    }
    static void Deserialize(MemoryPackReader& r, MapTileRow& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.rowIndex    = r.ReadInt32();
        if (cnt >= 2) v.tileCount   = r.ReadArray(v.tiles,   MapTileRow::MAX_TILES);
        if (cnt >= 3) v.heightCount = r.ReadArray(v.heights, MapTileRow::MAX_TILES);
    }
};

// --- MixedFormatPacket (memberCount=5) ---
// C# members: Id, DynamicScores(List<int>), FixedBonuses(List<int>),
//             TagBytes(List<sbyte>), Multiplier
template<> struct IMemoryPackable<MixedFormatPacket> {
    static void Serialize(MemoryPackWriter& w, const MixedFormatPacket* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(5);
        w.WriteInt32(v->id);
        w.WriteVector(v->dynamicScores);                        // vector
        w.WriteArray(v->fixedBonuses, v->bonusCount);           // C int32 array
        w.WriteArray(v->tag, v->tagLength);                     // C int8(char) array
        w.WriteDouble(v->multiplier);
    }
    static void Deserialize(MemoryPackReader& r, MixedFormatPacket& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.id             = r.ReadInt32();
        if (cnt >= 2) v.dynamicScores  = r.ReadVector<int32_t>();        // vector
        if (cnt >= 3) v.bonusCount     = r.ReadArray(v.fixedBonuses, MixedFormatPacket::MAX_BONUSES);
        if (cnt >= 4) v.tagLength      = r.ReadArray(v.tag, MixedFormatPacket::MAX_TAG_LEN);
        if (cnt >= 5) v.multiplier     = r.ReadDouble();
    }
};

} // namespace memorypack
