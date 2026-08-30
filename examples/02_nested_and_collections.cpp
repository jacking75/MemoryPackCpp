// examples/02_nested_and_collections.cpp
// ============================================================================
// Nested objects and the standard-library container mappings.
//
// What this example shows, and what each encodes to:
//
//   nested object            C# class field      [1B memberCount][members...]
//   std::vector<UserType>    List<T> / T[]       [4B int32 count][elements...]
//   std::vector<std::vector> List<List<int>>     collection of collections
//   std::map<K,V>            Dictionary<K,V>     [4B count][key value]*count
//   std::set<T>              HashSet<T>          [4B count][elements...]
//   std::optional<T>         nullable member     see 03_nullable.cpp
//
// The two rules worth internalising:
//
//   * An OBJECT costs one byte of header (its member count, 255 == null).
//   * A COLLECTION costs four bytes of header (an int32 element count,
//     -1 == null). There is no per-element tag and no VarInt anywhere.
//
// Because a collection header is a plain int32 count, an empty collection
// (`00 00 00 00`) and a null one (`FF FF FF FF`) are different on the wire -
// that distinction is the whole subject of 03_nullable.cpp.
// ============================================================================

#include "memorypack/memorypack.hpp"

#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// C#:
//     [MemoryPackable] public partial class Item {
//         public int Id { get; set; }
//         public string Name { get; set; } = "";
//     }
// ---------------------------------------------------------------------------
struct Item {
    int32_t     id = 0;
    std::string name;
};
MEMORYPACK_DEFINE(Item, id, name)

// ---------------------------------------------------------------------------
// C#:
//     [MemoryPackable] public partial class Inventory {
//         public Item Equipped { get; set; }                    // nested object
//         public List<Item> Bag { get; set; }                   // List<T>
//         public List<List<int>> Grid { get; set; }             // nested lists
//         public Dictionary<string, int> Counts { get; set; }   // Dictionary
//         public HashSet<int> Tags { get; set; }                // HashSet
//         public string? Note { get; set; }                     // nullable string
//     }
// ---------------------------------------------------------------------------
struct Inventory {
    Item                              equipped;
    std::vector<Item>                 bag;
    std::vector<std::vector<int32_t>> grid;
    std::map<std::string, int32_t>    counts;
    std::set<int32_t>                 tags;
    std::optional<std::string>        note;
};
MEMORYPACK_DEFINE(Inventory, equipped, bag, grid, counts, tags, note)

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

// Serializes one value on its own so that its encoding can be inspected in
// isolation, then explains what came out.
template<typename T>
void Show(const char* label, const T& value, const char* explanation) {
    const std::vector<uint8_t> bytes = memorypack::Serialize(value);
    Dump(label, bytes);
    std::printf("    -> %s\n\n", explanation);
}

} // namespace

int main() {
    std::printf("== 02 nested objects and collections ==\n\n");

    // -- 1. A nested object is just an object inline; no pointer, no indirection.
    Show("Item{7, \"axe\"}", Item{7, "axe"},
         "[02 header][07 00 00 00 id][FC FF FF FF = ~3][03 00 00 00 utf16][61 78 65]");

    // -- 2. std::vector<UserType> == C# List<Item>.
    //       [4B count] then each element with its own object header.
    Show("vector<Item> of 2", std::vector<Item>{{1, "a"}, {2, "b"}},
         "[02 00 00 00] then two complete Item objects, each with its own header");

    Show("vector<Item> empty", std::vector<Item>{},
         "[00 00 00 00] - an empty list, NOT null (null would be FF FF FF FF)");

    // -- 3. Nested collections: the outer count, then a full inner collection
    //       (its own int32 count + payload) per element.
    Show("vector<vector<int32>>",
         std::vector<std::vector<int32_t>>{{1, 2}, {3}},
         "[02 00 00 00] [02 00 00 00][1][2] [01 00 00 00][3]");

    // -- 4. std::map == C# Dictionary<K,V>: a flat collection of key/value
    //       pairs. A KeyValuePair has NO object header of its own - the key is
    //       immediately followed by the value.
    Show("map<string,int32>",
         std::map<std::string, int32_t>{{"hp", 100}, {"mp", 50}},
         "[02 00 00 00] then key,value key,value with no pair header");

    // -- 5. std::set == C# HashSet<T>. Same collection framing. Note that C#
    //       HashSet enumeration order is insertion order while std::set is
    //       sorted, so the BYTES may differ from C# even though the SET does
    //       not. Never assume set/dictionary byte equality across languages.
    Show("set<int32>", std::set<int32_t>{3, 1, 2},
         "[03 00 00 00][1][2][3] - std::set iterates sorted");

    // -- 6. Optional members. Full treatment in 03_nullable.cpp.
    Show("optional<string> engaged", std::optional<std::string>{"hi"},
         "a normal string; the optional adds no bytes of its own");
    Show("optional<string> null", std::optional<std::string>{},
         "FF FF FF FF - the string null sentinel (int32 -1)");

    // -- 7. Everything at once.
    Inventory inv;
    inv.equipped = Item{7, "axe"};
    inv.bag      = {{1, "a"}, {2, "b"}};
    inv.grid     = {{1, 2}, {3}};
    inv.counts   = {{"hp", 100}, {"mp", 50}};
    inv.tags     = {3, 1, 2};
    inv.note     = "starter kit";

    const std::vector<uint8_t> bytes = memorypack::Serialize(inv);
    Dump("Inventory (all six members)", bytes);
    std::printf("    -> leading 06 is the object header: six members follow, in\n");
    std::printf("       declaration order, with nothing between them.\n\n");

    const auto back = memorypack::Deserialize<Inventory>(bytes);

    std::printf("round-tripped\n");
    std::printf("    equipped = {%d, \"%s\"}\n", back.equipped.id, back.equipped.name.c_str());
    std::printf("    bag      = %zu items", back.bag.size());
    for (const auto& item : back.bag) std::printf("  {%d,\"%s\"}", item.id, item.name.c_str());
    std::printf("\n");
    std::printf("    grid     = %zu rows", back.grid.size());
    for (const auto& row : back.grid) std::printf("  [%zu]", row.size());
    std::printf("\n");
    std::printf("    counts   =");
    for (const auto& [key, value] : back.counts) std::printf("  %s=%d", key.c_str(), value);
    std::printf("\n");
    std::printf("    tags     =");
    for (int32_t tag : back.tags) std::printf("  %d", tag);
    std::printf("\n");
    std::printf("    note     = %s\n", back.note ? back.note->c_str() : "(null)");

    const bool same = back.equipped.name == inv.equipped.name
                   && back.bag.size() == inv.bag.size()
                   && back.grid == inv.grid
                   && back.counts == inv.counts
                   && back.tags == inv.tags
                   && back.note == inv.note;
    std::printf("\nround trip %s\n", same ? "OK" : "MISMATCH");
    return same ? 0 : 1;
}
