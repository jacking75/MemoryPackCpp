#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <string>

// ── Packet IDs ─────────────────────────────────────────────────────────────────
enum class PacketId : uint16_t {
    LoginRequest  = 1,
    LoginResponse = 2,
    Heartbeat     = 3,
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
    bool    success = false;
    int32_t playerId = 0;
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

// --- LoginResponse (memberCount=2) ---
template<> struct IMemoryPackable<LoginResponse> {
    static void Serialize(MemoryPackWriter& w, const LoginResponse* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(2);
        w.WriteBool(v->success);
        w.WriteInt32(v->playerId);
    }
    static void Deserialize(MemoryPackReader& r, LoginResponse& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.success  = r.ReadBool();
        if (cnt >= 2) v.playerId = r.ReadInt32();
    }
};

} // namespace memorypack

