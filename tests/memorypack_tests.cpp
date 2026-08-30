// Unit tests for MemoryPackCpp (header-only).
//
// Dependency-free on purpose: a tiny built-in assertion harness keeps the test
// target buildable anywhere a C++23 compiler exists, with no external libraries.
// Returns a non-zero exit code if any check fails (CTest-friendly).
//
// Byte-for-byte compatibility with real C# MemoryPack is covered separately by
// interop_tests.cpp, which replays fixtures captured from the C# library.

#include "memorypack/memorypack.hpp"
#include "memorypack/packet.hpp"
#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

using namespace memorypack;

// ── Test fixtures ────────────────────────────────────────────────────────────────

struct TestPacket {
    int32_t     id = 0;
    std::string name;
};

enum class Color : uint16_t { Red = 1, Green = 2, Blue = 3 };
enum class Signed : int8_t { Low = -128, Zero = 0, High = 127 };

// Written by hand to keep exercising the original hand-rolled extension point.
namespace memorypack {
template<>
struct IMemoryPackable<TestPacket> {
    static void Serialize(MemoryPackWriter& w, const TestPacket* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(2);
        w.WriteInt32(v->id);
        w.WriteString(v->name);
    }
    static void Deserialize(MemoryPackReader& r, TestPacket& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.id = r.ReadInt32();
        if (cnt >= 2) { auto s = r.ReadString(); v.name = s.value_or(""); }
    }
};
} // namespace memorypack

// The same shape declared with the macro; both must produce identical bytes.
struct MacroPacket {
    int32_t id = 0;
    std::string name;
};
MEMORYPACK_DEFINE(MacroPacket, id, name)

struct Nested {
    int32_t outer = 0;
    MacroPacket inner;
    std::vector<MacroPacket> more;
};
MEMORYPACK_DEFINE(Nested, outer, inner, more)

struct PlainVec3 {
    float x = 0.f, y = 0.f, z = 0.f;
    friend bool operator==(const PlainVec3&, const PlainVec3&) = default;
};
MEMORYPACK_UNMANAGED(PlainVec3, 12)

// C# `struct { byte Tag; int Value; }` - natural alignment leaves 3 bytes of
// padding after the first member, and MemoryPack copies them to the wire.
struct PaddedPair {
    uint8_t tag;
    int32_t value;
    friend bool operator==(const PaddedPair&, const PaddedPair&) = default;
};

// The same fields under [StructLayout(Pack = 1)]: no padding.
#pragma pack(push, 1)
struct PackedPair {
    uint8_t tag;
    int32_t value;
    friend bool operator==(const PackedPair&, const PackedPair&) = default;
};
#pragma pack(pop)
MEMORYPACK_UNMANAGED(PaddedPair, 8)
MEMORYPACK_UNMANAGED(PackedPair, 5)

struct AltA { int32_t a = 0; };
struct AltB { std::string b; };
MEMORYPACK_DEFINE(AltA, a)
MEMORYPACK_DEFINE(AltB, b)
MEMORYPACK_UNION_TAG(AltA, 0)
MEMORYPACK_UNION_TAG(AltB, 300)
using Alt = std::variant<AltA, AltB>;

// ── Primitives ───────────────────────────────────────────────────────────────────

static void test_primitives() {
    TEST_CASE("primitives");
    MemoryPackWriter w;
    w.WriteBool(true);
    w.WriteBool(false);
    w.WriteInt8(-5);
    w.WriteUInt8(200);
    w.WriteInt16(-1234);
    w.WriteUInt16(54321);
    w.WriteInt32(-123456);
    w.WriteUInt32(3000000000u);
    w.WriteInt64(-1234567890123LL);
    w.WriteUInt64(12345678901234567890ULL);
    w.WriteFloat(3.14159f);
    w.WriteDouble(2.718281828459045);

    auto buf = w.TakeBuffer();
    MemoryPackReader r(buf.data(), buf.size());
    CHECK(r.ReadBool() == true);
    CHECK(r.ReadBool() == false);
    CHECK(r.ReadInt8() == -5);
    CHECK(r.ReadUInt8() == 200);
    CHECK(r.ReadInt16() == -1234);
    CHECK(r.ReadUInt16() == 54321);
    CHECK(r.ReadInt32() == -123456);
    CHECK(r.ReadUInt32() == 3000000000u);
    CHECK(r.ReadInt64() == -1234567890123LL);
    CHECK(r.ReadUInt64() == 12345678901234567890ULL);
    CHECK(r.ReadFloat() == 3.14159f);
    CHECK(r.ReadDouble() == 2.718281828459045);
    CHECK(r.IsEnd());
}

// Special IEEE-754 values must survive with their exact bit pattern.
static void test_special_floats() {
    TEST_CASE("special floats");
    MemoryPackWriter w;
    w.WriteFloat(std::nanf(""));
    w.WriteFloat(std::numeric_limits<float>::infinity());
    w.WriteFloat(-std::numeric_limits<float>::infinity());
    w.WriteDouble(-0.0);
    w.WriteDouble(std::numeric_limits<double>::denorm_min());

    auto buf = w.TakeBuffer();
    MemoryPackReader r(buf);
    CHECK(std::isnan(r.ReadFloat()));
    CHECK(std::isinf(r.ReadFloat()));
    CHECK(std::isinf(r.ReadFloat()));
    double negZero = r.ReadDouble();
    CHECK(negZero == 0.0 && std::signbit(negZero));
    CHECK(r.ReadDouble() == std::numeric_limits<double>::denorm_min());
}

// Golden bytes mirror the documented wire format. Output is always little-endian
// regardless of host endianness, so these are host-independent.
static void test_golden_bytes() {
    TEST_CASE("golden bytes");
    {   // String "Hello"
        MemoryPackWriter w;
        w.WriteString("Hello");
        CHECK_BYTES(w.Data(), w.Size(),
            0xFA, 0xFF, 0xFF, 0xFF, 0x05, 0, 0, 0, 'H', 'e', 'l', 'l', 'o');
    }
    {   // vector<int32>{10, 20, 30}
        MemoryPackWriter w;
        w.WriteVector(std::vector<int32_t>{10, 20, 30});
        CHECK_BYTES(w.Data(), w.Size(),
            0x03, 0, 0, 0, 0x0A, 0, 0, 0, 0x14, 0, 0, 0, 0x1E, 0, 0, 0);
    }
    {   // Object { id = 42, name = "ABC" }
        MemoryPackWriter w;
        w.WriteObjectHeader(2);
        w.WriteInt32(42);
        w.WriteString("ABC");
        CHECK_BYTES(w.Data(), w.Size(),
            0x02, 0x2A, 0, 0, 0, 0xFC, 0xFF, 0xFF, 0xFF, 0x03, 0, 0, 0, 'A', 'B', 'C');
    }
    {   // map<int32,int32>{{1,10}} -> [count][key][value]
        MemoryPackWriter w;
        w.WriteMap(std::map<int32_t, int32_t>{{1, 10}});
        CHECK_BYTES(w.Data(), w.Size(),
            0x01, 0, 0, 0, 0x01, 0, 0, 0, 0x0A, 0, 0, 0);
    }
    {   // tuple<int32,int32> -> object header + items
        MemoryPackWriter w;
        w.WriteTuple(std::tuple<int32_t, int32_t>{7, 8});
        CHECK_BYTES(w.Data(), w.Size(),
            0x02, 0x07, 0, 0, 0, 0x08, 0, 0, 0);
    }
    {   // enum -> underlying integer
        MemoryPackWriter w;
        w.WriteEnum(Color::Green);
        CHECK_BYTES(w.Data(), w.Size(), 0x02, 0x00);
    }
    {   // union tag < 250 -> one byte; tag >= 250 -> [250][uint16]
        MemoryPackWriter w;
        w.WriteUnionHeader(3);
        w.WriteUnionHeader(300);
        w.WriteNullUnionHeader();
        CHECK_BYTES(w.Data(), w.Size(), 0x03, 0xFA, 0x2C, 0x01, 0xFF);
    }
}

// ── Strings ──────────────────────────────────────────────────────────────────────

static void test_string() {
    TEST_CASE("string");
    MemoryPackWriter w;
    w.WriteString("");
    w.WriteString("hello world");
    w.WriteNullString();
    w.WriteOptionalString(std::optional<std::string>{});
    w.WriteOptionalString(std::optional<std::string>{"opt"});

    auto buf = w.TakeBuffer();
    MemoryPackReader r(buf.data(), buf.size());
    auto a = r.ReadString(); CHECK(a.has_value() && *a == "");
    auto b = r.ReadString(); CHECK(b.has_value() && *b == "hello world");
    auto c = r.ReadString(); CHECK(!c.has_value());
    auto d = r.ReadString(); CHECK(!d.has_value());
    auto e = r.ReadString(); CHECK(e.has_value() && *e == "opt");
}

// Verifies the MemoryPack UTF-8 string wire format byte-for-byte.
static void test_string_format() {
    TEST_CASE("string format / MemoryPack compat");

    {   MemoryPackWriter w; w.WriteString("ABC");
        CHECK_BYTES(w.Data(), w.Size(), 0xFC, 0xFF, 0xFF, 0xFF, 0x03, 0, 0, 0, 'A', 'B', 'C'); }
    {   MemoryPackWriter w; w.WriteString("");
        CHECK_BYTES(w.Data(), w.Size(), 0x00, 0, 0, 0); }
    {   MemoryPackWriter w; w.WriteString("\xC3\xA9");                    // e-acute
        CHECK_BYTES(w.Data(), w.Size(), 0xFD, 0xFF, 0xFF, 0xFF, 0x01, 0, 0, 0, 0xC3, 0xA9); }
    {   MemoryPackWriter w; w.WriteString("\xED\x95\x9C");                // Hangul syllable
        CHECK_BYTES(w.Data(), w.Size(), 0xFC, 0xFF, 0xFF, 0xFF, 0x01, 0, 0, 0, 0xED, 0x95, 0x9C); }
    {   MemoryPackWriter w; w.WriteString("\xF0\x9F\x98\x80");            // U+1F600 (surrogate pair)
        CHECK_BYTES(w.Data(), w.Size(),
            0xFB, 0xFF, 0xFF, 0xFF, 0x02, 0, 0, 0, 0xF0, 0x9F, 0x98, 0x80); }
    {   MemoryPackWriter w; w.WriteString("A\xED\x95\x9C\xF0\x9F\x98\x80");
        CHECK_BYTES(w.Data(), w.Size(),
            0xF7, 0xFF, 0xFF, 0xFF, 0x04, 0, 0, 0,
            0x41, 0xED, 0x95, 0x9C, 0xF0, 0x9F, 0x98, 0x80); }
    {   MemoryPackWriter w; w.WriteNullString();
        CHECK_BYTES(w.Data(), w.Size(), 0xFF, 0xFF, 0xFF, 0xFF); }

    // Round-trip, including multi-byte UTF-8.
    for (const std::string& s : { std::string("ABC"), std::string(""),
                                  std::string("\xC3\xA9"), std::string("\xED\x95\x9C"),
                                  std::string("\xF0\x9F\x98\x80"),
                                  std::string("A\xED\x95\x9C\xF0\x9F\x98\x80") }) {
        MemoryPackWriter w; w.WriteString(s);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        auto got = r.ReadString();
        CHECK(got.has_value() && *got == s);
        CHECK(r.IsEnd());
    }
    {   MemoryPackWriter w; w.WriteNullString();
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(!r.ReadString().has_value()); }

    // UTF-16 read path: a positive header is a UTF-16LE code-unit count.
    {   uint8_t d[] = {0x03, 0, 0, 0, 0x41, 0x00, 0x42, 0x00, 0x43, 0x00};
        MemoryPackReader r(d, sizeof(d));
        auto got = r.ReadString();
        CHECK(got.has_value() && *got == "ABC"); }
    {   // surrogate pair D83D DE00 -> U+1F600 -> F0 9F 98 80
        uint8_t d[] = {0x02, 0, 0, 0, 0x3D, 0xD8, 0x00, 0xDE};
        MemoryPackReader r(d, sizeof(d));
        auto got = r.ReadString();
        CHECK(got.has_value() && *got == "\xF0\x9F\x98\x80"); }
}

// An unpaired surrogate must decode to U+FFFD, not to malformed UTF-8.
static void test_string_bad_surrogates() {
    TEST_CASE("string / lone surrogates");
    {   // High surrogate followed by a normal BMP unit: D83D 0041
        uint8_t d[] = {0x02, 0, 0, 0, 0x3D, 0xD8, 0x41, 0x00};
        MemoryPackReader r(d, sizeof(d));
        auto got = r.ReadString();
        CHECK(got.has_value());
        CHECK(got && *got == "\xEF\xBF\xBD" "A");   // U+FFFD then 'A'
    }
    {   // Lone low surrogate
        uint8_t d[] = {0x01, 0, 0, 0, 0x00, 0xDC};
        MemoryPackReader r(d, sizeof(d));
        auto got = r.ReadString();
        CHECK(got.has_value() && *got == "\xEF\xBF\xBD");
    }
    {   // High surrogate at the very end of the payload
        uint8_t d[] = {0x01, 0, 0, 0, 0x3D, 0xD8};
        MemoryPackReader r(d, sizeof(d));
        auto got = r.ReadString();
        CHECK(got.has_value() && *got == "\xEF\xBF\xBD");
    }
}

static void test_string_view_and_inplace() {
    TEST_CASE("string / view and in-place read");
    MemoryPackWriter w;
    w.WriteString("borrowed");
    w.WriteString("second");
    auto buf = w.TakeBuffer();

    MemoryPackReader r(buf);
    auto view = r.ReadStringView();
    CHECK(view.has_value() && *view == "borrowed");
    CHECK_MSG(view && view->data() >= reinterpret_cast<const char*>(buf.data()) &&
                  view->data() < reinterpret_cast<const char*>(buf.data()) + buf.size(),
              "ReadStringView must borrow the input, not copy it");

    std::string reused = "previous contents";
    CHECK(r.ReadString(reused));
    CHECK(reused == "second");

    // A UTF-16 payload cannot be viewed; the reader must rewind and say so.
    uint8_t utf16[] = {0x01, 0, 0, 0, 0x41, 0x00};
    MemoryPackReader r16(utf16, sizeof(utf16));
    CHECK(!r16.ReadStringView().has_value());
    CHECK(r16.Position() == 0);
    auto viaCopy = r16.ReadString();
    CHECK(viaCopy.has_value() && *viaCopy == "A");
}

static void test_utf16_strings() {
    TEST_CASE("string / UTF-16 output");
    MemoryPackWriter w;
    w.WriteStringUtf16(u"ABC");
    CHECK_BYTES(w.Data(), w.Size(), 0x03, 0, 0, 0, 'A', 0, 'B', 0, 'C', 0);

    std::u16string back;
    MemoryPackReader r(w.GetSpan());
    r.Read(back);
    CHECK(back == u"ABC");

    // Round-trip through the generic dispatch, including a surrogate pair.
    std::u16string wide = u"A";
    wide.push_back(static_cast<char16_t>(0xD83D));
    wide.push_back(static_cast<char16_t>(0xDE00));
    auto bytes = Serialize(wide);
    auto restored = Deserialize<std::u16string>(bytes);
    CHECK(restored == wide);
}

// ── Objects ──────────────────────────────────────────────────────────────────────

static void test_object() {
    TEST_CASE("object / IMemoryPackable");
    TestPacket p{42, "ABC"};
    auto data = Serialize(p);
    CHECK_BYTES(data.data(), data.size(),
        0x02, 0x2A, 0, 0, 0, 0xFC, 0xFF, 0xFF, 0xFF, 0x03, 0, 0, 0, 'A', 'B', 'C');

    auto back = Deserialize<TestPacket>(data);
    CHECK(back.id == 42);
    CHECK(back.name == "ABC");

    // Version tolerance: the header declares 1 member, the struct expects 2.
    MemoryPackWriter w;
    w.WriteObjectHeader(1);
    w.WriteInt32(7);
    auto buf = w.TakeBuffer();
    TestPacket vt = Deserialize<TestPacket>(buf);
    CHECK(vt.id == 7);
    CHECK(vt.name == "");

    // Null object header.
    MemoryPackWriter wn;
    wn.WriteNullObjectHeader();
    MemoryPackReader rn(wn.Data(), wn.Size());
    auto [cnt, isNull] = rn.ReadObjectHeader();
    CHECK(isNull);
    CHECK(cnt == 0);

    // Deserialize into an existing instance.
    TestPacket target;
    Deserialize(data.data(), data.size(), target);
    CHECK(target.id == 42 && target.name == "ABC");
}

// MEMORYPACK_DEFINE must generate exactly the hand-written encoding.
static void test_define_macro() {
    TEST_CASE("MEMORYPACK_DEFINE");
    TestPacket hand{42, "ABC"};
    MacroPacket macro{42, "ABC"};
    auto handBytes = Serialize(hand);
    auto macroBytes = Serialize(macro);
    CHECK_BYTES_EQ(std::span<const uint8_t>(macroBytes), std::span<const uint8_t>(handBytes),
                   "macro-generated encoding must match the hand-written one");

    auto back = Deserialize<MacroPacket>(macroBytes);
    CHECK(back.id == 42 && back.name == "ABC");
    CHECK(IMemoryPackable<MacroPacket>::MemberCount == 2);

    // Version tolerance is generated too.
    MemoryPackWriter w;
    w.WriteObjectHeader(1);
    w.WriteInt32(9);
    auto partial = w.TakeBuffer();
    auto vt = Deserialize<MacroPacket>(partial);
    CHECK(vt.id == 9 && vt.name.empty());

    // Nested objects and collections of objects.
    Nested n{1, MacroPacket{2, "inner"}, {MacroPacket{3, "a"}, MacroPacket{4, "b"}}};
    auto nestedBytes = Serialize(n);
    auto nestedBack = Deserialize<Nested>(nestedBytes);
    CHECK(nestedBack.outer == 1);
    CHECK(nestedBack.inner.id == 2 && nestedBack.inner.name == "inner");
    CHECK(nestedBack.more.size() == 2);
    if (nestedBack.more.size() == 2) {
        CHECK(nestedBack.more[0].name == "a");
        CHECK(nestedBack.more[1].id == 4);
    }
}

// ── Collections ──────────────────────────────────────────────────────────────────

static void test_collections() {
    TEST_CASE("collections");
    {   std::vector<int32_t> v{1, 2, 3, -4, 5};
        MemoryPackWriter w; w.WriteVector(v);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(r.ReadVector<int32_t>() == v); }
    {   std::vector<uint8_t> v{0, 1, 2, 255, 128};
        MemoryPackWriter w; w.WriteVector(v);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(r.ReadVector<uint8_t>() == v); }
    {   MemoryPackWriter w; w.WriteVector(std::vector<int32_t>{});
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(r.ReadVector<int32_t>().empty()); }
    {   // null collection -> empty vector
        MemoryPackWriter w; w.WriteNullCollectionHeader();
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(r.ReadVector<int32_t>().empty()); }
    {   std::vector<std::string> v{"a", "bb", "ccc"};
        MemoryPackWriter w; w.WriteStringVector(v);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(r.ReadStringVector() == v); }
    {   // A null element inside a string collection decodes as an empty string.
        MemoryPackWriter w;
        w.WriteCollectionHeader(2);
        w.WriteString("x");
        w.WriteNullString();
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf);
        auto got = r.ReadStringVector();
        CHECK(got.size() == 2 && got[0] == "x" && got[1].empty()); }
}

// std::vector<bool> is a bit-packed specialisation; it must still be one byte
// per element on the wire, like C# List<bool>.
static void test_vector_bool() {
    TEST_CASE("collections / vector<bool>");
    std::vector<bool> v{true, false, true};
    MemoryPackWriter w;
    w.WriteVector(v);
    CHECK_BYTES(w.Data(), w.Size(), 0x03, 0, 0, 0, 0x01, 0x00, 0x01);

    auto buf = w.TakeBuffer();
    MemoryPackReader r(buf);
    std::vector<bool> back;
    r.ReadVector(back);
    CHECK(back == v);

    // Also through the generic dispatch.
    auto bytes = Serialize(v);
    CHECK(Deserialize<std::vector<bool>>(bytes) == v);
    CHECK(Serialize(std::vector<bool>{}).size() == 4);
}

static void test_generic_collections() {
    TEST_CASE("collections / generic element types");
    {   // vector of objects
        std::vector<MacroPacket> v{{1, "a"}, {2, "bb"}};
        auto bytes = Serialize(v);
        auto back = Deserialize<std::vector<MacroPacket>>(bytes);
        CHECK(back.size() == 2 && back[1].name == "bb"); }
    {   // nested collections
        std::vector<std::vector<int32_t>> v{{1, 2}, {}, {3}};
        auto bytes = Serialize(v);
        auto back = Deserialize<std::vector<std::vector<int32_t>>>(bytes);
        CHECK(back.size() == 3 && back[0].size() == 2 && back[1].empty() && back[2][0] == 3); }
    {   // deque / list use the same collection encoding as vector
        std::deque<int32_t> d{1, 2, 3};
        auto bytes = Serialize(d);
        const auto asVector = Serialize(std::vector<int32_t>{1, 2, 3});
        CHECK_BYTES_EQ(std::span<const uint8_t>(bytes), std::span<const uint8_t>(asVector),
                       "deque must encode like vector");
        CHECK(Deserialize<std::deque<int32_t>>(bytes) == d);

        std::list<std::string> l{"a", "b"};
        auto lbytes = Serialize(l);
        CHECK(Deserialize<std::list<std::string>>(lbytes) == l); }
    {   // sets
        std::set<int32_t> s{3, 1, 2};
        auto bytes = Serialize(s);
        CHECK(Deserialize<std::set<int32_t>>(bytes) == s);

        std::unordered_set<std::string> us{"x", "y"};
        auto ubytes = Serialize(us);
        CHECK(Deserialize<std::unordered_set<std::string>>(ubytes) == us); }
    {   // std::array of objects
        std::array<MacroPacket, 2> arr{MacroPacket{1, "p"}, MacroPacket{2, "q"}};
        auto bytes = Serialize(arr);
        auto back = Deserialize<std::array<MacroPacket, 2>>(bytes);
        CHECK(back[0].name == "p" && back[1].id == 2); }
}

static void test_arrays() {
    TEST_CASE("arrays");
    {   std::array<int32_t, 4> a{10, 20, 30, 40};
        MemoryPackWriter w; w.WriteArray(a);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(r.ReadArray<int32_t, 4>() == a); }
    {   // read excess: write 5, read into 3 -> first 3 kept, 2 skipped
        MemoryPackWriter w; w.WriteVector(std::vector<int32_t>{1, 2, 3, 4, 5});
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        auto got = r.ReadArray<int32_t, 3>();
        CHECK(got[0] == 1 && got[1] == 2 && got[2] == 3);
        CHECK(r.IsEnd()); }
    {   // shortfall: write 2, read into 4 -> remainder stays zero
        MemoryPackWriter w; w.WriteVector(std::vector<int32_t>{7, 8});
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        auto got = r.ReadArray<int32_t, 4>();
        CHECK(got[0] == 7 && got[1] == 8 && got[2] == 0 && got[3] == 0); }
    {   int32_t src[5] = {5, 6, 7, 8, 9};
        MemoryPackWriter w; w.WriteArray(src, 5);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        int32_t dst[3] = {};
        int32_t n = r.ReadArray(dst, 3);
        CHECK(n == 3);
        CHECK(dst[0] == 5 && dst[1] == 6 && dst[2] == 7);
        CHECK(r.IsEnd()); }
    {   int32_t src[2] = {1, 2};
        MemoryPackWriter w; w.WriteArray(src, 2);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        int32_t dst[4] = {-1, -1, -1, -1};
        int32_t n = r.ReadArray(dst, 4);
        CHECK(n == 2);
        CHECK(dst[0] == 1 && dst[1] == 2); }
}

// ── Maps, tuples, enums ──────────────────────────────────────────────────────────

static void test_extended() {
    TEST_CASE("map / tuple / enum");
    {   std::map<int32_t, int32_t> m{{1, 10}, {2, 20}, {3, 30}};
        MemoryPackWriter w; w.WriteMap(m);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK((r.ReadMap<int32_t, int32_t>() == m)); }
    {   std::unordered_map<int32_t, int32_t> m{{1, 100}, {2, 200}};
        MemoryPackWriter w; w.WriteMap(m);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK((r.ReadUnorderedMap<int32_t, int32_t>() == m)); }
    {   std::map<std::string, int32_t> m{{"a", 1}, {"b", 2}};
        MemoryPackWriter w; w.WriteMap(m);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK((r.ReadMap<std::string, int32_t>() == m)); }
    {   // map with object values, through the generic dispatch
        std::map<int32_t, MacroPacket> m{{1, {1, "one"}}, {2, {2, "two"}}};
        auto bytes = Serialize(m);
        auto back = Deserialize<std::map<int32_t, MacroPacket>>(bytes);
        CHECK(back.size() == 2 && back.at(2).name == "two"); }
    {   std::tuple<int32_t, std::string, double> t{99, "x", 1.5};
        MemoryPackWriter w; w.WriteTuple(t);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        auto got = r.ReadTuple<int32_t, std::string, double>();
        CHECK(std::get<0>(got) == 99);
        CHECK(std::get<1>(got) == "x");
        CHECK(std::get<2>(got) == 1.5); }
    {   // std::pair encodes as C# KeyValuePair: key then value, no header
        std::pair<int32_t, int32_t> p{1, 2};
        auto bytes = Serialize(p);
        CHECK_BYTES(bytes.data(), bytes.size(), 0x01, 0, 0, 0, 0x02, 0, 0, 0);
        CHECK((Deserialize<std::pair<int32_t, int32_t>>(bytes) == p)); }
    {   MemoryPackWriter w; w.WriteEnum(Color::Green);
        CHECK_BYTES(w.Data(), w.Size(), 0x02, 0x00);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(r.ReadEnum<Color>() == Color::Green); }
    {   // signed enum with a negative value
        MemoryPackWriter w; w.WriteEnum(Signed::Low);
        CHECK_BYTES(w.Data(), w.Size(), 0x80);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf);
        CHECK(r.ReadEnum<Signed>() == Signed::Low); }
    {   // enums round-trip through generic dispatch too
        auto bytes = Serialize(Color::Blue);
        CHECK(Deserialize<Color>(bytes) == Color::Blue); }
}

// ── Optionals, unmanaged structs, unions ─────────────────────────────────────────

static void test_optionals() {
    TEST_CASE("optional / nullable");
    {   // optional<int> maps to C# Nullable<int>: [1B hasValue][pad][value]
        std::optional<int32_t> some = 42;
        auto bytes = Serialize(some);
        CHECK_BYTES(bytes.data(), bytes.size(), 0x01, 0, 0, 0, 0x2A, 0, 0, 0);
        CHECK(Deserialize<std::optional<int32_t>>(bytes) == some);

        std::optional<int32_t> none;
        auto noneBytes = Serialize(none);
        CHECK_BYTES(noneBytes.data(), noneBytes.size(), 0, 0, 0, 0, 0, 0, 0, 0);
        CHECK(!Deserialize<std::optional<int32_t>>(noneBytes).has_value()); }
    {   // optional<string> uses the string null sentinel
        std::optional<std::string> none;
        auto bytes = Serialize(none);
        CHECK_BYTES(bytes.data(), bytes.size(), 0xFF, 0xFF, 0xFF, 0xFF);
        CHECK(!Deserialize<std::optional<std::string>>(bytes).has_value());

        std::optional<std::string> some = "hi";
        CHECK(Deserialize<std::optional<std::string>>(Serialize(some)) == some); }
    {   // optional<vector> uses the collection null sentinel, distinct from empty
        std::optional<std::vector<int32_t>> none;
        auto nullBytes = Serialize(none);
        CHECK_BYTES(nullBytes.data(), nullBytes.size(), 0xFF, 0xFF, 0xFF, 0xFF);
        CHECK(!Deserialize<std::optional<std::vector<int32_t>>>(nullBytes).has_value());

        std::optional<std::vector<int32_t>> empty = std::vector<int32_t>{};
        auto emptyBytes = Serialize(empty);
        CHECK_BYTES(emptyBytes.data(), emptyBytes.size(), 0, 0, 0, 0);
        auto backEmpty = Deserialize<std::optional<std::vector<int32_t>>>(emptyBytes);
        CHECK(backEmpty.has_value() && backEmpty->empty()); }
    {   // optional<object> uses the object null header
        std::optional<MacroPacket> none;
        auto bytes = Serialize(none);
        CHECK_BYTES(bytes.data(), bytes.size(), 0xFF);
        CHECK(!Deserialize<std::optional<MacroPacket>>(bytes).has_value());

        std::optional<MacroPacket> some = MacroPacket{5, "s"};
        auto back = Deserialize<std::optional<MacroPacket>>(Serialize(some));
        CHECK(back.has_value() && back->id == 5); }
    {   // C# Nullable<managed struct> carries an explicit hasValue marker
        MemoryPackWriter w;
        w.WriteNullableObject(std::optional<MacroPacket>{MacroPacket{1, "m"}});
        CHECK(w.Size() > 0 && w.Data()[0] == 0x01);
        MemoryPackReader r(w.GetSpan());
        auto back = r.ReadNullableObject<MacroPacket>();
        CHECK(back.has_value() && back->name == "m");

        MemoryPackWriter wn;
        wn.WriteNullableObject(std::optional<MacroPacket>{});
        CHECK_BYTES(wn.Data(), wn.Size(), 0xFF); }
    {   // smart pointers behave like nullable references
        auto up = std::make_unique<MacroPacket>();
        up->id = 3; up->name = "u";
        auto bytes = Serialize(up);
        auto back = Deserialize<std::unique_ptr<MacroPacket>>(bytes);
        CHECK(back && back->id == 3);

        std::shared_ptr<MacroPacket> null;
        const auto nullBytes = Serialize(null);
        CHECK_BYTES(nullBytes.data(), nullBytes.size(), 0xFF); }
}

static void test_unmanaged() {
    TEST_CASE("unmanaged structs");
    PlainVec3 v{1.5f, 2.5f, 3.5f};
    auto bytes = Serialize(v);
    CHECK(bytes.size() == 12);   // no object header
    CHECK(Deserialize<PlainVec3>(bytes) == v);

    // Bulk collection: [int32 count][count * sizeof(T)]
    std::vector<PlainVec3> points{{1, 2, 3}, {4, 5, 6}};
    MemoryPackWriter w;
    w.WriteUnmanagedCollection(std::span<const PlainVec3>(points));
    CHECK(w.Size() == 4 + 2 * 12);

    MemoryPackReader r(w.GetSpan());
    std::vector<PlainVec3> back;
    r.ReadUnmanagedCollection(back);
    CHECK(back == points);

    // optional<unmanaged struct> follows the Nullable<T> layout.
    std::optional<PlainVec3> some = v;
    auto optBytes = Serialize(some);
    CHECK(optBytes.size() == 16);           // 1 byte + 3 padding + 12
    CHECK(optBytes[0] == 1);
    CHECK(Deserialize<std::optional<PlainVec3>>(optBytes) == some);

    std::optional<PlainVec3> none;
    auto noneBytes = Serialize(none);
    CHECK(noneBytes.size() == 16 && noneBytes[0] == 0);
    CHECK(!Deserialize<std::optional<PlainVec3>>(noneBytes).has_value());
}

// The unmanaged fast path copies padding to the wire. Value-initialization is
// what makes that padding - and therefore the output - deterministic.
// See docs/security.md#unmanaged-struct-padding.
static void test_unmanaged_padding() {
    TEST_CASE("unmanaged structs / padding is deterministic when value-initialized");

    // 8 bytes on both sides: 1 byte, 3 bytes of padding, then the int32.
    static_assert(sizeof(PaddedPair) == 8, "must match the .NET sequential layout");

    // Dirty the stack so that any padding we fail to zero would be visible.
    {
        volatile uint8_t noise[512];
        for (size_t i = 0; i < sizeof(noise); ++i)
            noise[i] = static_cast<uint8_t>(0xA5 + i);
        (void)noise;
    }

    auto encode = []() {
        PaddedPair p{};              // value-initialized: padding is zeroed
        p.tag = 3;
        p.value = 0x11223344;
        return Serialize(p);
    };

    const auto first = encode();
    const auto second = encode();
    CHECK(first.size() == 8);
    CHECK_BYTES(first.data(), first.size(), 0x03, 0x00, 0x00, 0x00, 0x44, 0x33, 0x22, 0x11);
    CHECK_BYTES_EQ(std::span<const uint8_t>(first), std::span<const uint8_t>(second),
                   "value-initialized structs must serialize reproducibly");

    // A packed layout has no padding at all, so the question cannot arise.
    PackedPair q{};
    q.tag = 3;
    q.value = 0x11223344;
    const auto packed = Serialize(q);
    CHECK(packed.size() == 5);
    CHECK_BYTES(packed.data(), packed.size(), 0x03, 0x44, 0x33, 0x22, 0x11);

    CHECK(Deserialize<PaddedPair>(first) == PaddedPair{3, 0x11223344});
    CHECK(Deserialize<PackedPair>(packed) == PackedPair{3, 0x11223344});
}

static void test_unions() {
    TEST_CASE("union / variant");
    {   Alt a = AltA{7};
        auto bytes = Serialize(a);
        CHECK_BYTES(bytes.data(), bytes.size(), 0x00, 0x01, 0x07, 0, 0, 0);
        auto back = Deserialize<Alt>(bytes);
        CHECK(std::holds_alternative<AltA>(back));
        CHECK(std::get<AltA>(back).a == 7); }
    {   // tag 300 uses the wide-tag encoding
        Alt b = AltB{"wide"};
        auto bytes = Serialize(b);
        CHECK(bytes.size() >= 4);
        CHECK(bytes[0] == 0xFA && bytes[1] == 0x2C && bytes[2] == 0x01);
        auto back = Deserialize<Alt>(bytes);
        CHECK(std::holds_alternative<AltB>(back));
        CHECK(std::get<AltB>(back).b == "wide"); }
    {   // a nullable union is optional<variant>
        std::optional<Alt> none;
        auto bytes = Serialize(none);
        CHECK_BYTES(bytes.data(), bytes.size(), 0xFF);
        CHECK(!Deserialize<std::optional<Alt>>(bytes).has_value());

        std::optional<Alt> some = Alt{AltA{1}};
        auto back = Deserialize<std::optional<Alt>>(Serialize(some));
        CHECK(back.has_value() && std::holds_alternative<AltA>(*back)); }
}

// ── VersionTolerant ──────────────────────────────────────────────────────────────

static void test_version_tolerant() {
    TEST_CASE("version tolerant");
    // Member lengths use the MemoryPack encoding: <=127 as one byte,
    // <=65535 as 0x84 + uint16, otherwise 0x82 + uint32.
    {   MemoryPackWriter w;
        w.WriteVarIntLength(4);
        w.WriteVarIntLength(127);
        w.WriteVarIntLength(130);
        w.WriteVarIntLength(70000);
        CHECK_BYTES(w.Data(), w.Size(),
            0x04,
            0x7F,
            0x84, 0x82, 0x00,
            0x82, 0x70, 0x11, 0x01, 0x00);

        MemoryPackReader r(w.GetSpan());
        CHECK(r.ReadVarIntLength() == 4);
        CHECK(r.ReadVarIntLength() == 127);
        CHECK(r.ReadVarIntLength() == 130);
        CHECK(r.ReadVarIntLength() == 70000); }

    {   // Writing three members produces [count][len][len][len][bodies]
        MemoryPackWriter w;
        {
            VersionTolerantWriter vt(w);
            vt.WriteMember(int32_t{77});
            vt.WriteMember(std::string("vt"));
            vt.WriteMember(0.5f);
        }
        CHECK_BYTES(w.Data(), w.Size(),
            0x03, 0x04, 0x0A, 0x04,
            0x4D, 0, 0, 0,
            0xFD, 0xFF, 0xFF, 0xFF, 0x02, 0, 0, 0, 'v', 't',
            0x00, 0x00, 0x00, 0x3F);

        MemoryPackReader r(w.GetSpan());
        VersionTolerantReader vt(r);
        CHECK(!vt.IsNull());
        CHECK(vt.Count() == 3);
        int32_t id = 0; std::string name; float score = 0;
        CHECK(vt.ReadMember(id));
        CHECK(vt.ReadMember(name));
        CHECK(vt.ReadMember(score));
        vt.Finish();
        CHECK(id == 77 && name == "vt" && score == 0.5f);
        CHECK(r.IsEnd()); }

    {   // A reader that knows fewer members must still land after the object.
        MemoryPackWriter w;
        {
            VersionTolerantWriter vt(w);
            vt.WriteMember(int32_t{1});
            vt.WriteMember(std::string("dropped"));
            vt.WriteMember(int32_t{2});
        }
        w.WriteInt32(0x5A5A5A5A);   // sentinel that must remain readable

        MemoryPackReader r(w.GetSpan());
        VersionTolerantReader vt(r);
        int32_t first = 0;
        CHECK(vt.ReadMember(first));
        vt.Finish();                 // skips the two members we did not read
        CHECK(first == 1);
        CHECK(r.ReadInt32() == 0x5A5A5A5A);
        CHECK(r.IsEnd()); }
}

// ── Buffers ──────────────────────────────────────────────────────────────────────

static void test_buffers() {
    TEST_CASE("buffers");
    {   std::vector<uint8_t> ext;
        MemoryPackWriter w(ext);
        w.WriteInt32(123);
        CHECK(ext.size() == 4);
        CHECK(w.Size() == 4); }
    {   // The documented "reserve a header, serialize, patch the length" pattern:
        // the caller-owned vector keeps an exact size() throughout.
        std::vector<uint8_t> sendBuffer;
        sendBuffer.resize(6);
        const size_t headerSize = sendBuffer.size();
        MemoryPackWriter w(sendBuffer);
        w.Write(MacroPacket{1, "hdr"});
        CHECK(sendBuffer.size() == w.Size());
        const size_t bodyLen = sendBuffer.size() - headerSize;
        CHECK(bodyLen == Serialize(MacroPacket{1, "hdr"}).size()); }
    {   std::array<uint8_t, 8> buf{};
        MemoryPackWriter w(buf);
        w.WriteInt32(1);
        w.WriteInt32(2);
        CHECK(w.Size() == 8);
        CHECK(w.RemainingCapacity() == 0); }
    {   std::array<uint8_t, 8> buf{};
        MemoryPackWriter w(buf);
        w.WriteInt32(1);
        w.Clear();
        CHECK(w.Size() == 0);
        w.WriteInt16(7);
        CHECK(w.Size() == 2); }
    {   // TakeBuffer leaves the writer reusable.
        MemoryPackWriter w;
        w.WriteInt32(1);
        auto first = w.TakeBuffer();
        CHECK(first.size() == 4);
        CHECK(w.Size() == 0);
        w.WriteInt64(2);
        auto second = w.TakeBuffer();
        CHECK(second.size() == 8); }
    {   // The writer is movable, so it can be stored in containers.
        MemoryPackWriter w;
        w.WriteInt32(5);
        MemoryPackWriter moved = std::move(w);
        moved.WriteInt32(6);
        CHECK(moved.Size() == 8);
        auto bytes = moved.TakeBuffer();
        MemoryPackReader r(bytes);
        CHECK(r.ReadInt32() == 5);
        CHECK(r.ReadInt32() == 6); }
    {   // Serializing straight into a fixed buffer.
        std::array<uint8_t, 64> buf{};
        size_t n = SerializeTo(std::span<uint8_t>(buf), MacroPacket{9, "fix"});
        CHECK(n > 0);
        auto back = Deserialize<MacroPacket>(std::span<const uint8_t>(buf.data(), n));
        CHECK(back.id == 9 && back.name == "fix"); }
    {   // Appending into a caller-owned vector.
        std::vector<uint8_t> out;
        Serialize(MacroPacket{1, "a"}, out);
        const size_t afterFirst = out.size();
        Serialize(MacroPacket{2, "b"}, out);
        CHECK(out.size() > afterFirst);
        MemoryPackReader r(out);
        CHECK(r.Read<MacroPacket>().name == "a");
        CHECK(r.Read<MacroPacket>().name == "b");
        CHECK(r.IsEnd()); }
}

// ── Errors and hostile input ─────────────────────────────────────────────────────

static void test_errors() {
    TEST_CASE("errors / bounds");

    // Fixed buffer overflow.
    CHECK_FAILS("fixed buffer overflow", {
        std::array<uint8_t, 2> buf{};
        MemoryPackWriter w(buf);
        w.WriteInt32(1);
        return w.Failed();
    });

    // Reader underflow.
    CHECK_FAILS("reader underflow", {
        uint8_t d[1] = {0};
        MemoryPackReader r(d, 1);
        (void)r.ReadInt32();
        return r.Failed();
    });

    // Truncated UTF-8 string: the header claims 3 bytes but only 1 is present.
    CHECK_FAILS("truncated utf-8 string", {
        uint8_t d[] = {0xFC, 0xFF, 0xFF, 0xFF, 0x03, 0, 0, 0, 0x41};
        MemoryPackReader r(d, sizeof(d));
        (void)r.ReadString();
        return r.Failed();
    });

    // An object header in the reserved 250..254 range must be rejected.
    CHECK_FAILS("reserved object header", {
        uint8_t d[] = {0xFB};
        MemoryPackReader r(d, 1);
        (void)r.ReadObjectHeader();
        return r.Failed();
    });

    // A union header in the reserved 251..254 range must be rejected.
    CHECK_FAILS("reserved union header", {
        uint8_t d[] = {0xFC};
        MemoryPackReader r(d, 1);
        (void)r.ReadUnionHeader();
        return r.Failed();
    });

    // Writing a member count above 249 would collide with the reserved codes.
    CHECK_FAILS("member count above 249", {
        MemoryPackWriter w;
        w.WriteObjectHeader(250);
        return w.Failed();
    });

    // A collection length that cannot possibly fit must fail before allocating:
    // the "4 bytes of input ask for gigabytes of memory" case.
    CHECK_FAILS("vector length bomb", {
        uint8_t d[] = {0xFF, 0xFF, 0xFF, 0x7F};
        MemoryPackReader r(d, sizeof(d));
        (void)r.ReadVector<int32_t>();
        return r.Failed();
    });
    CHECK_FAILS("unordered_map length bomb", {
        uint8_t d[] = {0xFF, 0xFF, 0xFF, 0x7F};
        MemoryPackReader r(d, sizeof(d));
        (void)r.ReadUnorderedMap<int32_t, int32_t>();
        return r.Failed();
    });
    CHECK_FAILS("map length bomb", {
        uint8_t d[] = {0xFF, 0xFF, 0xFF, 0x7F};
        MemoryPackReader r(d, sizeof(d));
        (void)r.ReadMap<int32_t, int32_t>();
        return r.Failed();
    });
    CHECK_FAILS("string vector length bomb", {
        uint8_t d[] = {0xFF, 0xFF, 0xFF, 0x7F};
        MemoryPackReader r(d, sizeof(d));
        (void)r.ReadStringVector();
        return r.Failed();
    });
    CHECK_FAILS("object collection length bomb", {
        uint8_t d[] = {0xFF, 0xFF, 0xFF, 0x7F};
        MemoryPackReader r(d, sizeof(d));
        (void)r.ReadCollection<MacroPacket>();
        return r.Failed();
    });
    CHECK_FAILS("string length bomb", {
        // ~0x7FFFFFFF byte count with no payload behind it.
        uint8_t d[] = {0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x7F};
        MemoryPackReader r(d, sizeof(d));
        (void)r.ReadString();
        return r.Failed();
    });

    // A negative length other than -1 is not a valid collection header.
    CHECK_FAILS("negative collection length", {
        uint8_t d[] = {0xFE, 0xFF, 0xFF, 0xFF};
        MemoryPackReader r(d, sizeof(d));
        (void)r.ReadCollectionLength(4);
        return r.Failed();
    });

    // Seeking past the end fails rather than corrupting the position.
    CHECK_FAILS("seek past end", {
        uint8_t d[] = {1, 2, 3, 4};
        MemoryPackReader r(d, sizeof(d));
        r.Seek(99);
        return r.Failed();
    });

    // An unknown VersionTolerant length marker is rejected.
    CHECK_FAILS("unknown varint marker", {
        uint8_t d[] = {0x90, 0x00};
        MemoryPackReader r(d, sizeof(d));
        (void)r.ReadVarIntLength();
        return r.Failed();
    });
}

static void test_reader_limits() {
    TEST_CASE("errors / configurable limits");
    // A well-formed payload that is nevertheless larger than policy allows.
    const auto bytes = Serialize(std::vector<int32_t>{1, 2, 3, 4, 5});
    CHECK_FAILS("collection above the configured limit", {
        ReaderOptions strict;
        strict.maxCollectionLength = 3;
        MemoryPackReader r(std::span<const uint8_t>(bytes), strict);
        (void)r.ReadVector<int32_t>();
        return r.Failed();
    });

    // The same payload passes with the default limits.
    MemoryPackReader ok(bytes);
    CHECK(ok.ReadVector<int32_t>().size() == 5);

    const auto text = Serialize(std::string("abcdefghij"));
    CHECK_FAILS("string above the configured limit", {
        ReaderOptions shortStrings;
        shortStrings.maxStringLength = 4;
        MemoryPackReader r(std::span<const uint8_t>(text), shortStrings);
        (void)r.ReadString();
        return r.Failed();
    });

    // Depth limiting: MEMORYPACK_DEFINE-generated readers call EnterObject().
    const auto nested = Serialize(MacroPacket{1, "x"});
    CHECK_FAILS("object nested deeper than the configured limit", {
        ReaderOptions shallow;
        shallow.maxDepth = 0;
        MemoryPackReader r(std::span<const uint8_t>(nested), shallow);
        (void)r.Read<MacroPacket>();
        return r.Failed();
    });
}

static void test_deserialize_exact() {
    TEST_CASE("errors / trailing bytes");
    auto bytes = Serialize(MacroPacket{1, "x"});
    bytes.push_back(0xAA);   // extra byte, e.g. a C#/C++ member-order mismatch

    // The lenient API accepts it...
    auto lenient = Deserialize<MacroPacket>(bytes);
    CHECK(lenient.id == 1);

    // ...but DeserializeExact reports the leftover input.
    CHECK_THROWS(DeserializeExact<MacroPacket>(std::span<const uint8_t>(bytes)));
}

static void test_reader_navigation() {
    TEST_CASE("reader / navigation");
    MemoryPackWriter w;
    w.WriteInt32(1);
    w.WriteInt32(2);
    w.WriteInt32(3);
    auto bytes = w.TakeBuffer();

    MemoryPackReader r(bytes);
    CHECK(r.ReadInt32() == 1);
    CHECK(r.Position() == 4);
    r.Seek(8);
    CHECK(r.ReadInt32() == 3);
    CHECK(r.IsEnd());
    r.Reset();
    CHECK(r.Position() == 0);
    CHECK(r.ReadInt32() == 1);

    // A sub-reader is bounded by its own length.
    r.Reset();
    auto sub = r.SubReader(8);
    CHECK(sub.Remaining() == 8);
    CHECK(sub.ReadInt32() == 1);
    CHECK(sub.ReadInt32() == 2);
    CHECK(sub.IsEnd());
    CHECK(r.Position() == 8);
    CHECK(r.ReadInt32() == 3);
}

#if MEMORYPACK_HAS_EXPECTED
static void test_expected_api() {
    TEST_CASE("std::expected API");
    auto good = Serialize(MacroPacket{4, "ok"});
    auto ok = TryDeserialize<MacroPacket>(good);
    CHECK(ok.has_value());
    CHECK(ok && ok->id == 4);

    std::vector<uint8_t> truncated{0x02, 0x01};
    auto bad = TryDeserialize<MacroPacket>(truncated);
    CHECK(!bad.has_value());
    CHECK(!bad && bad.error() == MemoryPackError::BufferUnderflow);

    std::array<uint8_t, 2> tiny{};
    auto small = TrySerializeTo(std::span<uint8_t>(tiny), MacroPacket{1, "too long"});
    CHECK(!small.has_value());
    CHECK(!small && small.error() == MemoryPackError::BufferOverflow);

    std::array<uint8_t, 64> big{};
    auto fits = TrySerializeTo(std::span<uint8_t>(big), MacroPacket{1, "ok"});
    CHECK(fits.has_value());
}
#endif

// ── Packet framing helpers ───────────────────────────────────────────────────────

static void test_packet_framing() {
    TEST_CASE("packet framing");
    auto packet = MakePacket(101, MacroPacket{7, "framed"});
    auto header = PeekPacketHeader(packet);
    CHECK(header.has_value());
    CHECK(header && header->id == 101);
    CHECK(header && static_cast<size_t>(header->bodyLength) == packet.size() - PACKET_HEADER_SIZE);

    // Feeding the stream one byte at a time must still yield exactly one packet.
    PacketFrameParser parser;
    int delivered = 0;
    for (uint8_t byte : packet) {
        const bool ok = parser.Feed(std::span<const uint8_t>(&byte, 1),
                                    [&](uint16_t id, std::span<const uint8_t> body) {
                                        ++delivered;
                                        CHECK(id == 101);
                                        auto v = Deserialize<MacroPacket>(body);
                                        CHECK(v.id == 7 && v.name == "framed");
                                    });
        CHECK(ok);
    }
    CHECK(delivered == 1);
    CHECK(parser.Buffered() == 0);

    // Two packets arriving in one chunk.
    std::vector<uint8_t> two;
    WritePacket(two, 1, MacroPacket{1, "a"});
    WritePacket(two, 2, MacroPacket{2, "b"});
    int count = 0;
    PacketFrameParser p2;
    CHECK(p2.Feed(two, [&](uint16_t id, std::span<const uint8_t>) {
        ++count;
        CHECK(id == static_cast<uint16_t>(count));
    }));
    CHECK(count == 2);

    // An oversized declared length must abort the stream rather than allocate.
    PacketFrameParser guarded(16);
    std::vector<uint8_t> hostile(PACKET_HEADER_SIZE, 0);
    hostile[2] = 0xFF; hostile[3] = 0xFF; hostile[4] = 0xFF; hostile[5] = 0x7F;
    CHECK(!guarded.Feed(hostile, [](uint16_t, std::span<const uint8_t>) {}));
}

// ── .NET types ───────────────────────────────────────────────────────────────────

static void test_dotnet_helpers() {
    TEST_CASE(".NET type helpers");
    auto guid = Guid::Parse("01020304-0506-0708-090a-0b0c0d0e0f10");
    CHECK(guid.has_value());
    CHECK(guid && guid->ToString() == "01020304-0506-0708-090a-0b0c0d0e0f10");
    CHECK(!Guid::Parse("not-a-guid").has_value());
    CHECK(!Guid::Parse("01020304-0506-0708-090a-0b0c0d0e0f1").has_value());
    if (guid) {
        auto bytes = Serialize(*guid);
        CHECK(bytes.size() == 16);
        CHECK(Deserialize<Guid>(bytes) == *guid);
    }

    auto dt = DateTime::FromTicks(638000000000000000LL, DateTimeKind::Utc);
    CHECK(dt.GetKind() == DateTimeKind::Utc);
    CHECK(dt.GetTicks() == 638000000000000000LL);
    CHECK(Deserialize<DateTime>(Serialize(dt)) == dt);

    auto ts = TimeSpan::FromDuration(std::chrono::seconds(90));
    CHECK(ts.ticks == 900000000LL);
    CHECK(ts.ToDuration() == std::chrono::seconds(90));

    // Half conversions round-trip the values that matter.
    for (float f : {0.0f, 1.0f, -1.0f, 1.5f, 0.5f, 65504.0f, -0.25f}) {
        CHECK(Half::FromFloat(f).ToFloat() == f);
    }
    CHECK(std::isinf(Half::FromFloat(1e30f).ToFloat()));
    CHECK(std::isnan(Half::FromFloat(std::nanf("")).ToFloat()));

    auto tp = std::chrono::system_clock::now();
    auto roundTripped = DateTime::FromTimePoint(tp).ToTimePoint();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(
        roundTripped > tp ? roundTripped - tp : tp - roundTripped);
    CHECK(diff.count() == 0);
}

// ── Generic dispatch ─────────────────────────────────────────────────────────────

static void test_generic_dispatch() {
    TEST_CASE("generic Write/Read");
    MemoryPackWriter w;
    w.Write(int32_t{1});
    w.Write(std::string("two"));
    w.Write(std::vector<int32_t>{3, 4});
    w.Write(Color::Blue);
    w.Write(MacroPacket{5, "five"});

    MemoryPackReader r(w.GetSpan());
    CHECK(r.Read<int32_t>() == 1);
    CHECK(r.Read<std::string>() == "two");
    CHECK((r.Read<std::vector<int32_t>>() == std::vector<int32_t>{3, 4}));
    CHECK(r.Read<Color>() == Color::Blue);
    CHECK(r.Read<MacroPacket>().name == "five");
    CHECK(r.IsEnd());

    static_assert(Serializable<int32_t>);
    static_assert(Serializable<std::string>);
    static_assert(Serializable<MacroPacket>);
    static_assert(Serializable<std::vector<MacroPacket>>);
    static_assert(Serializable<Color>);
}

int main(int argc, char** argv) {
    test_primitives();
    test_special_floats();
    test_golden_bytes();
    test_string();
    test_string_format();
    test_string_bad_surrogates();
    test_string_view_and_inplace();
    test_utf16_strings();
    test_object();
    test_define_macro();
    test_collections();
    test_vector_bool();
    test_generic_collections();
    test_arrays();
    test_extended();
    test_optionals();
    test_unmanaged();
    test_unmanaged_padding();
    test_unions();
    test_version_tolerant();
    test_buffers();
    test_errors();
    test_reader_limits();
    test_deserialize_exact();
    test_reader_navigation();
#if MEMORYPACK_HAS_EXPECTED
    test_expected_api();
#endif
    test_packet_framing();
    test_dotnet_helpers();
    test_generic_dispatch();

    (void)argc;
    (void)argv;
    return mptest::Summarize("memorypack_tests");
}
