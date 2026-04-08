#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <string>
#include <vector>

// ── Packet IDs (must match C# PacketId enum) ──────────────────────────────────
enum class PacketId : uint16_t {
    LoginRequest    = 101,
    LoginResponse   = 102,
    RoomJoinRequest = 103,
    RoomJoinResponse= 104,
    RoomChat        = 105,
    PrivateChat     = 106,
    UserEntered     = 107,
    UserLeft        = 108,
};

constexpr size_t PACKET_HEADER_SIZE = 6; // [2B packetId][4B bodyLength]

// ── Packet Structs ─────────────────────────────────────────────────────────────

struct LoginRequest {
    std::string username;
};

struct LoginResponse {
    bool        success = false;
    std::string message;
};

struct RoomJoinRequest {
    std::string roomName;
};

struct RoomJoinResponse {
    bool                      success = false;
    std::vector<std::string>  existingUsers;
};

struct RoomChat {
    std::string senderName;
    std::string message;
};

struct PrivateChat {
    std::string senderName;
    std::string targetName;
    std::string message;
};

struct UserEntered {
    std::string username;
};

struct UserLeft {
    std::string username;
};

// ── IMemoryPackable Specializations ────────────────────────────────────────────
namespace memorypack {

template<> struct IMemoryPackable<LoginRequest> {
    static void Serialize(MemoryPackWriter& w, const LoginRequest* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(1);
        w.WriteString(v->username);
    }
    static void Deserialize(MemoryPackReader& r, LoginRequest& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) { auto s = r.ReadString(); v.username = s.value_or(""); }
    }
};

template<> struct IMemoryPackable<LoginResponse> {
    static void Serialize(MemoryPackWriter& w, const LoginResponse* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(2);
        w.WriteBool(v->success);
        w.WriteString(v->message);
    }
    static void Deserialize(MemoryPackReader& r, LoginResponse& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.success = r.ReadBool();
        if (cnt >= 2) { auto s = r.ReadString(); v.message = s.value_or(""); }
    }
};

template<> struct IMemoryPackable<RoomJoinRequest> {
    static void Serialize(MemoryPackWriter& w, const RoomJoinRequest* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(1);
        w.WriteString(v->roomName);
    }
    static void Deserialize(MemoryPackReader& r, RoomJoinRequest& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) { auto s = r.ReadString(); v.roomName = s.value_or(""); }
    }
};

template<> struct IMemoryPackable<RoomJoinResponse> {
    static void Serialize(MemoryPackWriter& w, const RoomJoinResponse* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(2);
        w.WriteBool(v->success);
        w.WriteStringVector(v->existingUsers);
    }
    static void Deserialize(MemoryPackReader& r, RoomJoinResponse& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.success       = r.ReadBool();
        if (cnt >= 2) v.existingUsers = r.ReadStringVector();
    }
};

template<> struct IMemoryPackable<RoomChat> {
    static void Serialize(MemoryPackWriter& w, const RoomChat* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(2);
        w.WriteString(v->senderName);
        w.WriteString(v->message);
    }
    static void Deserialize(MemoryPackReader& r, RoomChat& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) { auto s = r.ReadString(); v.senderName = s.value_or(""); }
        if (cnt >= 2) { auto s = r.ReadString(); v.message    = s.value_or(""); }
    }
};

template<> struct IMemoryPackable<PrivateChat> {
    static void Serialize(MemoryPackWriter& w, const PrivateChat* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(3);
        w.WriteString(v->senderName);
        w.WriteString(v->targetName);
        w.WriteString(v->message);
    }
    static void Deserialize(MemoryPackReader& r, PrivateChat& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) { auto s = r.ReadString(); v.senderName = s.value_or(""); }
        if (cnt >= 2) { auto s = r.ReadString(); v.targetName = s.value_or(""); }
        if (cnt >= 3) { auto s = r.ReadString(); v.message    = s.value_or(""); }
    }
};

template<> struct IMemoryPackable<UserEntered> {
    static void Serialize(MemoryPackWriter& w, const UserEntered* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(1);
        w.WriteString(v->username);
    }
    static void Deserialize(MemoryPackReader& r, UserEntered& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) { auto s = r.ReadString(); v.username = s.value_or(""); }
    }
};

template<> struct IMemoryPackable<UserLeft> {
    static void Serialize(MemoryPackWriter& w, const UserLeft* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(1);
        w.WriteString(v->username);
    }
    static void Deserialize(MemoryPackReader& r, UserLeft& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) { auto s = r.ReadString(); v.username = s.value_or(""); }
    }
};

} // namespace memorypack
