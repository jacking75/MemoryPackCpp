#pragma once
#include "memorypack/memorypack.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// ── Packet Structs ─────────────────────────────────────────────────────────────
// Member order MUST match C# [MemoryPackable] declaration order.

struct CollectionPacket {
    std::map<int32_t, std::string>    scores;
    std::map<std::string, int32_t>    lookup;
    std::set<int32_t>                 tags;
    std::pair<int32_t, std::string>   head = {};
    std::vector<std::vector<int32_t>> rows;
    std::optional<int32_t>            maybeInt;
    std::optional<float>              maybeFloat;
};

struct DotNetPacket {
    memorypack::Guid           id;
    memorypack::DateTime       createdAt;
    memorypack::TimeSpan       duration;
    memorypack::DateTimeOffset offset;
    memorypack::Decimal        price;
    memorypack::Half           ratio;
    memorypack::Int128         big;
    memorypack::UInt128        uBig;
    char16_t                   initial = 0;
};

// ── Serializer definitions ─────────────────────────────────────────────────────
MEMORYPACK_DEFINE(CollectionPacket, scores, lookup, tags, head, rows, maybeInt, maybeFloat)
MEMORYPACK_DEFINE(DotNetPacket, id, createdAt, duration, offset, price, ratio, big, uBig, initial)

