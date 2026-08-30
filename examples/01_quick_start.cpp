// examples/01_quick_start.cpp
// ============================================================================
// MemoryPackCpp in 30 seconds: define a struct, describe it once, round-trip it.
//
// What this example shows
//   1. A plain C++ struct that mirrors a C# [MemoryPackable] type.
//   2. MEMORYPACK_DEFINE, which generates the serializer from the member list.
//   3. memorypack::Serialize / memorypack::Deserialize.
//   4. What the produced bytes actually look like, field by field.
//
// The C# type this mirrors:
//
//     [MemoryPackable]
//     public partial class LoginRequest
//     {
//         public int    UserId   { get; set; }
//         public string UserName { get; set; }
//         public float  Level    { get; set; }
//         public bool   IsAdmin  { get; set; }
//     }
//
// WHY the member ORDER is the contract: MemoryPack never writes member names.
// It writes a single "how many members follow" byte and then the members back
// to back in declaration order. Renaming a member is therefore free; reordering
// one, or inserting one in the middle, is a wire break. Appending at the end is
// safe - see 09_version_tolerance.cpp.
// ============================================================================

#include "memorypack/memorypack.hpp"

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The type. Default member initializers are not required by the library, but
// they make `Deserialize` on a short/absent payload land on known values
// instead of indeterminate ones, so always give them.
// ---------------------------------------------------------------------------
struct LoginRequest {
    int32_t     userId  = 0;
    std::string userName;
    float       level   = 0.0f;
    bool        isAdmin = false;
};

// MEMORYPACK_DEFINE must sit at GLOBAL scope. Internally it opens
// `namespace memorypack { ... }` to specialize memorypack::IMemoryPackable<T>,
// so it cannot appear inside a function, a class, or another namespace.
// Note: no trailing semicolon (the macro already closes its own braces).
MEMORYPACK_DEFINE(LoginRequest, userId, userName, level, isAdmin)

namespace {

// A tiny hex dump. Deliberately duplicated in every example file so that each
// one compiles on its own with nothing but the library on the include path.
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

} // namespace

int main() {
    std::printf("== 01 quick start ==\n\n");

    const LoginRequest request{1001, "Alice", 12.5f, true};

    // Serialize() allocates and returns the buffer. For zero-allocation paths
    // see 06_fixed_buffer.cpp.
    const std::vector<uint8_t> bytes = memorypack::Serialize(request);

    Dump("wire bytes", bytes);

    // ---------------------------------------------------------------------
    // Byte-by-byte, so the wire format stops being a black box:
    //
    //   04                        object header: 4 members follow
    //   E9 03 00 00               int32 userId = 1001, little-endian
    //   FA FF FF FF               string header: ~(-6) == 5 UTF-8 bytes
    //   05 00 00 00               ...and 5 UTF-16 code units (C# string.Length)
    //   41 6C 69 63 65            the UTF-8 payload "Alice"
    //   00 00 48 41               float level = 12.5f (IEEE 754, little-endian)
    //   01                        bool isAdmin = true (1 byte, not bit-packed)
    //
    // No VarInts, no type tags, no field names: MemoryPack is a memory-layout
    // copy, which is exactly why it is fast and why order is load-bearing.
    // ---------------------------------------------------------------------
    std::printf("\nlayout: [1B count][4B int32][4B ~utf8Len][4B utf16Len][utf8][4B float][1B bool]\n");
    std::printf("        1 + 4 + 4 + 4 + 5 + 4 + 1 = %zu bytes\n\n", bytes.size());

    // Deserialize is the mirror image. It is lenient: a payload whose header
    // declares fewer members than the struct has leaves the rest at default.
    const auto back = memorypack::Deserialize<LoginRequest>(bytes);

    std::printf("round-tripped values\n");
    std::printf("    userId   = %d\n", back.userId);
    std::printf("    userName = \"%s\"\n", back.userName.c_str());
    std::printf("    level    = %.2f\n", static_cast<double>(back.level));
    std::printf("    isAdmin  = %s\n", back.isAdmin ? "true" : "false");

    const bool same = back.userId == request.userId
                   && back.userName == request.userName
                   && back.level == request.level
                   && back.isAdmin == request.isAdmin;
    std::printf("\nround trip %s\n", same ? "OK" : "MISMATCH");
    return same ? 0 : 1;
}
