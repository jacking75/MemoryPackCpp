#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <span>
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

// ── Serializer definitions ─────────────────────────────────────────────────────
MEMORYPACK_DEFINE(LoginRequest, username, level)
MEMORYPACK_DEFINE(LoginResponse, success, playerId)

// ── Packet dispatch table ──────────────────────────────────────────────────────
template<typename Handler>
bool DispatchPacket(PacketId id, std::span<const uint8_t> body, Handler&& handler) {
    switch (id) {
    case PacketId::LoginRequest: { auto v = memorypack::Deserialize<LoginRequest>(body); handler(v); return true; }
    case PacketId::LoginResponse: { auto v = memorypack::Deserialize<LoginResponse>(body); handler(v); return true; }
    default: return false;
    }
}

// ── Schema hash ────────────────────────────────────────────────────────────────
// FNV-1a 64bit over "Type(0:CsType;1:CsType;...)\n" for every packet.
inline constexpr uint64_t PACKET_SCHEMA_HASH = 0xA7CEC854C3062379ULL;

