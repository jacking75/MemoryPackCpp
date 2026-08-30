#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// ── Packet IDs ─────────────────────────────────────────────────────────────────
enum class PacketId : uint16_t {
    LoginRequest     = 101,
    LoginResponse    = 102,
    RoomJoinRequest  = 103,
    RoomJoinResponse = 104,
    RoomChat         = 105,
    PrivateChat      = 106,
    UserEntered      = 107,
    UserLeft         = 108,
};

// ── Packet Header: [2B packetId][4B bodyLength] ────────────────────────────────
constexpr size_t PACKET_HEADER_SIZE = 6;

// ── Packet Structs ─────────────────────────────────────────────────────────────
// Member order MUST match C# [MemoryPackable] declaration order.

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
    bool                     success = false;
    std::vector<std::string> existingUsers;
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

// ── Serializer definitions ─────────────────────────────────────────────────────
MEMORYPACK_DEFINE(LoginRequest, username)
MEMORYPACK_DEFINE(LoginResponse, success, message)
MEMORYPACK_DEFINE(RoomJoinRequest, roomName)
MEMORYPACK_DEFINE(RoomJoinResponse, success, existingUsers)
MEMORYPACK_DEFINE(RoomChat, senderName, message)
MEMORYPACK_DEFINE(PrivateChat, senderName, targetName, message)
MEMORYPACK_DEFINE(UserEntered, username)
MEMORYPACK_DEFINE(UserLeft, username)

// ── Packet dispatch table ──────────────────────────────────────────────────────
template<typename Handler>
bool DispatchPacket(PacketId id, std::span<const uint8_t> body, Handler&& handler) {
    switch (id) {
    case PacketId::LoginRequest: { auto v = memorypack::Deserialize<LoginRequest>(body); handler(v); return true; }
    case PacketId::LoginResponse: { auto v = memorypack::Deserialize<LoginResponse>(body); handler(v); return true; }
    case PacketId::RoomJoinRequest: { auto v = memorypack::Deserialize<RoomJoinRequest>(body); handler(v); return true; }
    case PacketId::RoomJoinResponse: { auto v = memorypack::Deserialize<RoomJoinResponse>(body); handler(v); return true; }
    case PacketId::RoomChat: { auto v = memorypack::Deserialize<RoomChat>(body); handler(v); return true; }
    case PacketId::PrivateChat: { auto v = memorypack::Deserialize<PrivateChat>(body); handler(v); return true; }
    case PacketId::UserEntered: { auto v = memorypack::Deserialize<UserEntered>(body); handler(v); return true; }
    case PacketId::UserLeft: { auto v = memorypack::Deserialize<UserLeft>(body); handler(v); return true; }
    default: return false;
    }
}

// ── Schema hash ────────────────────────────────────────────────────────────────
// FNV-1a 64bit over "Type(0:CsType;1:CsType;...)\n" for every packet.
inline constexpr uint64_t PACKET_SCHEMA_HASH = 0xE3D920B3AD21BFE8ULL;

