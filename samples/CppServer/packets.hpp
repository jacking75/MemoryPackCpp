#pragma once
// ============================================================================
// samples/CppServer/packets.hpp
//
// Protocol definition for the CppServer + CsClient pair.
//
// This is the mirror image of the CSharpServer + CppClient sample: here the
// SERVER is C++ and the CLIENT is C#, which is the shape a Unity project ends
// up with when the authoritative simulation runs in native code.
//
// The C# side of this protocol lives in samples/CsClient/Packets.cs. MemoryPack
// serializes by member POSITION and never writes member names, so the two files
// must declare the same members in the same order. Renaming a member is free;
// reordering or inserting one is a wire break.
//
// Every struct here is described with MEMORYPACK_DEFINE, which generates the
// memorypack::IMemoryPackable<T> specialization (object header + one Write/Read
// per member, in declaration order).
// ============================================================================

#include "memorypack/memorypack.hpp"
#include "memorypack/packet.hpp"

#include <cstdint>
#include <string>
#include <vector>

// -- Packet ids ---------------------------------------------------------------
// The first two bytes of every frame. Must match PacketId in Packets.cs.
enum class PacketId : uint16_t {
    EchoRequest   = 201,
    EchoResponse  = 202,
    SumRequest    = 203,
    SumResponse   = 204,
    SpawnRequest  = 205,
    SpawnResponse = 206,
};

// -- Echo: string round trip --------------------------------------------------
// The simplest shape: an int and a string in, the same plus a server timestamp
// back. Exercises MemoryPack's UTF-8 string encoding in both directions.

struct EchoRequest {
    int32_t     seq = 0;
    std::string text;
};

struct EchoResponse {
    int32_t     seq = 0;
    std::string text;             ///< the request text, upper-cased by the server
    int64_t     serverTimeTicks = 0;   ///< .NET DateTime ticks (100ns since year 1)
};

// -- Sum: a collection of primitives -----------------------------------------
// std::vector<int32_t> maps to C# List<int>: [4B count][count * 4B], bulk-copied
// on both sides because the element type is a fixed-size primitive.

struct SumRequest {
    std::vector<int32_t> values;
};

struct SumResponse {
    int64_t total = 0;
};

// -- Spawn: nested objects, an unmanaged struct, and a collection of objects ---
// Entity mixes all three interesting cases in one type:
//   int32_t            -> 4 little-endian bytes
//   memorypack::Vector3 -> a C# unmanaged struct (System.Numerics.Vector3),
//                          copied verbatim as 12 raw bytes with NO object header
//   std::string        -> MemoryPack's length-prefixed UTF-8 form
// SpawnResponse then carries a std::vector<Entity>, i.e. a collection whose
// elements each have their own object header.

struct Entity {
    int32_t             id = 0;
    memorypack::Vector3 position{};
    std::string         name;
};

struct SpawnRequest {
    int32_t count = 0;
};

struct SpawnResponse {
    std::vector<Entity> entities;
};

// -- Generated serializers ----------------------------------------------------
// MEMORYPACK_DEFINE must appear at global scope: it opens namespace memorypack
// internally to specialize IMemoryPackable<T>.

MEMORYPACK_DEFINE(EchoRequest, seq, text)
MEMORYPACK_DEFINE(EchoResponse, seq, text, serverTimeTicks)
MEMORYPACK_DEFINE(SumRequest, values)
MEMORYPACK_DEFINE(SumResponse, total)
MEMORYPACK_DEFINE(Entity, id, position, name)
MEMORYPACK_DEFINE(SpawnRequest, count)
MEMORYPACK_DEFINE(SpawnResponse, entities)
