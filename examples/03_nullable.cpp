// examples/03_nullable.cpp
// ============================================================================
// The four different ways C# spells "null", and which C++ type maps to each.
//
// This is the single most common source of C#/C++ wire mismatches, because in
// C++ all four cases look identical (`std::optional<T>`) while on the wire they
// are four completely different encodings. MemoryPackCpp picks the right one
// automatically from T via memorypack::WireNullEncoding<T>, so as long as the
// C++ member type is the correct mapping of the C# member type, it just works.
//
//   C# member type          C++ member type                    null on the wire
//   ---------------------------------------------------------------------------
//   int?  (Nullable<int>)   std::optional<int32_t>             the Nullable<T>
//                                                              struct copied
//                                                              verbatim:
//                                                              [1B hasValue]
//                                                              [padding][T]
//   string?                 std::optional<std::string>         [4B -1]
//   List<int>? / int[]?     std::optional<std::vector<int32_t>>[4B -1]
//   MyClass?  (a nullable   std::optional<MyClass>             [1B 0xFF]
//   reference)
//
// WHY they differ: MemoryPack does not add a generic "is null" flag. It reuses
// whatever spare encoding space each kind of type already has.
//   * A struct value cannot be absent, so Nullable<T> - itself a struct - is
//     copied byte for byte, hasValue flag and CLR padding included. That is why
//     `int?` costs EIGHT bytes even when null, not four and not one.
//   * A collection header is a signed int32 count, so -1 is free to mean null.
//   * A string header is also a signed int32, so -1 means null there too
//     (0 means empty, negative-other means UTF-8, positive means UTF-16).
//   * An object header is a member count capped at 249, so 255 is free to mean
//     null.
//
// The practical consequence: `List<int>? x = null` and `List<int> x = new()`
// are DIFFERENT bytes. Round-tripping null as empty (or the reverse) silently
// changes meaning; the mapping below preserves it.
// ============================================================================

#include "memorypack/memorypack.hpp"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <vector>

// A managed C# type: a class, i.e. a reference. `Profile?` is a nullable
// REFERENCE, which is the [1B 0xFF] case.
//
//     [MemoryPackable] public partial class Profile {
//         public int Id { get; set; }
//         public string Nick { get; set; } = "";
//     }
struct Profile {
    int32_t     id = 0;
    std::string nick;
};
MEMORYPACK_DEFINE(Profile, id, nick)

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

template<typename T>
void Show(const char* label, const T& value, const char* note) {
    const std::vector<uint8_t> bytes = memorypack::Serialize(value);
    Dump(label, bytes);
    std::printf("    -> %s\n\n", note);
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
    std::printf("== 03 the four null encodings ==\n\n");

    // -----------------------------------------------------------------------
    // 1. C# `int?` == Nullable<int>, an unmanaged struct.
    //
    //    MemoryPack copies the CLR layout of Nullable<int> verbatim:
    //        struct Nullable<int> { bool hasValue; int value; }
    //    which the CLR lays out as [1B hasValue][3B padding][4B value] = 8 bytes,
    //    because `value` must sit at its natural 4-byte alignment.
    //
    //    So both the engaged and the disengaged form are 8 bytes. There is no
    //    "compact null". This is the case people get wrong most often, because
    //    the intuitive guess is "one byte, or four".
    // -----------------------------------------------------------------------
    std::printf("--- 1. C# `int?`  ->  std::optional<int32_t>  (Nullable<T> struct copy)\n\n");
    Show("optional<int32_t> = 42", std::optional<int32_t>{42},
         "01 | 00 00 00 (pad) | 2A 00 00 00 - hasValue byte, CLR padding, value");
    Show("optional<int32_t> = null", std::optional<int32_t>{},
         "00 | 00 00 00 (pad) | 00 00 00 00 - still 8 bytes; only byte 0 differs");

    // The same rule applies to any unmanaged struct, and the padding follows
    // the ELEMENT's alignment: optional<double> is 16 bytes (1 + 7 pad + 8).
    Show("optional<double> = null", std::optional<double>{},
         "16 bytes: alignof(double)==8 pushes the value to offset 8");

    // -----------------------------------------------------------------------
    // 2. C# `string?`.
    //
    //    A string header is a signed int32 that carries three meanings at once:
    //        -1  null
    //         0  empty
    //       < -1 UTF-8: ~header is the byte count, an int32 UTF-16 length
    //            follows, then the payload
    //       > 0  UTF-16: the value is the code-unit count, payload follows
    //
    //    Null therefore costs exactly four bytes, and null != empty.
    // -----------------------------------------------------------------------
    std::printf("--- 2. C# `string?`  ->  std::optional<std::string>  (4-byte -1 sentinel)\n\n");
    Show("optional<string> = \"hi\"", std::optional<std::string>{"hi"},
         "FD FF FF FF (~2 == -3) | 02 00 00 00 (utf16 len) | 68 69 ('hi')");
    Show("optional<string> = \"\"", std::optional<std::string>{std::string{}},
         "00 00 00 00 - EMPTY string");
    Show("optional<string> = null", std::optional<std::string>{},
         "FF FF FF FF - NULL string. Different from empty!");

    // -----------------------------------------------------------------------
    // 3. C# `List<int>?` / `int[]?`.
    //
    //    A collection header is a signed int32 element count, so -1 is null.
    //    Same four bytes as the string sentinel, but reached through a
    //    different code path - and again, null != empty.
    // -----------------------------------------------------------------------
    std::printf("--- 3. C# `List<int>?`  ->  std::optional<std::vector<int32_t>>  (4-byte -1)\n\n");
    Show("optional<vector<int32_t>> = {1,2}",
         std::optional<std::vector<int32_t>>{std::vector<int32_t>{1, 2}},
         "02 00 00 00 | 01 00 00 00 | 02 00 00 00");
    Show("optional<vector<int32_t>> = {}",
         std::optional<std::vector<int32_t>>{std::vector<int32_t>{}},
         "00 00 00 00 - an EMPTY list");
    Show("optional<vector<int32_t>> = null",
         std::optional<std::vector<int32_t>>{},
         "FF FF FF FF - a NULL list");

    // -----------------------------------------------------------------------
    // 4. C# `MyClass?` - a nullable REFERENCE, not Nullable<T>.
    //
    //    An object header is a member count, and the format reserves 250..255,
    //    so 255 (0xFF) is spare and means null. Null therefore costs ONE byte.
    //
    //    Careful: a nullable reference (`Profile?` where Profile is a class) is
    //    the 0xFF case, whereas `Nullable<SomeManagedStruct>` prefixes an extra
    //    [1B 1] marker - use WriteNullableObject/ReadNullableObject for that
    //    rarer shape. std::unique_ptr / std::shared_ptr map to this same
    //    nullable-reference encoding.
    // -----------------------------------------------------------------------
    std::printf("--- 4. C# `MyClass?`  ->  std::optional<Profile>  (single 0xFF byte)\n\n");
    Show("optional<Profile> = {5,\"neo\"}", std::optional<Profile>{Profile{5, "neo"}},
         "02 (member count) then the two members - no extra optional byte");
    Show("optional<Profile> = null", std::optional<Profile>{},
         "FF - one single byte for the whole null object");

    // -----------------------------------------------------------------------
    // Side by side: four nulls, four completely different encodings.
    // -----------------------------------------------------------------------
    std::printf("--- summary: sizeof(null) per kind ---\n");
    std::printf("    C# int?        -> %zu bytes\n",
                memorypack::Serialize(std::optional<int32_t>{}).size());
    std::printf("    C# string?     -> %zu bytes\n",
                memorypack::Serialize(std::optional<std::string>{}).size());
    std::printf("    C# List<int>?  -> %zu bytes\n",
                memorypack::Serialize(std::optional<std::vector<int32_t>>{}).size());
    std::printf("    C# MyClass?    -> %zu bytes\n\n",
                memorypack::Serialize(std::optional<Profile>{}).size());

    // -----------------------------------------------------------------------
    // Round trips, including the null-vs-empty distinction that matters.
    // -----------------------------------------------------------------------
    using memorypack::Deserialize;
    using memorypack::Serialize;

    Check("int? engaged",
          Deserialize<std::optional<int32_t>>(Serialize(std::optional<int32_t>{42})) == 42);
    Check("int? null",
          !Deserialize<std::optional<int32_t>>(Serialize(std::optional<int32_t>{})).has_value());

    Check("string? null stays null",
          !Deserialize<std::optional<std::string>>(
              Serialize(std::optional<std::string>{})).has_value());
    {
        const auto emptyBack = Deserialize<std::optional<std::string>>(
            Serialize(std::optional<std::string>{std::string{}}));
        Check("string? empty stays empty (not null)", emptyBack.has_value() && emptyBack->empty());
    }

    Check("List<int>? null stays null",
          !Deserialize<std::optional<std::vector<int32_t>>>(
              Serialize(std::optional<std::vector<int32_t>>{})).has_value());
    {
        const auto emptyList = Deserialize<std::optional<std::vector<int32_t>>>(
            Serialize(std::optional<std::vector<int32_t>>{std::vector<int32_t>{}}));
        Check("List<int>? empty stays empty (not null)",
              emptyList.has_value() && emptyList->empty());
    }

    Check("MyClass? null stays null",
          !Deserialize<std::optional<Profile>>(Serialize(std::optional<Profile>{})).has_value());
    {
        const auto profile = Deserialize<std::optional<Profile>>(
            Serialize(std::optional<Profile>{Profile{5, "neo"}}));
        Check("MyClass? engaged", profile.has_value() && profile->nick == "neo");
    }

    std::printf("all null encodings %s\n", g_ok ? "OK" : "MISMATCH");
    return g_ok ? 0 : 1;
}
