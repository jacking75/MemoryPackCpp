#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// ── Packet IDs ─────────────────────────────────────────────────────────────────
enum class PacketId : uint16_t {
    LoginRequest      = 1,
    LoginResponse     = 2,
    PlayerState       = 3,
    ScoreUpdate       = 4,
    ChatMessage       = 5,
    InventoryData     = 6,
    BufferData        = 7,
    IntArrayPacket    = 8,
    SkillSlotData     = 9,
    MapTileRow        = 10,
    MixedFormatPacket = 11,
};

// ── Packet Header: [2B packetId][4B bodyLength] ────────────────────────────────
constexpr size_t PACKET_HEADER_SIZE = 6;

// ── Packet Structs ─────────────────────────────────────────────────────────────
// Member order MUST match C# [MemoryPackable] declaration order.

struct LoginRequest {
    std::string username;
    int32_t     level = 0;
};

struct LoginResponse {
    bool        success = false;
    int32_t     playerId = 0;
    std::string message;
};

struct PlayerState {
    int32_t     playerId = 0;
    float       posX = 0.f;
    float       posY = 0.f;
    float       posZ = 0.f;
    std::string name;
};

struct ScoreUpdate {
    int32_t              playerId = 0;
    std::vector<int32_t> scores;
    double               totalScore = 0.0;
};

struct ChatMessage {
    int32_t     senderId = 0;
    std::string message;
    int64_t     timestamp = 0;
};

struct InventoryData {
    int32_t                  playerId = 0;
    std::vector<std::string> itemNames;
    std::vector<int32_t>     itemCounts;
};

struct BufferData {
    uint8_t              tag = 0;
    int8_t               grade = 0;
    std::vector<uint8_t> rawData;
    std::vector<int8_t>  charCodes;
};

struct IntArrayPacket {
    int32_t              id = 0;
    std::vector<int16_t> shortArray;
    std::vector<int32_t> intArray;
    std::vector<int64_t> longArray;
};

struct SkillSlotData {
    static constexpr int32_t MAX_SKILLS = 8;

    int32_t playerId = 0;
    int32_t skillIds[MAX_SKILLS] = {};
    int32_t skillCount = 0;
    float   cooldowns[MAX_SKILLS] = {};
    int32_t cooldownCount = 0;
};

struct MapTileRow {
    static constexpr int32_t MAX_TILES = 64;

    int32_t rowIndex = 0;
    uint8_t tiles[MAX_TILES] = {};
    int32_t tileCount = 0;
    int16_t heights[MAX_TILES] = {};
    int32_t heightCount = 0;
};

struct MixedFormatPacket {
    static constexpr int32_t MAX_BONUSES = 4;
    static constexpr int32_t MAX_TAG_LEN = 16;

    int32_t              id = 0;
    std::vector<int32_t> dynamicScores;
    int32_t              fixedBonuses[MAX_BONUSES] = {};
    int32_t              fixedBonusCount = 0;
    int8_t               tagBytes[MAX_TAG_LEN] = {};
    int32_t              tagByteCount = 0;
    double               multiplier = 0.0;
};

// ── Serializer definitions ─────────────────────────────────────────────────────
MEMORYPACK_DEFINE(LoginRequest, username, level)
MEMORYPACK_DEFINE(LoginResponse, success, playerId, message)
MEMORYPACK_DEFINE(PlayerState, playerId, posX, posY, posZ, name)
MEMORYPACK_DEFINE(ScoreUpdate, playerId, scores, totalScore)
MEMORYPACK_DEFINE(ChatMessage, senderId, message, timestamp)
MEMORYPACK_DEFINE(InventoryData, playerId, itemNames, itemCounts)
MEMORYPACK_DEFINE(BufferData, tag, grade, rawData, charCodes)
MEMORYPACK_DEFINE(IntArrayPacket, id, shortArray, intArray, longArray)

// ── IMemoryPackable Specializations ────────────────────────────────────────────
namespace memorypack {

// --- SkillSlotData (memberCount=3) ---
template<> struct IMemoryPackable<SkillSlotData> {
    static void Serialize(MemoryPackWriter& w, const SkillSlotData* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(3);
        w.WriteInt32(v->playerId);
        w.WriteArray(v->skillIds, v->skillCount);
        w.WriteArray(v->cooldowns, v->cooldownCount);
    }
    static void Deserialize(MemoryPackReader& r, SkillSlotData& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.playerId      = r.ReadInt32();
        if (cnt >= 2) v.skillCount    = r.ReadArray(v.skillIds, SkillSlotData::MAX_SKILLS);
        if (cnt >= 3) v.cooldownCount = r.ReadArray(v.cooldowns, SkillSlotData::MAX_SKILLS);
    }
};

// --- MapTileRow (memberCount=3) ---
template<> struct IMemoryPackable<MapTileRow> {
    static void Serialize(MemoryPackWriter& w, const MapTileRow* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(3);
        w.WriteInt32(v->rowIndex);
        w.WriteArray(v->tiles, v->tileCount);
        w.WriteArray(v->heights, v->heightCount);
    }
    static void Deserialize(MemoryPackReader& r, MapTileRow& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.rowIndex    = r.ReadInt32();
        if (cnt >= 2) v.tileCount   = r.ReadArray(v.tiles, MapTileRow::MAX_TILES);
        if (cnt >= 3) v.heightCount = r.ReadArray(v.heights, MapTileRow::MAX_TILES);
    }
};

// --- MixedFormatPacket (memberCount=5) ---
template<> struct IMemoryPackable<MixedFormatPacket> {
    static void Serialize(MemoryPackWriter& w, const MixedFormatPacket* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(5);
        w.WriteInt32(v->id);
        w.WriteVector(v->dynamicScores);
        w.WriteArray(v->fixedBonuses, v->fixedBonusCount);
        w.WriteArray(v->tagBytes, v->tagByteCount);
        w.WriteDouble(v->multiplier);
    }
    static void Deserialize(MemoryPackReader& r, MixedFormatPacket& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.id              = r.ReadInt32();
        if (cnt >= 2) v.dynamicScores   = r.ReadVector<int32_t>();
        if (cnt >= 3) v.fixedBonusCount = r.ReadArray(v.fixedBonuses, MixedFormatPacket::MAX_BONUSES);
        if (cnt >= 4) v.tagByteCount    = r.ReadArray(v.tagBytes, MixedFormatPacket::MAX_TAG_LEN);
        if (cnt >= 5) v.multiplier      = r.ReadDouble();
    }
};

} // namespace memorypack

// ── Packet dispatch table ──────────────────────────────────────────────────────
template<typename Handler>
bool DispatchPacket(PacketId id, std::span<const uint8_t> body, Handler&& handler) {
    switch (id) {
    case PacketId::LoginRequest: { auto v = memorypack::Deserialize<LoginRequest>(body); handler(v); return true; }
    case PacketId::LoginResponse: { auto v = memorypack::Deserialize<LoginResponse>(body); handler(v); return true; }
    case PacketId::PlayerState: { auto v = memorypack::Deserialize<PlayerState>(body); handler(v); return true; }
    case PacketId::ScoreUpdate: { auto v = memorypack::Deserialize<ScoreUpdate>(body); handler(v); return true; }
    case PacketId::ChatMessage: { auto v = memorypack::Deserialize<ChatMessage>(body); handler(v); return true; }
    case PacketId::InventoryData: { auto v = memorypack::Deserialize<InventoryData>(body); handler(v); return true; }
    case PacketId::BufferData: { auto v = memorypack::Deserialize<BufferData>(body); handler(v); return true; }
    case PacketId::IntArrayPacket: { auto v = memorypack::Deserialize<IntArrayPacket>(body); handler(v); return true; }
    case PacketId::SkillSlotData: { auto v = memorypack::Deserialize<SkillSlotData>(body); handler(v); return true; }
    case PacketId::MapTileRow: { auto v = memorypack::Deserialize<MapTileRow>(body); handler(v); return true; }
    case PacketId::MixedFormatPacket: { auto v = memorypack::Deserialize<MixedFormatPacket>(body); handler(v); return true; }
    default: return false;
    }
}

// ── Schema hash ────────────────────────────────────────────────────────────────
// FNV-1a 64bit over "Type(0:CsType;1:CsType;...)\n" for every packet.
inline constexpr uint64_t PACKET_SCHEMA_HASH = 0x84FDF5A4970EF78FULL;

