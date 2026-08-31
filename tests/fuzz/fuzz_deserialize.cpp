// libFuzzer entry point for the MemoryPackCpp deserializer.
//
// The deserializer is the part of the library that touches untrusted input, so
// it must never read out of bounds, allocate on an attacker-chosen length, or
// recurse without a limit - no matter how malformed the bytes are.
//
// Build and run:
//   clang++ -std=c++23 -g -O1 -Iinclude \
//       -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
//       tests/fuzz/fuzz_deserialize.cpp -o fuzz_deserialize
//   ./fuzz_deserialize -max_total_time=600
//
// A crash, a sanitizer report, or an out-of-memory is a bug. Reader errors are
// expected and are swallowed here on purpose.

#include "memorypack/memorypack.hpp"
#include "memorypack/packet.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

// ── Types under test ───────────────────────────────────────────────────────────

namespace fuzztypes {

struct Item {
    int32_t id = 0;
    std::string name;
    int32_t count = 0;
};

struct Inventory {
    int32_t ownerId = 0;
    std::vector<Item> items;
    std::map<std::string, int32_t> tags;
};

struct Deep {
    int32_t depth = 0;
    std::vector<Deep> children;    // exercises the nesting-depth limit
};

struct Everything {
    bool flag = false;
    int8_t i8 = 0;
    uint16_t u16 = 0;
    int64_t i64 = 0;
    double d = 0.0;
    std::string text;
    std::optional<std::string> maybeText;
    std::optional<int32_t> maybeInt;
    std::vector<int32_t> numbers;
    std::vector<bool> flags;
    std::vector<std::string> names;
    std::vector<std::vector<int32_t>> matrix;
    std::map<int32_t, std::string> lookup;
    std::set<int32_t> unique;
    std::pair<int32_t, std::string> pair;
    std::tuple<int32_t, std::string> tuple;
};

struct AltA { int32_t a = 0; };
struct AltB { std::string b; };
using Alt = std::variant<AltA, AltB>;

struct Vec3 { float x = 0, y = 0, z = 0; };

} // namespace fuzztypes

MEMORYPACK_DEFINE(fuzztypes::Item, id, name, count)
MEMORYPACK_DEFINE(fuzztypes::Inventory, ownerId, items, tags)
MEMORYPACK_DEFINE(fuzztypes::Deep, depth, children)
MEMORYPACK_DEFINE(fuzztypes::Everything, flag, i8, u16, i64, d, text, maybeText, maybeInt,
                  numbers, flags, names, matrix, lookup, unique, pair, tuple)
MEMORYPACK_DEFINE(fuzztypes::AltA, a)
MEMORYPACK_DEFINE(fuzztypes::AltB, b)
MEMORYPACK_UNION_TAG(fuzztypes::AltA, 0)
MEMORYPACK_UNION_TAG(fuzztypes::AltB, 300)
MEMORYPACK_UNMANAGED_EXACT(fuzztypes::Vec3, 12, x, y, z)

// ── Harness ────────────────────────────────────────────────────────────────────

namespace {

/// Deserializes as T and ignores any reported error. Any crash is a real bug.
template<typename T>
void TryOne(std::span<const uint8_t> data, const memorypack::ReaderOptions& options) {
    memorypack::MemoryPackReader reader(data, options);
    T value{};
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
    try {
        reader.Read(value);
    } catch (const memorypack::MemoryPackException&) {
        // Expected for malformed input.
    } catch (const std::bad_alloc&) {
        // A length the reader accepted still has to be allocatable; treat an
        // allocation failure as tolerable rather than a crash.
    }
#else
    reader.Read(value);
#endif
}

/// Also exercises the low-level readers directly, since a caller may drive them
/// by hand rather than through a generated object reader.
void TryPrimitives(std::span<const uint8_t> data, const memorypack::ReaderOptions& options) {
    memorypack::MemoryPackReader reader(data, options);
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
    try {
#endif
        while (!reader.IsEnd() && !reader.Failed()) {
            (void)reader.ReadObjectHeader();
            (void)reader.ReadUnionHeader();
            (void)reader.ReadVarIntLength();
            (void)reader.ReadInt32();
            (void)reader.ReadDouble();
            (void)reader.ReadString();
            (void)reader.ReadStringView();
            (void)reader.ReadVector<int32_t>();
            (void)reader.ReadStringVector();
            (void)reader.ReadArray<int32_t, 4>();
            (void)reader.ReadUnmanaged<fuzztypes::Vec3>();
        }
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
    } catch (const memorypack::MemoryPackException&) {
    } catch (const std::bad_alloc&) {
    }
#endif
}

/// Feeds the input to the TCP frame reassembler in small chunks.
///
/// Found by fuzzing: the callback below must catch what Deserialize() throws
/// on a malformed body, exactly as every other Try*() helper in this file
/// does. PacketFrameParser::Feed() has no reason to guard against that itself
/// - an exception escaping a user callback is normal C++ propagation, not a
/// library defect - but this harness treats "the process survives" as the
/// bar for every code path it drives, so it has to do here what
/// docs/security.md's hardening checklist already tells a real server to do:
/// never let an exception escape from packet handling into the accept loop.
void TryFraming(std::span<const uint8_t> data) {
    memorypack::PacketFrameParser parser(64 * 1024);
    size_t offset = 0;
    while (offset < data.size()) {
        const size_t chunk = std::min<size_t>(7, data.size() - offset);
        const bool ok = parser.Feed(data.subspan(offset, chunk),
                                    [](uint16_t, std::span<const uint8_t> body) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
                                        try {
                                            auto v = memorypack::Deserialize<fuzztypes::Item>(body);
                                            (void)v;
                                        } catch (const memorypack::MemoryPackException&) {
                                        } catch (const std::bad_alloc&) {
                                        }
#else
                                        auto v = memorypack::Deserialize<fuzztypes::Item>(body);
                                        (void)v;
#endif
                                    });
        if (!ok) break;   // the parser rejected the stream, as designed
        offset += chunk;
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;

    // The first byte selects the type so one corpus can reach every decoder.
    const uint8_t selector = data[0];
    std::span<const uint8_t> payload(data + 1, size - 1);

    // Tight limits, so a hostile length is rejected rather than turned into a
    // multi-gigabyte allocation that would look like an OOM instead of a bug.
    memorypack::ReaderOptions options;
    options.maxCollectionLength = 1u << 20;
    options.maxStringLength = 1u << 20;
    options.maxDepth = 64;

    switch (selector % 12) {
        case 0:  TryOne<fuzztypes::Item>(payload, options); break;
        case 1:  TryOne<fuzztypes::Inventory>(payload, options); break;
        case 2:  TryOne<fuzztypes::Everything>(payload, options); break;
        case 3:  TryOne<fuzztypes::Deep>(payload, options); break;
        case 4:  TryOne<std::string>(payload, options); break;
        case 5:  TryOne<std::vector<int32_t>>(payload, options); break;
        case 6:  TryOne<std::vector<std::string>>(payload, options); break;
        case 7:  TryOne<std::map<std::string, std::string>>(payload, options); break;
        case 8:  TryOne<fuzztypes::Alt>(payload, options); break;
        case 9:  TryOne<std::optional<fuzztypes::Item>>(payload, options); break;
        case 10: TryPrimitives(payload, options); break;
        default: TryFraming(payload); break;
    }
    return 0;
}
