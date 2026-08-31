// =============================================================================
// memorypack_bench.cpp - Google Benchmark suite for MemoryPackCpp
// =============================================================================
//
// Build (always optimized - see docs/benchmarks.md):
//
//   cmake -B build-bench -DMEMORYPACK_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
//   cmake --build build-bench --config Release
//   ./build-bench/benchmarks/memorypack_bench
//
// Every benchmark reports throughput via SetBytesProcessed(), so the primary
// number to compare is bytes/s. The wall-clock ns/op only makes sense next to
// the payload size, which each benchmark also exposes as a "payload_bytes"
// counter.
//
// Conventions used throughout:
//   * benchmark::DoNotOptimize() on every result, so nothing is dead-code
//     eliminated. For writes into a buffer, ClobberMemory() additionally forces
//     the stores to be considered observable.
//   * Sample data is built once, outside the timed loop.
//   * Where a benchmark deliberately reuses a buffer, the comment says so - the
//     difference between "allocates" and "reuses" is the point of section 2.
//
// ASCII-only source on purpose (see the /utf-8 note in CMakeLists.txt).

#include "memorypack/memorypack.hpp"
#include "memorypack/packet.hpp"

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// =============================================================================
// Benchmark payload types
//
// MEMORYPACK_DEFINE / MEMORYPACK_UNMANAGED* open `namespace memorypack`, so they
// (and therefore the types they refer to) must live at global scope.
// =============================================================================

/// The classic "small hot packet": a handful of scalars plus a short name.
/// Wire layout: [1B count=5][4B id][4B x][4B y][4B z][string name] = 32 bytes.
struct PlayerState {
    int32_t id;
    float x;
    float y;
    float z;
    std::string name;
};
MEMORYPACK_DEFINE(PlayerState, id, x, y, z, name)

/// String-heavy packet: one scalar and two variable-length UTF-8 strings.
struct ChatMessage {
    int64_t timestamp;
    std::string sender;
    std::string body;
};
MEMORYPACK_DEFINE(ChatMessage, timestamp, sender, body)

/// Element of a nested-object collection.
struct Item {
    int32_t id;
    std::string name;
    int32_t count;
};
MEMORYPACK_DEFINE(Item, id, name, count)

/// Object containing a collection of objects - the nested-object case.
struct ItemList {
    std::vector<Item> items;
};
MEMORYPACK_DEFINE(ItemList, items)

/// Layout-compatible with a C# unmanaged struct: serialized as a verbatim
/// memcpy, with no per-element object header. All-float, so
/// MEMORYPACK_UNMANAGED_EXACT's no-padding proof is free here.
struct Vec3 {
    float x;
    float y;
    float z;
};
MEMORYPACK_UNMANAGED_EXACT(Vec3, 12, x, y, z)

/// Byte-for-byte the same C++ layout as Vec3, but registered through the
/// generic object path instead. Existing only so section 7 can measure what the
/// unmanaged bulk path actually buys.
struct Vec3Generic {
    float x;
    float y;
    float z;
};
MEMORYPACK_DEFINE(Vec3Generic, x, y, z)

/// Byte-for-byte the same layout as Vec3, but registered through
/// MEMORYPACK_UNMANAGED_SCRUBBED instead. Exists so section 7b can measure the
/// cost of the copy-through-a-value-initialized-temporary mechanism in
/// isolation - Vec3 has no actual padding to zero, so any difference against
/// Vec3 is purely the per-member-copy overhead, not padding-clearing work.
struct Vec3Scrubbed {
    float x;
    float y;
    float z;
};
MEMORYPACK_UNMANAGED_SCRUBBED(Vec3Scrubbed, 12, x, y, z)

namespace {

// =============================================================================
// Shared helpers
// =============================================================================

/// Buffer size used by the fixed-buffer benchmarks. The small packet is 32
/// bytes on the wire; 256 leaves plenty of head room without leaving the stack.
constexpr size_t kFixedBufferSize = 256;

/// Element count for the nested-object benchmarks.
constexpr int32_t kItemCount = 100;

/// Element count for the unmanaged-struct benchmarks.
constexpr int32_t kVec3Count = 4096;

/// Packet id used by the framing benchmark.
constexpr uint16_t kPlayerStatePacketId = 1001;

/// Reports throughput plus the payload size the throughput was derived from.
/// Google Benchmark turns bytes_processed into the "bytes/s" column.
void SetThroughput(benchmark::State& state, size_t payloadBytes) {
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(payloadBytes));
    state.counters["payload_bytes"] = static_cast<double>(payloadBytes);
}

PlayerState MakePlayerState() {
    return PlayerState{42, 1.5f, -2.25f, 100.75f, "Player1"};
}

ChatMessage MakeChatMessage() {
    return ChatMessage{
        1700000000000LL,
        "player_1234",
        // ~60 characters, which is a realistic single chat line.
        "Heading to the north gate now, bring potions and a spare key."};
}

std::vector<int32_t> MakeIntVector(size_t count) {
    std::vector<int32_t> values(count);
    for (size_t i = 0; i < count; ++i) {
        values[i] = static_cast<int32_t>(i);
    }
    return values;
}

ItemList MakeItemList(int32_t count) {
    ItemList list;
    list.items.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i) {
        list.items.push_back(Item{i, "item_" + std::to_string(i), i % 7 + 1});
    }
    return list;
}

template<typename T>
std::vector<T> MakeVec3s(int32_t count) {
    std::vector<T> values;
    values.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i) {
        const auto f = static_cast<float>(i);
        values.push_back(T{f, f * 2.0f, f * 3.0f});
    }
    return values;
}

// =============================================================================
// 1. Small packet: baseline serialize / deserialize
//
// Question: what does one round of the common case cost end to end?
// =============================================================================

void BM_Serialize_SmallPacket(benchmark::State& state) {
    const PlayerState value = MakePlayerState();
    const size_t payloadSize = memorypack::Serialize(value).size();

    for (auto _ : state) {
        // Allocating form: a fresh vector per call, like most first-draft code.
        std::vector<uint8_t> bytes = memorypack::Serialize(value);
        benchmark::DoNotOptimize(bytes.data());
        benchmark::ClobberMemory();
    }

    SetThroughput(state, payloadSize);
}
BENCHMARK(BM_Serialize_SmallPacket);

void BM_Deserialize_SmallPacket(benchmark::State& state) {
    const std::vector<uint8_t> bytes = memorypack::Serialize(MakePlayerState());

    for (auto _ : state) {
        PlayerState out{};
        memorypack::Deserialize(bytes.data(), bytes.size(), out);
        benchmark::DoNotOptimize(out);
    }

    SetThroughput(state, bytes.size());
}
BENCHMARK(BM_Deserialize_SmallPacket);

// =============================================================================
// 2. Buffer modes for the same small packet
//
// Question: how much of the "serialization cost" is really the allocator?
// All five benchmarks move the same 32 bytes; only the buffer strategy differs,
// with std::memcpy as the hardware floor.
// =============================================================================

/// Mode A: fresh buffer per call - one heap allocation per iteration.
void BM_Serialize_OwnedBuffer(benchmark::State& state) {
    const PlayerState value = MakePlayerState();
    const size_t payloadSize = memorypack::Serialize(value).size();

    for (auto _ : state) {
        std::vector<uint8_t> bytes = memorypack::Serialize(value);
        benchmark::DoNotOptimize(bytes.data());
        benchmark::ClobberMemory();
    }

    SetThroughput(state, payloadSize);
}
BENCHMARK(BM_Serialize_OwnedBuffer);

/// Mode B: one writer kept alive, rewound with Clear() - zero allocations after
/// the first iteration. This is the recommended per-connection send pattern.
void BM_Serialize_ReusedWriter(benchmark::State& state) {
    const PlayerState value = MakePlayerState();
    const size_t payloadSize = memorypack::Serialize(value).size();

    memorypack::MemoryPackWriter writer;
    writer.Reserve(kFixedBufferSize);

    for (auto _ : state) {
        writer.Clear();
        writer.Write(value);
        const size_t written = writer.Size();
        benchmark::DoNotOptimize(written);
        benchmark::ClobberMemory();
    }

    SetThroughput(state, payloadSize);
}
BENCHMARK(BM_Serialize_ReusedWriter);

/// Mode C: caller-owned std::vector reused across iterations. Same idea as mode
/// B, but the buffer outlives the writer - the shape most send queues have.
void BM_Serialize_ExternalVector(benchmark::State& state) {
    const PlayerState value = MakePlayerState();
    const size_t payloadSize = memorypack::Serialize(value).size();

    std::vector<uint8_t> buffer;
    buffer.reserve(kFixedBufferSize);

    for (auto _ : state) {
        buffer.clear();
        memorypack::Serialize(value, buffer);   // appends into `buffer`
        benchmark::DoNotOptimize(buffer.data());
        benchmark::ClobberMemory();
    }

    SetThroughput(state, payloadSize);
}
BENCHMARK(BM_Serialize_ExternalVector);

/// Mode D: fixed stack buffer - no heap involvement at all.
void BM_Serialize_FixedBuffer(benchmark::State& state) {
    const PlayerState value = MakePlayerState();
    const size_t payloadSize = memorypack::Serialize(value).size();

    std::array<uint8_t, kFixedBufferSize> buffer{};

    for (auto _ : state) {
        const size_t written =
            memorypack::SerializeTo(std::span<uint8_t>(buffer), value);
        benchmark::DoNotOptimize(written);
        benchmark::ClobberMemory();
    }

    SetThroughput(state, payloadSize);
}
BENCHMARK(BM_Serialize_FixedBuffer);

/// The floor: raw std::memcpy of exactly as many bytes as the packet occupies.
/// Nothing above this line can be faster, and how close a serializer gets is
/// the only honest way to read the other numbers in this section.
void BM_Memcpy_Baseline(benchmark::State& state) {
    const std::vector<uint8_t> source = memorypack::Serialize(MakePlayerState());
    const size_t payloadSize = source.size();

    std::array<uint8_t, kFixedBufferSize> destination{};

    for (auto _ : state) {
        std::memcpy(destination.data(), source.data(), payloadSize);
        benchmark::DoNotOptimize(destination.data());
        benchmark::ClobberMemory();
    }

    SetThroughput(state, payloadSize);
}
BENCHMARK(BM_Memcpy_Baseline);

// =============================================================================
// 3. Bulk primitives: std::vector<int32_t>
//
// Question: does the arithmetic-vector path stay a bulk copy as N grows?
// 1024 elements (4 KB) fits comfortably in L1; 65536 (256 KB) does not, so the
// pair also shows where memory bandwidth starts to dominate.
// =============================================================================

void BM_Serialize_IntVector(benchmark::State& state) {
    const std::vector<int32_t> values =
        MakeIntVector(static_cast<size_t>(state.range(0)));
    const size_t payloadSize = memorypack::Serialize(values).size();

    // Buffer reused on purpose: this benchmark is about the bulk-copy path, not
    // about the allocator (section 2 covers that).
    std::vector<uint8_t> buffer;
    buffer.reserve(payloadSize);

    for (auto _ : state) {
        buffer.clear();
        memorypack::Serialize(values, buffer);
        benchmark::DoNotOptimize(buffer.data());
        benchmark::ClobberMemory();
    }

    SetThroughput(state, payloadSize);
}
BENCHMARK(BM_Serialize_IntVector)->Arg(1024)->Arg(65536);

void BM_Deserialize_IntVector(benchmark::State& state) {
    const std::vector<uint8_t> bytes =
        memorypack::Serialize(MakeIntVector(static_cast<size_t>(state.range(0))));

    for (auto _ : state) {
        // Returning form: a new vector - and therefore a new allocation - per call.
        std::vector<int32_t> out =
            memorypack::Deserialize<std::vector<int32_t>>(bytes);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }

    SetThroughput(state, bytes.size());
}
BENCHMARK(BM_Deserialize_IntVector)->Arg(1024)->Arg(65536);

// =============================================================================
// 4. In-place bulk read
//
// Question: what does reusing the destination vector save over the returning
// form above? Same bytes, same decode - only the allocation differs.
// =============================================================================

void BM_Deserialize_IntVector_InPlace(benchmark::State& state) {
    const std::vector<uint8_t> bytes =
        memorypack::Serialize(MakeIntVector(static_cast<size_t>(state.range(0))));

    std::vector<int32_t> out;
    out.reserve(static_cast<size_t>(state.range(0)));

    for (auto _ : state) {
        memorypack::MemoryPackReader reader{std::span<const uint8_t>(bytes)};
        reader.ReadVector(out);   // assigns into the existing capacity
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }

    SetThroughput(state, bytes.size());
}
BENCHMARK(BM_Deserialize_IntVector_InPlace)->Arg(1024)->Arg(65536);

// =============================================================================
// 5. String-heavy payload: chat message
//
// Question: what do variable-length UTF-8 strings cost, and how much of that is
// just building std::string? The StringView variant answers the second half by
// borrowing the input buffer instead of copying out of it.
// =============================================================================

void BM_Serialize_ChatMessage(benchmark::State& state) {
    const ChatMessage value = MakeChatMessage();
    const size_t payloadSize = memorypack::Serialize(value).size();

    std::vector<uint8_t> buffer;
    buffer.reserve(kFixedBufferSize);

    for (auto _ : state) {
        buffer.clear();
        memorypack::Serialize(value, buffer);
        benchmark::DoNotOptimize(buffer.data());
        benchmark::ClobberMemory();
    }

    SetThroughput(state, payloadSize);
}
BENCHMARK(BM_Serialize_ChatMessage);

void BM_Deserialize_ChatMessage(benchmark::State& state) {
    const std::vector<uint8_t> bytes = memorypack::Serialize(MakeChatMessage());

    for (auto _ : state) {
        // Owning form: both strings are copied into freshly built std::strings.
        ChatMessage out{};
        memorypack::Deserialize(bytes.data(), bytes.size(), out);
        benchmark::DoNotOptimize(out);
    }

    SetThroughput(state, bytes.size());
}
BENCHMARK(BM_Deserialize_ChatMessage);

void BM_Deserialize_ChatMessage_StringView(benchmark::State& state) {
    const std::vector<uint8_t> bytes = memorypack::Serialize(MakeChatMessage());

    for (auto _ : state) {
        // Zero-copy form: the views borrow `bytes`, so nothing is allocated.
        // Only valid while the input buffer outlives the views.
        memorypack::MemoryPackReader reader{std::span<const uint8_t>(bytes)};
        const memorypack::ObjectHeader header = reader.ReadObjectHeader();
        benchmark::DoNotOptimize(header);

        const int64_t timestamp = reader.ReadInt64();
        benchmark::DoNotOptimize(timestamp);

        std::optional<std::string_view> sender = reader.ReadStringView();
        std::optional<std::string_view> body = reader.ReadStringView();
        benchmark::DoNotOptimize(sender);
        benchmark::DoNotOptimize(body);
    }

    SetThroughput(state, bytes.size());
}
BENCHMARK(BM_Deserialize_ChatMessage_StringView);

// =============================================================================
// 6. Nested objects: 100 items inside one object
//
// Question: what is the per-element overhead of the generic object path, where
// every element carries its own 1-byte member-count header and its own string?
// =============================================================================

void BM_Serialize_ItemList(benchmark::State& state) {
    const ItemList value = MakeItemList(kItemCount);
    const size_t payloadSize = memorypack::Serialize(value).size();

    std::vector<uint8_t> buffer;
    buffer.reserve(payloadSize);

    for (auto _ : state) {
        buffer.clear();
        memorypack::Serialize(value, buffer);
        benchmark::DoNotOptimize(buffer.data());
        benchmark::ClobberMemory();
    }

    SetThroughput(state, payloadSize);
}
BENCHMARK(BM_Serialize_ItemList);

void BM_Deserialize_ItemList(benchmark::State& state) {
    const std::vector<uint8_t> bytes =
        memorypack::Serialize(MakeItemList(kItemCount));

    for (auto _ : state) {
        ItemList out{};
        memorypack::Deserialize(bytes.data(), bytes.size(), out);
        benchmark::DoNotOptimize(out);
    }

    SetThroughput(state, bytes.size());
}
BENCHMARK(BM_Deserialize_ItemList);

// =============================================================================
// 7. Unmanaged struct bulk path vs. the generic per-element path
//
// Question: how much does MEMORYPACK_UNMANAGED + Write/ReadUnmanagedCollection
// buy over walking a std::vector element by element?
//
// Vec3 and Vec3Generic have identical C++ layouts. The bulk path emits
// [4B count][count * 12B] and copies it in one memcpy; the generic path emits
// [4B count][count * (1B header + 12B)] and touches every element. Both the
// byte count and the instruction count differ, which is exactly the point.
// =============================================================================

void BM_Serialize_UnmanagedCollection(benchmark::State& state) {
    const std::vector<Vec3> values = MakeVec3s<Vec3>(kVec3Count);

    memorypack::MemoryPackWriter probe;
    probe.WriteUnmanagedCollection(std::span<const Vec3>(values));
    const size_t payloadSize = probe.Size();

    memorypack::MemoryPackWriter writer;
    writer.Reserve(payloadSize);

    for (auto _ : state) {
        writer.Clear();
        writer.WriteUnmanagedCollection(std::span<const Vec3>(values));
        const size_t written = writer.Size();
        benchmark::DoNotOptimize(written);
        benchmark::ClobberMemory();
    }

    SetThroughput(state, payloadSize);
}
BENCHMARK(BM_Serialize_UnmanagedCollection);

void BM_Deserialize_UnmanagedCollection(benchmark::State& state) {
    const std::vector<Vec3> values = MakeVec3s<Vec3>(kVec3Count);

    memorypack::MemoryPackWriter writer;
    writer.WriteUnmanagedCollection(std::span<const Vec3>(values));
    const std::vector<uint8_t> bytes = writer.TakeBuffer();

    std::vector<Vec3> out;
    out.reserve(values.size());

    for (auto _ : state) {
        memorypack::MemoryPackReader reader{std::span<const uint8_t>(bytes)};
        reader.ReadUnmanagedCollection(out);   // one memcpy for the whole array
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }

    SetThroughput(state, bytes.size());
}
BENCHMARK(BM_Deserialize_UnmanagedCollection);

void BM_Serialize_GenericCollection(benchmark::State& state) {
    const std::vector<Vec3Generic> values = MakeVec3s<Vec3Generic>(kVec3Count);
    const size_t payloadSize = memorypack::Serialize(values).size();

    memorypack::MemoryPackWriter writer;
    writer.Reserve(payloadSize);

    for (auto _ : state) {
        writer.Clear();
        writer.Write(values);   // per-element object path
        const size_t written = writer.Size();
        benchmark::DoNotOptimize(written);
        benchmark::ClobberMemory();
    }

    SetThroughput(state, payloadSize);
}
BENCHMARK(BM_Serialize_GenericCollection);

void BM_Deserialize_GenericCollection(benchmark::State& state) {
    const std::vector<uint8_t> bytes =
        memorypack::Serialize(MakeVec3s<Vec3Generic>(kVec3Count));

    std::vector<Vec3Generic> out;
    out.reserve(static_cast<size_t>(kVec3Count));

    for (auto _ : state) {
        memorypack::MemoryPackReader reader{std::span<const uint8_t>(bytes)};
        reader.ReadCollection(out);   // per-element object path
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }

    SetThroughput(state, bytes.size());
}
BENCHMARK(BM_Deserialize_GenericCollection);

// =============================================================================
// 7b. MEMORYPACK_UNMANAGED_EXACT vs MEMORYPACK_UNMANAGED_SCRUBBED
//
// Question: what does _SCRUBBED's padding safety (a value-initialized
// temporary plus a per-member copy in Serialize) cost compared to the plain
// unmanaged memcpy? Both benchmarks go through the same generic per-element
// vector path (Write(item) per element - Vec3 and Vec3Scrubbed are both
// unmanaged, so neither gets a per-element object header), so the only
// difference measured is what each type's Serialize() does.
// =============================================================================

void BM_Serialize_UnmanagedExact_PerElement(benchmark::State& state) {
    const std::vector<Vec3> values = MakeVec3s<Vec3>(kVec3Count);
    const size_t payloadSize = memorypack::Serialize(values).size();

    memorypack::MemoryPackWriter writer;
    writer.Reserve(payloadSize);

    for (auto _ : state) {
        writer.Clear();
        writer.Write(values);   // per-element: MemoryPackFormatter<Vec3>::Serialize
        const size_t written = writer.Size();
        benchmark::DoNotOptimize(written);
        benchmark::ClobberMemory();
    }

    SetThroughput(state, payloadSize);
}
BENCHMARK(BM_Serialize_UnmanagedExact_PerElement);

void BM_Serialize_UnmanagedScrubbed_PerElement(benchmark::State& state) {
    const std::vector<Vec3Scrubbed> values = MakeVec3s<Vec3Scrubbed>(kVec3Count);
    const size_t payloadSize = memorypack::Serialize(values).size();

    memorypack::MemoryPackWriter writer;
    writer.Reserve(payloadSize);

    for (auto _ : state) {
        writer.Clear();
        writer.Write(values);   // per-element: value-init temp + 3 member copies
        const size_t written = writer.Size();
        benchmark::DoNotOptimize(written);
        benchmark::ClobberMemory();
    }

    SetThroughput(state, payloadSize);
}
BENCHMARK(BM_Serialize_UnmanagedScrubbed_PerElement);

// =============================================================================
// 8. End-to-end packet framing
//
// Question: what does a full send/receive hop cost, including the
// [2B id][4B length] header, the stream reassembly buffer, and the decode?
//
// This is the only benchmark that keeps the allocation in the timed region on
// purpose: MakePacket() returns a fresh vector, which is what a naive send path
// does. The parser itself is reused across iterations, matching a long-lived
// connection - it consumes each frame completely, so its internal buffer is
// cleared (not regrown) every time.
// =============================================================================

void BM_PacketFraming_RoundTrip(benchmark::State& state) {
    const PlayerState value = MakePlayerState();
    const size_t payloadSize =
        memorypack::MakePacket(kPlayerStatePacketId, value).size();

    memorypack::PacketFrameParser parser;
    PlayerState decoded{};

    for (auto _ : state) {
        const std::vector<uint8_t> frame =
            memorypack::MakePacket(kPlayerStatePacketId, value);

        const bool ok = parser.Feed(
            std::span<const uint8_t>(frame),
            [&decoded](uint16_t id, std::span<const uint8_t> body) {
                benchmark::DoNotOptimize(id);
                memorypack::Deserialize(body.data(), body.size(), decoded);
            });

        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(decoded);
    }

    SetThroughput(state, payloadSize);
}
BENCHMARK(BM_PacketFraming_RoundTrip);

}   // namespace

// main() comes from benchmark::benchmark_main.
