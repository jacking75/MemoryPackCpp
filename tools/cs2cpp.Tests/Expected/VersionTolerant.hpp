#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <string>

// ── Packet Structs ─────────────────────────────────────────────────────────────
// Member order MUST match C# [MemoryPackable] declaration order.

struct VersionTolerantPacket {
    int32_t     id = 0;
    std::string name;
    float       score = 0.f;
};

// ── IMemoryPackable Specializations ────────────────────────────────────────────
namespace memorypack {

// --- VersionTolerantPacket (memberCount=3) ---
template<> struct IMemoryPackable<VersionTolerantPacket> {
    static void Serialize(MemoryPackWriter& w, const VersionTolerantPacket* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        VersionTolerantWriter vt(w);
        vt.WriteMember(v->id);
        vt.WriteMember(v->name);
        vt.WriteMember(v->score);
    }
    static void Deserialize(MemoryPackReader& r, VersionTolerantPacket& v) {
        VersionTolerantReader vt(r);
        if (vt.IsNull()) return;
        vt.ReadMember(v.id);
        vt.ReadMember(v.name);
        vt.ReadMember(v.score);
        vt.Finish();
    }
};

} // namespace memorypack

