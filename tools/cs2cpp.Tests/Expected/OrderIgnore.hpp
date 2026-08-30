#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <string>

// ── Packet Structs ─────────────────────────────────────────────────────────────
// Member order MUST match C# [MemoryPackable] declaration order.

struct OrderedPacket {
    int32_t     first = 0;
    int32_t     second = 0;
    std::string trailing;
};

// ── Serializer definitions ─────────────────────────────────────────────────────
MEMORYPACK_DEFINE(OrderedPacket, first, second, trailing)

