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

// ── Serializer definitions ─────────────────────────────────────────────────────
MEMORYPACK_DEFINE(LoginRequest, username, level, remember, score)
MEMORYPACK_DEFINE(FieldPacket, id, name, ticks)
MEMORYPACK_DEFINE(RecordPacket, id, name)

