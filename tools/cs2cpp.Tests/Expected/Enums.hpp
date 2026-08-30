#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <vector>

// ── Enums ──────────────────────────────────────────────────────────────────────
enum class Color : uint8_t {
    Red   = 0,
    Green = 1,
    Blue  = 10,
    Cyan  = 11,
};
enum class Signed : int16_t {
    Neg  = -2,
    Zero = 0,
    Pos  = 5,
};
enum class Wide : uint32_t {
    Small = 1,
    Big   = 65536,
};

// ── Packet Structs ─────────────────────────────────────────────────────────────
// Member order MUST match C# [MemoryPackable] declaration order.

struct EnumPacket {
    Color              color = {};
    Signed             signed_ = {};
    Wide               wide = {};
    std::vector<Color> palette;
};

// ── Serializer definitions ─────────────────────────────────────────────────────
MEMORYPACK_DEFINE(EnumPacket, color, signed_, wide, palette)

