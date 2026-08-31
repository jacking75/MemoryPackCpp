#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// ── Packet Structs ─────────────────────────────────────────────────────────────
// Member order MUST match C# [MemoryPackable] declaration order.

struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

struct PaddedStruct {
    uint8_t tag = 0;
    int32_t value = 0;
};

#pragma pack(push, 1)
struct PackedStruct {
    uint8_t tag = 0;
    int32_t value = 0;
};
#pragma pack(pop)

struct ManagedStruct {
    int32_t     id = 0;
    std::string label;
};

struct UnmanagedHolder {
    Vec3                position;
    PaddedStruct        padded;
    PackedStruct        packed;
    std::optional<Vec3> maybeVec;
    std::vector<Vec3>   points;
    ManagedStruct       managed;
};

// ── Serializer definitions ─────────────────────────────────────────────────────
MEMORYPACK_UNMANAGED_SCRUBBED(Vec3, 12, x, y, z)
MEMORYPACK_UNMANAGED_SCRUBBED(PaddedStruct, 8, tag, value)
MEMORYPACK_UNMANAGED_EXACT(PackedStruct, 5, tag, value)

MEMORYPACK_DEFINE(ManagedStruct, id, label)
MEMORYPACK_DEFINE(UnmanagedHolder, position, padded, packed, maybeVec, points, managed)

