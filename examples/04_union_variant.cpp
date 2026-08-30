// examples/04_union_variant.cpp
// ============================================================================
// std::variant as a C# [MemoryPackUnion] polymorphic payload.
//
// C# side:
//
//     [MemoryPackable]
//     [MemoryPackUnion(0,   typeof(MoveCommand))]
//     [MemoryPackUnion(1,   typeof(ChatCommand))]
//     [MemoryPackUnion(300, typeof(AdminCommand))]
//     public partial interface ICommand { }
//
//     [MemoryPackable] public partial class MoveCommand  : ICommand { ... }
//     [MemoryPackable] public partial class ChatCommand  : ICommand { ... }
//     [MemoryPackable] public partial class AdminCommand : ICommand { ... }
//
// C++ side: one MEMORYPACK_UNION_TAG per alternative, then a std::variant of
// them. The tag numbers - not the declaration order of the variant - are the
// wire contract, exactly as in C#.
//
// Wire format
//   tag <  250 :  [1B tag]            [the concrete object]
//   tag >= 250 :  [1B 250][2B uint16 tag]   [the concrete object]
//   null union :  [1B 255]
//
// WHY the 250 escape exists: 250..255 are reserved header values shared with
// the object header (255 == null). A union tag of 250 or above therefore has to
// be escaped, and MemoryPack spends one marker byte (250 == WIDE_TAG) plus a
// full uint16. Keep hot-path tags below 250 and you pay one byte per message.
//
// A bare std::variant cannot be empty, so a NULLABLE union is
// std::optional<std::variant<...>> - that is the [1B 255] case.
// ============================================================================

#include "memorypack/memorypack.hpp"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

// -- The alternatives -------------------------------------------------------
struct MoveCommand {
    float x = 0.0f;
    float y = 0.0f;
};

struct ChatCommand {
    std::string text;
};

struct AdminCommand {
    int32_t code = 0;
};

// Each alternative is a normal MemoryPack object: it carries its own object
// header after the union tag.
MEMORYPACK_DEFINE(MoveCommand, x, y)
MEMORYPACK_DEFINE(ChatCommand, text)
MEMORYPACK_DEFINE(AdminCommand, code)

// The tags. These MUST match the [MemoryPackUnion(N, ...)] attributes in C#.
// 300 is deliberately >= 250 so the wide-tag encoding shows up below.
MEMORYPACK_UNION_TAG(MoveCommand,  0)
MEMORYPACK_UNION_TAG(ChatCommand,  1)
MEMORYPACK_UNION_TAG(AdminCommand, 300)

// The variant maps to the C# interface/abstract base. Its FIRST alternative is
// what a default-constructed variant holds, so make that one cheap and safe.
using Command = std::variant<MoveCommand, ChatCommand, AdminCommand>;

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

// Dispatch on the active alternative - the C++ equivalent of a C# switch on
// the concrete type behind the interface.
void Handle(const Command& command) {
    std::visit([](const auto& alt) {
        using Alt = std::decay_t<decltype(alt)>;
        if constexpr (std::is_same_v<Alt, MoveCommand>) {
            std::printf("    Move  x=%.1f y=%.1f\n",
                        static_cast<double>(alt.x), static_cast<double>(alt.y));
        } else if constexpr (std::is_same_v<Alt, ChatCommand>) {
            std::printf("    Chat  \"%s\"\n", alt.text.c_str());
        } else {
            std::printf("    Admin code=%d\n", alt.code);
        }
    }, command);
}

} // namespace

int main() {
    std::printf("== 04 unions (std::variant <-> [MemoryPackUnion]) ==\n\n");

    // -----------------------------------------------------------------------
    // Narrow tags: one byte of overhead.
    // -----------------------------------------------------------------------
    std::printf("--- narrow tags (< 250): one byte ---\n\n");
    Show("Command = MoveCommand{1,2}", Command{MoveCommand{1.0f, 2.0f}},
         "00 (tag 0) | 02 (MoveCommand object header) | 4B float | 4B float");
    Show("Command = ChatCommand{\"gg\"}", Command{ChatCommand{"gg"}},
         "01 (tag 1) | 01 (ChatCommand header) | string 'gg'");

    // -----------------------------------------------------------------------
    // Wide tag: >= 250 escapes to [250][uint16]. 300 == 0x012C, little-endian
    // 2C 01, so the header is FA 2C 01 - three bytes instead of one.
    // -----------------------------------------------------------------------
    std::printf("--- wide tag (>= 250): the 250 escape + uint16 ---\n\n");
    Show("Command = AdminCommand{7}", Command{AdminCommand{7}},
         "FA (250 == WIDE_TAG) | 2C 01 (uint16 300) | 01 (header) | 07 00 00 00");

    std::printf("    tag 0   header cost: %zu byte(s)\n",
                memorypack::Serialize(Command{MoveCommand{}}).size()
                    - memorypack::Serialize(MoveCommand{}).size());
    std::printf("    tag 300 header cost: %zu byte(s)  <- keep hot tags below 250\n\n",
                memorypack::Serialize(Command{AdminCommand{}}).size()
                    - memorypack::Serialize(AdminCommand{}).size());

    // -----------------------------------------------------------------------
    // Nullable union. A std::variant always holds SOMETHING, so "no command"
    // has to be std::optional<std::variant<...>>, which serializes null as the
    // object-null header 0xFF (see 03_nullable.cpp, case 4).
    // -----------------------------------------------------------------------
    std::printf("--- nullable union: std::optional<std::variant<...>> ---\n\n");
    Show("optional<Command> = ChatCommand{\"hi\"}",
         std::optional<Command>{Command{ChatCommand{"hi"}}},
         "01 (tag) | 01 (header) | string - the optional adds nothing");
    Show("optional<Command> = null", std::optional<Command>{},
         "FF - one byte for the whole null union");

    // -----------------------------------------------------------------------
    // Round trip + dispatch, the way a real message loop would consume it.
    // -----------------------------------------------------------------------
    std::printf("--- receiving side ---\n");
    const Command outgoing[] = {
        Command{MoveCommand{3.5f, -1.25f}},
        Command{ChatCommand{"hello union"}},
        Command{AdminCommand{42}},
    };

    for (const Command& command : outgoing) {
        const std::vector<uint8_t> bytes = memorypack::Serialize(command);
        const auto received = memorypack::Deserialize<Command>(bytes);
        Check("variant alternative survives the round trip",
              received.index() == command.index());
        Handle(received);
    }

    Check("MoveCommand payload",
          std::get<MoveCommand>(memorypack::Deserialize<Command>(
              memorypack::Serialize(outgoing[0]))).x == 3.5f);
    Check("ChatCommand payload",
          std::get<ChatCommand>(memorypack::Deserialize<Command>(
              memorypack::Serialize(outgoing[1]))).text == "hello union");
    Check("AdminCommand payload (wide tag)",
          std::get<AdminCommand>(memorypack::Deserialize<Command>(
              memorypack::Serialize(outgoing[2]))).code == 42);

    {
        const auto nullBack = memorypack::Deserialize<std::optional<Command>>(
            memorypack::Serialize(std::optional<Command>{}));
        Check("null union stays null", !nullBack.has_value());

        const auto someBack = memorypack::Deserialize<std::optional<Command>>(
            memorypack::Serialize(std::optional<Command>{Command{AdminCommand{9}}}));
        Check("engaged nullable union",
              someBack.has_value() && std::holds_alternative<AdminCommand>(*someBack));
    }

    std::printf("\nunions %s\n", g_ok ? "OK" : "MISMATCH");
    return g_ok ? 0 : 1;
}
