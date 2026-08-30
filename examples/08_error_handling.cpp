// examples/08_error_handling.cpp
// ============================================================================
// Surviving malformed and hostile input.
//
// A deserializer is the most exposed code in a server: every byte it looks at
// came from somewhere you do not control. MemoryPackCpp is written so that no
// input - truncated, corrupted, or deliberately crafted - can read past the end
// of the buffer or trigger an unbounded allocation. This example walks through
// the four layers of defence:
//
//   1. Bounds checking          every read is checked against the input length.
//   2. ReaderOptions            caps on collection length, string length and
//                               object nesting depth, so a WELL-FORMED but
//                               implausible payload is rejected by policy.
//   3. DeserializeExact         requires the whole input to be consumed, which
//                               catches C#/C++ member-order drift in dev.
//   4. std::expected            an allocation- and exception-free result type
//                               for the hot path.
//
// HOW ERRORS ARE REPORTED
//   * Default build: the reader/writer throws memorypack::MemoryPackException,
//     carrying a MemoryPackError code and the byte offset.
//   * MEMORYPACK_NO_EXCEPTIONS build (Unreal, some console toolchains): nothing
//     is thrown. The reader records the error, every subsequent read becomes a
//     no-op returning a zero value, and Failed()/Error() report it.
//
// Both paths set the same error state, so the code below is written to work in
// either build. That is also why the reader never returns garbage on failure:
// it returns a value-initialised result AND flags the error.
// ============================================================================

#include "memorypack/memorypack.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

struct Packet {
    int32_t     id = 0;
    std::string name;
};
MEMORYPACK_DEFINE(Packet, id, name)

// A two-level nest, to exercise the depth limit.
struct Inner  { int32_t v = 0; };
struct Outer  { Inner inner; };
MEMORYPACK_DEFINE(Inner, v)
MEMORYPACK_DEFINE(Outer, inner)

namespace {

void Dump(const char* label, std::span<const uint8_t> bytes) {
    std::printf("%s (%zu bytes)\n", label, bytes.size());
    for (size_t i = 0; i < bytes.size(); i += 16) {
        std::printf("    %04zx  ", i);
        size_t j = 0;
        for (; j < 16 && i + j < bytes.size(); ++j) std::printf("%02X ", bytes[i + j]);
        for (; j < 16; ++j) std::printf("   ");
        std::printf(" |");
        for (j = 0; j < 16 && i + j < bytes.size(); ++j) {
            const uint8_t c = bytes[i + j];
            std::printf("%c", (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.');
        }
        std::printf("|\n");
    }
}

bool g_ok = true;

void Check(const char* what, bool condition) {
    if (!condition) {
        g_ok = false;
        std::printf("  FAILED: %s\n", what);
    }
}

// Runs `attempt` (which must return the reader's error state) and prints how
// the failure surfaced. Written so the example behaves correctly whether or not
// exceptions are enabled.
template<typename Fn>
void ExpectFailure(const char* what, Fn&& attempt) {
    memorypack::MemoryPackError code = memorypack::MemoryPackError::None;
#if MEMORYPACK_HAS_EXCEPTIONS
    try {
        code = attempt();
        std::printf("  %-38s -> error state: %s\n", what, memorypack::ToString(code));
    } catch (const memorypack::MemoryPackException& e) {
        std::printf("  %-38s -> threw: %s\n", what, memorypack::ToString(e.code()));
        std::printf("  %-38s    at offset %zu\n", "", e.offset());
        Check(what, e.code() != memorypack::MemoryPackError::None);
        return;
    }
#else
    code = attempt();
    std::printf("  %-38s -> error state: %s\n", what, memorypack::ToString(code));
#endif
    Check(what, code != memorypack::MemoryPackError::None);
}

} // namespace

int main() {
    std::printf("== 08 error handling and hostile input ==\n\n");

    using memorypack::MemoryPackError;
    using memorypack::MemoryPackReader;
    using memorypack::ReaderOptions;

    // =======================================================================
    // 1. Bounds-checked reads.
    //
    //    Every primitive read goes through EnsureBytes(), which compares
    //    against the bytes ACTUALLY remaining - using subtraction, so a huge
    //    requested length cannot overflow the comparison itself. Reading past
    //    the end is BufferUnderflow, never a buffer overrun.
    // =======================================================================
    std::printf("--- 1. bounds-checked reads ---\n\n");
    {
        const std::vector<uint8_t> full = memorypack::Serialize(Packet{7, "hello"});
        Dump("a valid packet", full);
        std::printf("\n");

        // Chop it in half: the string header survives, the payload does not.
        const std::vector<uint8_t> truncated(full.begin(), full.begin() + 7);
        Dump("the same packet, truncated to 7 bytes", truncated);

        ExpectFailure("reading a truncated payload", [&] {
            MemoryPackReader reader(truncated);
            Packet value;
            reader.Read(value);
            return reader.Error();
        });

        ExpectFailure("reading past the end of an empty input", [] {
            const std::array<uint8_t, 0> nothing{};
            MemoryPackReader reader(std::span<const uint8_t>(nothing.data(), nothing.size()));
            (void)reader.ReadInt64();
            return reader.Error();
        });

        // A reserved object header value (250..254) is not a valid member
        // count, so it is rejected instead of being treated as 250 members.
        ExpectFailure("reserved object header value", [] {
            const std::array<uint8_t, 1> bad{0xFC};
            MemoryPackReader reader(std::span<const uint8_t>(bad.data(), bad.size()));
            (void)reader.ReadObjectHeader();
            return reader.Error();
        });

        // A length prefix is attacker controlled. Here it claims 2^31-1
        // elements while only four bytes remain. The reader compares the
        // declared length against the remaining input BEFORE allocating, so
        // this costs nothing.
        ExpectFailure("collection longer than the input", [] {
            const std::array<uint8_t, 8> hostile{0xFF, 0xFF, 0xFF, 0x7F, 0x01, 0x02, 0x03, 0x04};
            MemoryPackReader reader(std::span<const uint8_t>(hostile.data(), hostile.size()));
            (void)reader.ReadVector<int32_t>();
            return reader.Error();
        });
        std::printf("\n");
    }

    // =======================================================================
    // 2. ReaderOptions: policy limits on top of the structural checks.
    //
    //    The checks above reject what is IMPOSSIBLE. ReaderOptions rejects what
    //    is merely IMPLAUSIBLE for your protocol - a 10-million-element list, a
    //    4 MB name, a 200-deep object graph. Set them from what your protocol
    //    can legitimately produce and hostile payloads die at the front door.
    // =======================================================================
    std::printf("--- 2. ReaderOptions limits ---\n\n");
    {
        // maxCollectionLength -------------------------------------------------
        const std::vector<uint8_t> list = memorypack::Serialize(std::vector<int32_t>{1, 2, 3, 4, 5});

        ExpectFailure("5 elements, maxCollectionLength = 3", [&] {
            ReaderOptions strict;
            strict.maxCollectionLength = 3;
            MemoryPackReader reader(std::span<const uint8_t>(list), strict);
            (void)reader.ReadVector<int32_t>();
            return reader.Error();
        });

        // ...and the identical payload passes with a sane limit.
        {
            ReaderOptions relaxed;
            relaxed.maxCollectionLength = 1000;
            MemoryPackReader reader(std::span<const uint8_t>(list), relaxed);
            const auto values = reader.ReadVector<int32_t>();
            std::printf("  %-38s -> accepted, %zu elements\n",
                        "5 elements, maxCollectionLength = 1000", values.size());
            Check("legit payload still accepted", !reader.Failed() && values.size() == 5);
        }

        // maxStringLength -----------------------------------------------------
        const std::vector<uint8_t> text = memorypack::Serialize(std::string("abcdefghij"));
        ExpectFailure("10-byte string, maxStringLength = 4", [&] {
            ReaderOptions shortStrings;
            shortStrings.maxStringLength = 4;
            MemoryPackReader reader(std::span<const uint8_t>(text), shortStrings);
            (void)reader.ReadString();
            return reader.Error();
        });

        // maxDepth ------------------------------------------------------------
        // MEMORYPACK_DEFINE-generated readers call EnterObject()/LeaveObject(),
        // which is what makes the depth cap work. It matters for recursive or
        // self-referential graphs, where a crafted payload could otherwise
        // drive the reader into stack exhaustion.
        const std::vector<uint8_t> nested = memorypack::Serialize(Outer{Inner{42}});
        ExpectFailure("2 levels of nesting, maxDepth = 1", [&] {
            ReaderOptions shallow;
            shallow.maxDepth = 1;
            MemoryPackReader reader(std::span<const uint8_t>(nested), shallow);
            (void)reader.Read<Outer>();
            return reader.Error();
        });
        {
            ReaderOptions deepEnough;
            deepEnough.maxDepth = 2;
            MemoryPackReader reader(std::span<const uint8_t>(nested), deepEnough);
            const auto value = reader.Read<Outer>();
            std::printf("  %-38s -> accepted, inner.v = %d\n",
                        "2 levels of nesting, maxDepth = 2", value.inner.v);
            Check("nesting within the limit is accepted",
                  !reader.Failed() && value.inner.v == 42);
        }
        std::printf("\n");
    }

    // =======================================================================
    // 3. DeserializeExact: "and there had better be nothing left over".
    //
    //    Plain Deserialize is deliberately lenient - that leniency is what
    //    gives version tolerance (09_version_tolerance.cpp). The flip side is
    //    that a C++ struct which is missing a member, or has two members in the
    //    wrong order, can still "succeed" while quietly leaving bytes unread.
    //
    //    DeserializeExact additionally requires the input to be fully consumed,
    //    turning that class of bug into a loud TrailingBytes error. Use it in
    //    tests and development builds; use plain Deserialize in production
    //    where a newer peer may legitimately send more members.
    // =======================================================================
    std::printf("--- 3. DeserializeExact catches trailing bytes ---\n\n");
    {
        std::vector<uint8_t> bytes = memorypack::Serialize(Packet{1, "x"});
        bytes.push_back(uint8_t{0xAA});   // stray bytes, e.g. a member we forgot to read
        bytes.push_back(uint8_t{0xBB});

        // Lenient: succeeds, silently ignoring the leftovers.
        const auto lenient = memorypack::Deserialize<Packet>(bytes);
        std::printf("  Deserialize      -> id=%d name=\"%s\" (2 stray bytes ignored)\n",
                    lenient.id, lenient.name.c_str());
        Check("lenient deserialize succeeds", lenient.id == 1);

        // Strict: reports the leftovers.
        ExpectFailure("DeserializeExact with 2 stray bytes", [&] {
            MemoryPackReader reader(bytes);
            Packet value;
            reader.Read(value);
            reader.RequireEnd();          // exactly what DeserializeExact adds
            return reader.Error();
        });
        std::printf("\n");
    }

    // =======================================================================
    // 4. The std::expected API: no exceptions, no error state to remember to
    //    check, and the failure is impossible to ignore because the value is
    //    only reachable through the expected.
    //
    //    Guarded by MEMORYPACK_HAS_EXPECTED because <expected> is C++23 and not
    //    yet present in every standard library the project supports.
    // =======================================================================
    std::printf("--- 4. std::expected API ---\n\n");
#if MEMORYPACK_HAS_EXPECTED
    {
        // TryDeserialize: success
        const std::vector<uint8_t> good = memorypack::Serialize(Packet{4, "ok"});
        const auto parsed = memorypack::TryDeserialize<Packet>(good);
        if (parsed) {
            std::printf("  TryDeserialize(valid)     -> id=%d name=\"%s\"\n",
                        parsed->id, parsed->name.c_str());
        } else {
            std::printf("  TryDeserialize(valid)     -> UNEXPECTED failure: %s\n",
                        memorypack::ToString(parsed.error()));
        }
        Check("TryDeserialize succeeds on valid input", parsed.has_value());

        // TryDeserialize: failure. Note this does NOT throw even in a build
        // with exceptions enabled - TryDeserialize catches internally and
        // converts to std::unexpected.
        const std::vector<uint8_t> truncated{0x02, 0x01};
        const auto failed = memorypack::TryDeserialize<Packet>(truncated);
        std::printf("  TryDeserialize(truncated) -> %s\n",
                    failed ? "unexpectedly ok" : memorypack::ToString(failed.error()));
        Check("TryDeserialize reports truncation",
              !failed.has_value() && failed.error() == MemoryPackError::BufferUnderflow);

        // TrySerializeTo: the writer side, for fixed buffers.
        std::array<uint8_t, 4> tooSmall{};
        const auto overflowed = memorypack::TrySerializeTo(std::span<uint8_t>(tooSmall),
                                                           Packet{1, "far too long"});
        std::printf("  TrySerializeTo(4-byte buf)-> %s\n",
                    overflowed ? "unexpectedly ok" : memorypack::ToString(overflowed.error()));
        Check("TrySerializeTo reports overflow",
              !overflowed.has_value() && overflowed.error() == MemoryPackError::BufferOverflow);

        std::array<uint8_t, 64> roomy{};
        const auto written = memorypack::TrySerializeTo(std::span<uint8_t>(roomy),
                                                        Packet{1, "fits"});
        std::printf("  TrySerializeTo(64-byte buf)-> %zu bytes written\n",
                    written ? *written : size_t{0});
        Check("TrySerializeTo succeeds when it fits", written.has_value());
    }
#else
    std::printf("  <expected> is unavailable in this standard library;\n");
    std::printf("  use Failed()/Error() on the reader and writer instead.\n");
#endif

    std::printf("\nerror handling %s\n", g_ok ? "OK" : "MISMATCH");
    return g_ok ? 0 : 1;
}
