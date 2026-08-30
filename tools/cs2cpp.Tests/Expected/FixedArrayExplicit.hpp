#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <vector>

// ── Packet Structs ─────────────────────────────────────────────────────────────
// Member order MUST match C# [MemoryPackable] declaration order.

struct SkillSlotData {
    static constexpr int32_t MAX_SKILLS = 8;

    int32_t playerId = 0;
    int32_t skillIds[MAX_SKILLS] = {};
    int32_t skillCount = 0;
    float   cooldowns[MAX_SKILLS] = {};
    int32_t cooldownCount = 0;
};

struct MixedFormatPacket {
    static constexpr int32_t MAX_BONUSES = 4;
    static constexpr int32_t MAX_TAG_LEN = 16;

    int32_t              id = 0;
    std::vector<int32_t> dynamicScores;
    int32_t              fixedBonuses[MAX_BONUSES] = {};
    int32_t              fixedBonusCount = 0;
    int8_t               tag[MAX_TAG_LEN] = {};
    int32_t              tagCount = 0;
    double               multiplier = 0.0;
};

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

// --- MixedFormatPacket (memberCount=5) ---
template<> struct IMemoryPackable<MixedFormatPacket> {
    static void Serialize(MemoryPackWriter& w, const MixedFormatPacket* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(5);
        w.WriteInt32(v->id);
        w.WriteVector(v->dynamicScores);
        w.WriteArray(v->fixedBonuses, v->fixedBonusCount);
        w.WriteArray(v->tag, v->tagCount);
        w.WriteDouble(v->multiplier);
    }
    static void Deserialize(MemoryPackReader& r, MixedFormatPacket& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.id              = r.ReadInt32();
        if (cnt >= 2) v.dynamicScores   = r.ReadVector<int32_t>();
        if (cnt >= 3) v.fixedBonusCount = r.ReadArray(v.fixedBonuses, MixedFormatPacket::MAX_BONUSES);
        if (cnt >= 4) v.tagCount        = r.ReadArray(v.tag, MixedFormatPacket::MAX_TAG_LEN);
        if (cnt >= 5) v.multiplier      = r.ReadDouble();
    }
};

} // namespace memorypack

