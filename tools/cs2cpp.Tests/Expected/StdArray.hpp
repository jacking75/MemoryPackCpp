#pragma once
#include "memorypack/memorypack.hpp"
#include <array>
#include <cstdint>

// ── Packet Structs ─────────────────────────────────────────────────────────────
// Member order MUST match C# [MemoryPackable] declaration order.

struct TransformData {
    int32_t               id = 0;
    std::array<float, 4>  quaternion = {};
    std::array<float, 16> matrix = {};
};

// ── IMemoryPackable Specializations ────────────────────────────────────────────
namespace memorypack {

// --- TransformData (memberCount=3) ---
template<> struct IMemoryPackable<TransformData> {
    static void Serialize(MemoryPackWriter& w, const TransformData* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(3);
        w.WriteInt32(v->id);
        w.WriteArray(v->quaternion);
        w.WriteArray(v->matrix);
    }
    static void Deserialize(MemoryPackReader& r, TransformData& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.id         = r.ReadInt32();
        if (cnt >= 2) v.quaternion = r.ReadArray<float, 4>();
        if (cnt >= 3) v.matrix     = r.ReadArray<float, 16>();
    }
};

} // namespace memorypack

