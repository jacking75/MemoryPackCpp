#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <string>

// ── Packet Structs ─────────────────────────────────────────────────────────────
// Member order MUST match C# [MemoryPackable] declaration order.

struct LoginRequest {
    std::string username;
    int32_t     level = 0;
    bool        remember = false;
    double      score = 0.0;
};

struct FieldPacket {
    int32_t     id = 0;
    std::string name;
    int64_t     ticks = 0;
};

struct RecordPacket {
    int32_t     id = 0;
    std::string name;
};

// ── IMemoryPackable Specializations ────────────────────────────────────────────
namespace memorypack {

// --- LoginRequest (memberCount=4) ---
template<> struct IMemoryPackable<LoginRequest> {
    static void Serialize(MemoryPackWriter& w, const LoginRequest* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(4);
        w.WriteString(v->username);
        w.WriteInt32(v->level);
        w.WriteBool(v->remember);
        w.WriteDouble(v->score);
    }
    static void Deserialize(MemoryPackReader& r, LoginRequest& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) { auto s = r.ReadString(); v.username = s.value_or(""); }
        if (cnt >= 2) v.level    = r.ReadInt32();
        if (cnt >= 3) v.remember = r.ReadBool();
        if (cnt >= 4) v.score    = r.ReadDouble();
    }
};

// --- FieldPacket (memberCount=3) ---
template<> struct IMemoryPackable<FieldPacket> {
    static void Serialize(MemoryPackWriter& w, const FieldPacket* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(3);
        w.WriteInt32(v->id);
        w.WriteString(v->name);
        w.WriteInt64(v->ticks);
    }
    static void Deserialize(MemoryPackReader& r, FieldPacket& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.id    = r.ReadInt32();
        if (cnt >= 2) { auto s = r.ReadString(); v.name = s.value_or(""); }
        if (cnt >= 3) v.ticks = r.ReadInt64();
    }
};

// --- RecordPacket (memberCount=2) ---
template<> struct IMemoryPackable<RecordPacket> {
    static void Serialize(MemoryPackWriter& w, const RecordPacket* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(2);
        w.WriteInt32(v->id);
        w.WriteString(v->name);
    }
    static void Deserialize(MemoryPackReader& r, RecordPacket& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.id = r.ReadInt32();
        if (cnt >= 2) { auto s = r.ReadString(); v.name = s.value_or(""); }
    }
};

} // namespace memorypack

