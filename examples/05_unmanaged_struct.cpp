// examples/05_unmanaged_struct.cpp
// ============================================================================
// MEMORYPACK_UNMANAGED: mapping a C# unmanaged struct.
//
// In C#, a struct with no reference-type fields (no string, no array, no class)
// is "unmanaged". MemoryPack serializes such a value by copying its memory
// verbatim - NO object header, NO per-member framing, CLR padding bytes and all.
// That is what makes position updates and vertex buffers essentially free.
//
//     [MemoryPackable]
//     public partial struct Vec3 { public float X, Y, Z; }   // 12 bytes, no header
//
// C++ side: mark the mirror struct with MEMORYPACK_UNMANAGED(Type, ExpectedSize)
// at global scope. The macro
//   * asserts the type is trivially copyable,
//   * asserts sizeof(Type) == ExpectedSize,
//   * registers a formatter that memcpy's the value.
//
// ---------------------------------------------------------------------------
// WHY THE SIZE ASSERTION IS LOAD-BEARING
// ---------------------------------------------------------------------------
// Because the bytes are a raw memory image, the C++ layout has to match .NET's
// layout EXACTLY - and .NET lays out an unmanaged struct at each field's
// natural alignment, padding included, just like a default C++ struct on the
// same platform. So:
//
//     C#:  struct PaddedStat { byte Tier; int Score; }
//     .NET layout: [1B Tier][3B padding][4B Score] = 8 bytes, NOT 5.
//
// Do NOT reach for #pragma pack to "tidy that up": packing to 5 bytes would
// make the C++ struct disagree with every C# writer. Mirror the natural layout
// and let the padding exist.
//
// The ExpectedSize argument in MEMORYPACK_UNMANAGED is the tripwire. If someone
// later inserts a field, changes a type, or builds on a toolchain that lays the
// struct out differently, the static_assert fails at compile time instead of
// producing packets that decode into garbage at 3 a.m. Always pass the size
// that the C# side actually has (Marshal.SizeOf / Unsafe.SizeOf<T>()), never
// just whatever sizeof() happens to return locally.
//
// ---------------------------------------------------------------------------
// THREE MACROS, ONE QUESTION: DOES THIS TYPE HAVE PADDING?
// ---------------------------------------------------------------------------
// Plain MEMORYPACK_UNMANAGED (above) trusts the caller to value-initialize
// every instance so padding never leaks onto the wire - a documentation-only
// guarantee, demonstrated (and broken) with PaddedStat below. Two stricter
// variants turn that trust requirement into either a compile-time proof or a
// runtime guarantee, given the struct's member names:
//
//   MEMORYPACK_UNMANAGED_EXACT(Type, size, members...)
//     Compile error if Type has ANY padding (sum of member sizes != sizeof).
//     Use for a naturally packed type (all-float, like Vec3 below) or one
//     under [StructLayout(Pack = 1)]. Zero runtime cost - identical generated
//     code to MEMORYPACK_UNMANAGED, just a stronger compile-time check.
//
//   MEMORYPACK_UNMANAGED_SCRUBBED(Type, size, members...)
//     For a type that DOES have padding: every Serialize() copies the members
//     into a value-initialized temporary first, so the padding on the wire is
//     always zero, no matter how the caller's instance was constructed. Costs
//     one stack temporary and a per-member copy - see PaddedStatScrubbed below.
//
// This file uses EXACT for Vec3 and SCRUBBED for PaddedStat's safe twin,
// PaddedStatScrubbed. See docs/security.md#unmanaged-struct-padding.
// ============================================================================

#include "memorypack/memorypack.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

// -- 1. The classic: three floats, 12 bytes, no padding needed. --------------
//    MEMORYPACK_UNMANAGED_EXACT compiles this into exactly the same code as
//    MEMORYPACK_UNMANAGED, but additionally proves at compile time that the
//    three floats add up to sizeof(Vec3) - there is no padding to worry about.
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    friend bool operator==(const Vec3&, const Vec3&) = default;
};
MEMORYPACK_UNMANAGED_EXACT(Vec3, 12, x, y, z)

// -- 2. The one that bites: a byte followed by an int. -----------------------
//    C#: struct PaddedStat { byte Tier; int Score; }  ->  8 bytes on the wire.
//    Registered with the plain, unchecked macro on purpose, so the padding
//    leak below is real rather than hypothetical - PaddedStatScrubbed further
//    down is the same shape with the leak fixed structurally.
struct PaddedStat {
    uint8_t tier  = 0;
    int32_t score = 0;
    friend bool operator==(const PaddedStat&, const PaddedStat&) = default;
};
MEMORYPACK_UNMANAGED(PaddedStat, 8)   // 8, not 5 - the padding is part of the wire format

// -- 2b. The same shape, fixed structurally with MEMORYPACK_UNMANAGED_SCRUBBED.
//    Serialize() always copies tier/score into a value-initialized temporary
//    before writing it, so the temporary's padding - not the caller's - is
//    what reaches the wire, and that padding is always zero.
struct PaddedStatScrubbed {
    uint8_t tier  = 0;
    int32_t score = 0;
    friend bool operator==(const PaddedStatScrubbed&, const PaddedStatScrubbed&) = default;
};
MEMORYPACK_UNMANAGED_SCRUBBED(PaddedStatScrubbed, 8, tier, score)

// -- 3. For contrast: the same shape as a MANAGED type. ----------------------
//    A C# class, or a struct containing a string, is NOT unmanaged, so it gets
//    the normal object header + per-member encoding.
struct ManagedVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
MEMORYPACK_DEFINE(ManagedVec3, x, y, z)

// -- 4. Unmanaged values nest inside normal objects like any other member. ---
struct Transform {
    Vec3        position;
    Vec3        scale;
    std::string label;      // this member is what makes Transform managed
};
MEMORYPACK_DEFINE(Transform, position, scale, label)

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

} // namespace

int main() {
    std::printf("== 05 unmanaged structs ==\n\n");

    // -----------------------------------------------------------------------
    // No object header at all.
    // -----------------------------------------------------------------------
    std::printf("--- no object header ---\n\n");
    const Vec3 v{1.5f, 2.5f, 3.5f};
    const std::vector<uint8_t> vecBytes = memorypack::Serialize(v);
    Dump("Vec3{1.5, 2.5, 3.5} (unmanaged)", vecBytes);
    std::printf("    -> 12 bytes: three raw little-endian floats, nothing else.\n\n");

    const std::vector<uint8_t> managedBytes = memorypack::Serialize(ManagedVec3{1.5f, 2.5f, 3.5f});
    Dump("the same three floats as a MANAGED object", managedBytes);
    std::printf("    -> 13 bytes: the leading 03 is the object header.\n");
    std::printf("       Header cost avoided per value: %zu byte(s).\n\n",
                managedBytes.size() - vecBytes.size());

    Check("unmanaged Vec3 has no header", vecBytes.size() == 12);
    Check("managed equivalent has a header", managedBytes.size() == 13);
    Check("Vec3 round trip", memorypack::Deserialize<Vec3>(vecBytes) == v);

    // -----------------------------------------------------------------------
    // Padding is part of the wire format - and it is YOUR padding.
    //
    // Because the value is copied as a memory image, the three padding bytes
    // between `tier` and `score` are transmitted verbatim. C++ leaves padding
    // INDETERMINATE: `PaddedStat s{3, 42};` initialises the two members and
    // says nothing about the gap, so whatever happened to be on the stack goes
    // out on the wire. That is harmless for correctness (the receiver ignores
    // those bytes, and .NET zeroes its own) but it means:
    //
    //   * the bytes are not deterministic, so hashing / deduplicating /
    //     golden-file testing a serialized struct gives unstable results, and
    //   * you are leaking whatever was in that stack slot to the peer.
    //
    // Zero the whole object before filling it in whenever either matters.
    // -----------------------------------------------------------------------
    std::printf("--- padding is part of the format ---\n\n");
    PaddedStat stat;
    // Zero the PADDING, not just the members. The cast to void* is what keeps
    // GCC's -Wclass-memaccess quiet: PaddedStat has default member
    // initializers, so it is not a trivial type even though it is trivially
    // copyable, and the cast is the documented way to say "yes, I mean it".
    std::memset(static_cast<void*>(&stat), 0, sizeof(stat));
    stat.tier  = 3;
    stat.score = 0x11223344;

    const std::vector<uint8_t> statBytes = memorypack::Serialize(stat);
    Dump("PaddedStat{tier=3, score=0x11223344}, zeroed first", statBytes);
    std::printf("    -> 03 | 00 00 00 (padding) | 44 33 22 11\n");
    std::printf("       sizeof(PaddedStat) == %zu, matching .NET's natural alignment.\n",
                sizeof(PaddedStat));
    std::printf("       MEMORYPACK_UNMANAGED(PaddedStat, 8) is what catches layout drift.\n\n");

    // The same VALUE without that zeroing: the members are identical, the BYTES
    // are not. Here the padding is pre-filled with 0xCD to stand in for the
    // leftover stack data a real `PaddedStat s{3, 0x11223344};` would ship.
    {
        PaddedStat noisy;
        std::memset(static_cast<void*>(&noisy), 0xCD, sizeof(noisy));
        noisy.tier  = 3;
        noisy.score = 0x11223344;

        const std::vector<uint8_t> noisyBytes = memorypack::Serialize(noisy);
        Dump("the same value, padding left dirty", noisyBytes);
        std::printf("    -> bytes 1..3 carry whatever was in that stack slot. The VALUE\n");
        std::printf("       still decodes correctly, but the BYTES are not reproducible,\n");
        std::printf("       and you just sent three bytes of your own memory to the peer.\n\n");

        Check("dirty padding still decodes",
              memorypack::Deserialize<PaddedStat>(noisyBytes) == noisy);
        Check("dirty padding really does reach the wire", noisyBytes[1] == 0xCD);
        Check("same value, different bytes", noisyBytes != statBytes);
    }

    Check("PaddedStat is 8 bytes", statBytes.size() == 8);
    Check("PaddedStat round trip", memorypack::Deserialize<PaddedStat>(statBytes) == stat);
    Check("zeroed padding is actually zero",
          statBytes[1] == 0 && statBytes[2] == 0 && statBytes[3] == 0);

    // -----------------------------------------------------------------------
    // MEMORYPACK_UNMANAGED_SCRUBBED: the same padding problem, fixed
    // structurally instead of by caller discipline. `dirtyScrubbed` below is
    // filled from the same 0xCD noise as `noisy` above, but Serialize() copies
    // its members into a fresh, value-initialized temporary before writing -
    // so the dirty padding never reaches the wire.
    // -----------------------------------------------------------------------
    std::printf("--- MEMORYPACK_UNMANAGED_SCRUBBED fixes it structurally ---\n\n");
    PaddedStatScrubbed dirtyScrubbed;
    std::memset(static_cast<void*>(&dirtyScrubbed), 0xCD, sizeof(dirtyScrubbed));
    dirtyScrubbed.tier  = 3;
    dirtyScrubbed.score = 0x11223344;

    const std::vector<uint8_t> scrubbedBytes = memorypack::Serialize(dirtyScrubbed);
    Dump("PaddedStatScrubbed{tier=3, score=0x11223344}, padding left dirty", scrubbedBytes);
    std::printf("    -> padding is zero anyway: Serialize() never touches the caller's\n");
    std::printf("       padding at all, only the value-initialized temporary's.\n\n");

    Check("SCRUBBED zeroes padding even from a dirty source object",
          scrubbedBytes[1] == 0 && scrubbedBytes[2] == 0 && scrubbedBytes[3] == 0);
    Check("SCRUBBED bytes match the cleanly-zeroed PaddedStat bytes",
          scrubbedBytes == statBytes);
    Check("SCRUBBED round trip",
          memorypack::Deserialize<PaddedStatScrubbed>(scrubbedBytes) == dirtyScrubbed);

    // -----------------------------------------------------------------------
    // Bulk arrays: WriteUnmanagedCollection / ReadUnmanagedCollection.
    //
    // C#: List<Vec3> / Vec3[] of an unmanaged element type is written as
    //     [4B int32 count][count * sizeof(T) raw bytes]
    // with a single memcpy - no per-element work at all. That is the whole
    // point of unmanaged structs, and the reason to prefer them for anything
    // that ships in quantity (positions, vertices, tile maps, samples).
    //
    // The generic Write/Read path would also work here (it detects the
    // formatter per element), but going through the explicit
    // Write/ReadUnmanagedCollection pair documents the intent and guarantees
    // the single-memcpy path.
    //
    // CAVEAT: that single memcpy is also why WriteUnmanagedCollection is not
    // safe to call directly on a MEMORYPACK_UNMANAGED_SCRUBBED type such as
    // PaddedStatScrubbed - it copies the span's raw bytes and never goes
    // through the type's Serialize(), so scrubbing would not happen. Use the
    // generic Write(vector<T>) path for a SCRUBBED element type instead; it
    // calls Serialize() per element like any other formatter.
    // -----------------------------------------------------------------------
    std::printf("--- bulk arrays (single memcpy) ---\n\n");
    const std::vector<Vec3> cloud{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};

    memorypack::MemoryPackWriter writer;
    writer.WriteUnmanagedCollection(std::span<const Vec3>(cloud));
    Dump("vector<Vec3> of 3 via WriteUnmanagedCollection", writer.GetSpan());
    std::printf("    -> [03 00 00 00 count][3 * 12 raw bytes] = %zu bytes,\n",
                writer.Size());
    std::printf("       written and read back with one memcpy each way.\n\n");

    memorypack::MemoryPackReader reader(writer.GetSpan());
    std::vector<Vec3> cloudBack;
    reader.ReadUnmanagedCollection(cloudBack);

    Check("bulk array size", writer.Size() == 4 + 3 * 12);
    Check("bulk array round trip", cloudBack == cloud);
    Check("bulk array fully consumed", reader.IsEnd());

    std::printf("    read back %zu points:", cloudBack.size());
    for (const Vec3& p : cloudBack) {
        std::printf(" (%.0f,%.0f,%.0f)", static_cast<double>(p.x),
                    static_cast<double>(p.y), static_cast<double>(p.z));
    }
    std::printf("\n\n");

    // -----------------------------------------------------------------------
    // Unmanaged members inside a normal object.
    // -----------------------------------------------------------------------
    std::printf("--- unmanaged members inside a managed object ---\n\n");
    const Transform transform{{0, 1, 0}, {2, 2, 2}, "player"};
    const std::vector<uint8_t> transformBytes = memorypack::Serialize(transform);
    Dump("Transform{position, scale, label}", transformBytes);
    std::printf("    -> 03 (header) | 12 raw bytes | 12 raw bytes | string.\n");
    std::printf("       The Vec3 members contribute no headers of their own.\n\n");

    const auto transformBack = memorypack::Deserialize<Transform>(transformBytes);
    Check("nested unmanaged members", transformBack.position == transform.position
                                   && transformBack.scale == transform.scale
                                   && transformBack.label == transform.label);

    // -----------------------------------------------------------------------
    // Reminder: std::optional<UnmanagedStruct> maps to C# Nullable<T> and
    // therefore follows the Nullable layout, not the object-null one.
    // -----------------------------------------------------------------------
    const std::vector<uint8_t> optionalBytes = memorypack::Serialize(std::optional<Vec3>{v});
    std::printf("optional<Vec3> is %zu bytes: [1B hasValue][3B pad][12B Vec3]\n",
                optionalBytes.size());
    std::printf("(C# Nullable<Vec3>, see 03_nullable.cpp case 1)\n\n");
    Check("optional<Vec3> uses the Nullable layout", optionalBytes.size() == 16);

    std::printf("unmanaged structs %s\n", g_ok ? "OK" : "MISMATCH");
    return g_ok ? 0 : 1;
}
