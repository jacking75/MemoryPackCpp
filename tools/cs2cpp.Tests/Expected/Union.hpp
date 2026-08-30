#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <optional>
#include <variant>

// ── Packet Structs ─────────────────────────────────────────────────────────────
// Member order MUST match C# [MemoryPackable] declaration order.

struct CircleShape {
    float radius = 0.f;
};

struct RectShape {
    float width = 0.f;
    float height = 0.f;
};

struct WideShape {
    int32_t sides = 0;
};

using IShape = std::variant<CircleShape, RectShape, WideShape>;

struct UnionHolder {
    std::optional<IShape> shape;
};

// ── Serializer definitions ─────────────────────────────────────────────────────
MEMORYPACK_UNION_TAG(CircleShape, 0)
MEMORYPACK_UNION_TAG(RectShape, 1)
MEMORYPACK_UNION_TAG(WideShape, 300)

MEMORYPACK_DEFINE(CircleShape, radius)
MEMORYPACK_DEFINE(RectShape, width, height)
MEMORYPACK_DEFINE(WideShape, sides)
MEMORYPACK_DEFINE(UnionHolder, shape)

