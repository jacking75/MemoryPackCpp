#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// ── Packet Structs ─────────────────────────────────────────────────────────────
// Member order MUST match C# [MemoryPackable] declaration order.

struct Item {
    int32_t     itemId = 0;
    std::string itemName;
    int32_t     count = 0;
};

struct Inventory {
    int32_t             ownerId = 0;
    std::optional<Item> equipped;
    std::vector<Item>   items;
};

// ── Serializer definitions ─────────────────────────────────────────────────────
MEMORYPACK_DEFINE(Item, itemId, itemName, count)
MEMORYPACK_DEFINE(Inventory, ownerId, equipped, items)

