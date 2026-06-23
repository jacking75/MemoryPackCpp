// Unit tests for MemoryPackCpp (header-only).
//
// Dependency-free on purpose: a tiny built-in assertion harness keeps the test
// target buildable anywhere a C++23 compiler exists, with no external libraries.
// Returns a non-zero exit code if any check fails (CTest-friendly).

#include "memorypack/memorypack.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace memorypack;

// ── Tiny test harness ───────────────────────────────────────────────────────────
static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++g_checks;                                                                     \
        if (!(cond)) {                                                                  \
            ++g_failures;                                                               \
            std::printf("  [FAIL] %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);      \
        }                                                                               \
    } while (0)

#define CHECK_THROWS(expr)                                                              \
    do {                                                                                \
        ++g_checks;                                                                     \
        bool threw_ = false;                                                            \
        try { (void)(expr); } catch (...) { threw_ = true; }                            \
        if (!threw_) {                                                                  \
            ++g_failures;                                                               \
            std::printf("  [FAIL] %s:%d  CHECK_THROWS(%s)\n", __FILE__, __LINE__, #expr); \
        }                                                                               \
    } while (0)

static bool bytes_eq(const uint8_t* got, size_t n, std::initializer_list<int> exp) {
    if (n != exp.size()) return false;
    size_t i = 0;
    for (int e : exp) {
        if (got[i++] != static_cast<uint8_t>(e)) return false;
    }
    return true;
}

// ── Test fixtures ────────────────────────────────────────────────────────────────
struct TestPacket {
    int32_t     id = 0;
    std::string name;
};

enum class Color : uint16_t { Red = 1, Green = 2, Blue = 3 };

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

// ── Tests ────────────────────────────────────────────────────────────────────────
static void test_primitives() {
    std::printf("[primitives]\n");
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

// Golden bytes mirror the README "Wire Format" examples. Output is always
// little-endian regardless of host endianness, so these are host-independent.
static void test_golden_bytes() {
    std::printf("[golden bytes]\n");
    {   // String "Hello"
        MemoryPackWriter w;
        w.WriteString("Hello");
        CHECK(bytes_eq(w.Data(), w.Size(),
            {0xFA, 0xFF, 0xFF, 0xFF, 0x05, 0, 0, 0, 'H', 'e', 'l', 'l', 'o'}));
    }
    {   // vector<int32>{10, 20, 30}
        MemoryPackWriter w;
        w.WriteVector(std::vector<int32_t>{10, 20, 30});
        CHECK(bytes_eq(w.Data(), w.Size(),
            {0x03, 0, 0, 0, 0x0A, 0, 0, 0, 0x14, 0, 0, 0, 0x1E, 0, 0, 0}));
    }
    {   // Object { id = 42, name = "ABC" }
        MemoryPackWriter w;
        w.WriteObjectHeader(2);
        w.WriteInt32(42);
        w.WriteString("ABC");
        CHECK(bytes_eq(w.Data(), w.Size(),
            {0x02, 0x2A, 0, 0, 0, 0xFC, 0xFF, 0xFF, 0xFF, 0x03, 0, 0, 0, 'A', 'B', 'C'}));
    }
}

static void test_string() {
    std::printf("[string]\n");
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

// Verifies the MemoryPack UTF-8 string wire format byte-for-byte against real
// MemoryPack 1.x output (captured empirically), including multi-byte/surrogate cases.
static void test_string_format() {
    std::printf("[string format / MemoryPack compat]\n");

    auto golden = [](const std::string& s, std::initializer_list<int> exp) {
        MemoryPackWriter w; w.WriteString(s);
        CHECK(bytes_eq(w.Data(), w.Size(), exp));
    };
    golden("ABC",   {0xFC, 0xFF, 0xFF, 0xFF, 0x03, 0, 0, 0, 'A', 'B', 'C'});
    golden("Hello", {0xFA, 0xFF, 0xFF, 0xFF, 0x05, 0, 0, 0, 'H', 'e', 'l', 'l', 'o'});
    golden("",      {0x00, 0, 0, 0});
    golden("\xC3\xA9",         {0xFD, 0xFF, 0xFF, 0xFF, 0x01, 0, 0, 0, 0xC3, 0xA9});               // é
    golden("\xED\x95\x9C",     {0xFC, 0xFF, 0xFF, 0xFF, 0x01, 0, 0, 0, 0xED, 0x95, 0x9C});         // 한
    golden("\xF0\x9F\x98\x80", {0xFB, 0xFF, 0xFF, 0xFF, 0x02, 0, 0, 0, 0xF0, 0x9F, 0x98, 0x80});   // 😀 (surrogate)
    golden("A\xED\x95\x9C\xF0\x9F\x98\x80",
           {0xF7, 0xFF, 0xFF, 0xFF, 0x04, 0, 0, 0, 0x41, 0xED, 0x95, 0x9C, 0xF0, 0x9F, 0x98, 0x80});

    {   // WriteNullString golden
        MemoryPackWriter w; w.WriteNullString();
        CHECK(bytes_eq(w.Data(), w.Size(), {0xFF, 0xFF, 0xFF, 0xFF}));
    }

    // Round-trip (write -> read), including multi-byte UTF-8.
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
    {   // null round-trip
        MemoryPackWriter w; w.WriteNullString();
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(!r.ReadString().has_value());
    }

    // UTF-16 read path: positive header = UTF-16LE code-unit count.
    {   // "ABC" as UTF-16LE
        uint8_t d[] = {0x03, 0, 0, 0, 0x41, 0x00, 0x42, 0x00, 0x43, 0x00};
        MemoryPackReader r(d, sizeof(d));
        auto got = r.ReadString();
        CHECK(got.has_value() && *got == "ABC");
    }
    {   // surrogate pair D83D DE00 -> U+1F600 -> F0 9F 98 80
        uint8_t d[] = {0x02, 0, 0, 0, 0x3D, 0xD8, 0x00, 0xDE};
        MemoryPackReader r(d, sizeof(d));
        auto got = r.ReadString();
        CHECK(got.has_value() && *got == "\xF0\x9F\x98\x80");
    }
}

static void test_object() {
    std::printf("[object / IMemoryPackable]\n");
    TestPacket p{42, "ABC"};
    auto data = Serialize(p);
    CHECK(bytes_eq(data.data(), data.size(),
        {0x02, 0x2A, 0, 0, 0, 0xFC, 0xFF, 0xFF, 0xFF, 0x03, 0, 0, 0, 'A', 'B', 'C'}));

    auto back = Deserialize<TestPacket>(data);
    CHECK(back.id == 42);
    CHECK(back.name == "ABC");

    // Version tolerance: header declares 1 member, struct expects 2.
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
}

static void test_collections() {
    std::printf("[collections]\n");
    {   // vector<int32>
        std::vector<int32_t> v{1, 2, 3, -4, 5};
        MemoryPackWriter w; w.WriteVector(v);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(r.ReadVector<int32_t>() == v);
    }
    {   // vector<uint8>
        std::vector<uint8_t> v{0, 1, 2, 255, 128};
        MemoryPackWriter w; w.WriteVector(v);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(r.ReadVector<uint8_t>() == v);
    }
    {   // empty vector
        MemoryPackWriter w; w.WriteVector(std::vector<int32_t>{});
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(r.ReadVector<int32_t>().empty());
    }
    {   // null collection -> empty vector
        MemoryPackWriter w; w.WriteNullCollectionHeader();
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(r.ReadVector<int32_t>().empty());
    }
    {   // vector<string>
        std::vector<std::string> v{"a", "bb", "ccc"};
        MemoryPackWriter w; w.WriteStringVector(v);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(r.ReadStringVector() == v);
    }
}

static void test_arrays() {
    std::printf("[arrays]\n");
    {   // std::array exact round-trip
        std::array<int32_t, 4> a{10, 20, 30, 40};
        MemoryPackWriter w; w.WriteArray(a);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        auto got = r.ReadArray<int32_t, 4>();
        CHECK(got == a);
    }
    {   // std::array read excess: write 5, read into 3 -> first 3, skip 2
        MemoryPackWriter w; w.WriteVector(std::vector<int32_t>{1, 2, 3, 4, 5});
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        auto got = r.ReadArray<int32_t, 3>();
        CHECK(got[0] == 1 && got[1] == 2 && got[2] == 3);
        CHECK(r.IsEnd());
    }
    {   // std::array read shortfall: write 2, read into 4 -> rest zero
        MemoryPackWriter w; w.WriteVector(std::vector<int32_t>{7, 8});
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        auto got = r.ReadArray<int32_t, 4>();
        CHECK(got[0] == 7 && got[1] == 8 && got[2] == 0 && got[3] == 0);
    }
    {   // C-style array excess
        int32_t src[5] = {5, 6, 7, 8, 9};
        MemoryPackWriter w; w.WriteArray(src, 5);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        int32_t dst[3] = {};
        int32_t n = r.ReadArray(dst, 3);
        CHECK(n == 3);
        CHECK(dst[0] == 5 && dst[1] == 6 && dst[2] == 7);
        CHECK(r.IsEnd());
    }
    {   // C-style array shortfall
        int32_t src[2] = {1, 2};
        MemoryPackWriter w; w.WriteArray(src, 2);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        int32_t dst[4] = {-1, -1, -1, -1};
        int32_t n = r.ReadArray(dst, 4);
        CHECK(n == 2);
        CHECK(dst[0] == 1 && dst[1] == 2);
    }
}

static void test_extended() {
    std::printf("[map / tuple / enum]\n");
    {   // std::map
        std::map<int32_t, int32_t> m{{1, 10}, {2, 20}, {3, 30}};
        MemoryPackWriter w; w.WriteMap(m);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        auto got = r.ReadMap<int32_t, int32_t>();
        CHECK(got == m);
    }
    {   // std::unordered_map
        std::unordered_map<int32_t, int32_t> m{{1, 100}, {2, 200}};
        MemoryPackWriter w; w.WriteMap(m);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        auto got = r.ReadUnorderedMap<int32_t, int32_t>();
        CHECK(got == m);
    }
    {   // std::map<string, int>
        std::map<std::string, int32_t> m{{"a", 1}, {"b", 2}};
        MemoryPackWriter w; w.WriteMap(m);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        auto got = r.ReadMap<std::string, int32_t>();
        CHECK(got == m);
    }
    {   // std::tuple
        std::tuple<int32_t, std::string, double> t{99, "x", 1.5};
        MemoryPackWriter w; w.WriteTuple(t);
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        auto got = r.ReadTuple<int32_t, std::string, double>();
        CHECK(std::get<0>(got) == 99);
        CHECK(std::get<1>(got) == "x");
        CHECK(std::get<2>(got) == 1.5);
    }
    {   // enum (uint16 underlying)
        MemoryPackWriter w; w.WriteEnum(Color::Green);
        CHECK(bytes_eq(w.Data(), w.Size(), {0x02, 0x00}));
        auto buf = w.TakeBuffer();
        MemoryPackReader r(buf.data(), buf.size());
        CHECK(r.ReadEnum<Color>() == Color::Green);
    }
}

static void test_buffers_and_errors() {
    std::printf("[buffers / errors]\n");
    {   // external vector buffer
        std::vector<uint8_t> ext;
        MemoryPackWriter w(ext);
        w.WriteInt32(123);
        CHECK(ext.size() == 4);
        CHECK(w.Size() == 4);
    }
    {   // fixed std::array buffer, exact fit
        std::array<uint8_t, 8> buf{};
        MemoryPackWriter w(buf);
        w.WriteInt32(1);
        w.WriteInt32(2);
        CHECK(w.Size() == 8);
        CHECK(w.RemainingCapacity() == 0);
    }
    {   // Clear() reuses a fixed buffer
        std::array<uint8_t, 8> buf{};
        MemoryPackWriter w(buf);
        w.WriteInt32(1);
        w.Clear();
        CHECK(w.Size() == 0);
        w.WriteInt16(7);
        CHECK(w.Size() == 2);
    }
    // fixed buffer overflow throws
    CHECK_THROWS(([]{
        std::array<uint8_t, 2> buf{};
        MemoryPackWriter w(buf);
        w.WriteInt32(1);
    }()));
    // reader underflow throws
    CHECK_THROWS(([]{
        uint8_t d[1] = {0};
        MemoryPackReader r(d, 1);
        r.ReadInt32();
    }()));
    // truncated UTF-8 string (header claims 3 bytes but buffer ends early) throws
    CHECK_THROWS(([]{
        // ~3 = 0xFFFFFFFC, utf16Length = 3, but only 1 utf8 byte present
        uint8_t d[] = {0xFC, 0xFF, 0xFF, 0xFF, 0x03, 0, 0, 0, 0x41};
        MemoryPackReader r(d, sizeof(d));
        r.ReadString();
    }()));
}

int main() {
    test_primitives();
    test_golden_bytes();
    test_string();
    test_string_format();
    test_object();
    test_collections();
    test_arrays();
    test_extended();
    test_buffers_and_errors();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("TESTS FAILED\n");
    return 1;
}
