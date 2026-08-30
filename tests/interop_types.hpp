#pragma once
// C++ mirrors of the C# types in tools/FormatProbe/Types.cs.
//
// Member order here MUST match the C# declaration order (after [MemoryPackOrder]
// is applied), because MemoryPack serializes by position, not by name.

#include "memorypack/memorypack.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace interop {

// ── Basic objects ──────────────────────────────────────────────────────────────

struct SimplePacket {
    int32_t id = 0;
    std::string name;
};

struct AllPrimitives {
    bool boolValue = false;
    uint8_t byteValue = 0;
    int8_t sbyteValue = 0;
    int16_t shortValue = 0;
    uint16_t ushortValue = 0;
    int32_t intValue = 0;
    uint32_t uintValue = 0;
    int64_t longValue = 0;
    uint64_t ulongValue = 0;
    float floatValue = 0.f;
    double doubleValue = 0.0;
};

struct EmptyPacket {};

struct StringPacket {
    std::string ascii;
    std::string korean;
    std::string emoji;
    std::string empty;
    std::optional<std::string> nullString;
};

// ── Objects and collections of objects ─────────────────────────────────────────

struct Item {
    int32_t itemId = 0;
    std::string itemName;
    int32_t count = 0;
};

struct Inventory {
    int32_t ownerId = 0;
    std::vector<Item> items;
};

struct NestedObject {
    int32_t id = 0;
    std::optional<Item> child;
};

struct NestedList {
    std::vector<std::vector<int32_t>> rows;
};

struct ListOfNullableObjects {
    std::vector<std::optional<Item>> items;
};

struct NullMembers {
    std::optional<std::string> nullString;
    std::string emptyString;
    std::optional<std::vector<int32_t>> nullList;
    std::vector<int32_t> emptyList;
    std::optional<Item> nullChild;
};

struct BoolCollection {
    std::vector<bool> flags;
    std::vector<bool> flagArray;
};

struct ArrayPacket {
    std::vector<int16_t> shorts;
    std::vector<int32_t> ints;
    std::vector<int64_t> longs;
    std::vector<uint8_t> bytes;
    std::vector<int8_t> sbytes;
    std::vector<float> floats;
    std::vector<double> doubles;
    std::vector<std::string> strings;
};

struct ManyMembers {
    int32_t m00 = 0, m01 = 0, m02 = 0, m03 = 0, m04 = 0;
    int32_t m05 = 0, m06 = 0, m07 = 0, m08 = 0, m09 = 0;
};

// [MemoryPackOrder] puts First before Second; [MemoryPackIgnore] drops the third.
struct OrderedPacket {
    int32_t first = 0;
    int32_t second = 0;
};

// ── Enums ──────────────────────────────────────────────────────────────────────

enum class ColorU16 : uint16_t { Red = 1, Green = 2, Blue = 3 };
enum class ColorI8 : int8_t { Neg = -2, Zero = 0, Pos = 5 };
enum class ColorI32 : int32_t { A = 0, B = 1000000 };

struct EnumPacket {
    ColorU16 u16 = ColorU16::Red;
    ColorI8 i8 = ColorI8::Zero;
    ColorI32 i32 = ColorI32::A;
};

// ── Unmanaged structs ──────────────────────────────────────────────────────────
// These mirror C# structs with no reference-type fields, which MemoryPack copies
// verbatim. The C++ layout must match the .NET one, padding included, so the
// MEMORYPACK_UNMANAGED size assertions below are load-bearing.

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
    friend bool operator==(const Vec3&, const Vec3&) = default;
};

// C# `struct { byte Tag; int Value; }` is 8 bytes: 1 byte, 3 bytes of padding,
// then the int at offset 4 - it is NOT packed.
struct PaddedStruct {
    uint8_t tag = 0;
    int32_t value = 0;
    friend bool operator==(const PaddedStruct&, const PaddedStruct&) = default;
};

#pragma pack(push, 1)
struct PackedStruct {
    uint8_t tag = 0;
    int32_t value = 0;
    friend bool operator==(const PackedStruct&, const PackedStruct&) = default;
};
#pragma pack(pop)

struct UnmanagedHolder {
    Vec3 position;
    PaddedStruct padded;
};

struct UnmanagedList {
    std::vector<Vec3> points;
};

// A C# struct that holds a string is NOT unmanaged: it keeps an object header.
struct ManagedStruct {
    int32_t id = 0;
    std::string label;
};

struct ManagedStructHolder {
    ManagedStruct value;
};

struct NumericsTypes {
    memorypack::Vector2 v2;
    memorypack::Vector3 v3;
    memorypack::Vector4 v4;
    memorypack::Quaternion quat;
};

// ── Nullable<T> ────────────────────────────────────────────────────────────────

struct NullablePacket {
    std::optional<int32_t> maybeInt;
    std::optional<int32_t> nullInt;
    std::optional<float> maybeFloat;
    std::optional<Vec3> maybeVec;
    std::optional<Vec3> nullVec;
};

// C# `Nullable<managed struct>` uses `[1B 1][value]` / `[1B 255]`, unlike a
// nullable reference which has no marker byte.
struct ManagedNullableTarget {
    int32_t a = 0;
    std::string b;
};

struct NullableManagedHolder {
    std::optional<ManagedNullableTarget> value;
};

// ── Dictionaries / sets / pairs ────────────────────────────────────────────────

struct DictPacket {
    std::map<int32_t, int32_t> intMap;
    std::map<std::string, int32_t> stringMap;
    std::map<int32_t, std::string> intToString;
};

struct DictObjectPacket {
    std::map<int32_t, Item> items;
};

struct SetPacket {
    std::set<int32_t> intSet;
    std::set<std::string> stringSet;
};

struct KvpPacket {
    std::pair<int32_t, int32_t> intPair{};
    std::pair<int32_t, std::string> mixedPair{};
};

// ── Tuples ─────────────────────────────────────────────────────────────────────
// A C# ValueTuple whose fields are all unmanaged is copied verbatim, and the CLR
// is free to reorder its fields for packing. `(int, float, double)` really is
// laid out as double, int, float - mirror the observed layout, do not assume
// declaration order.

struct ValueTuple2i {
    int32_t item1 = 0;
    int32_t item2 = 0;
    friend bool operator==(const ValueTuple2i&, const ValueTuple2i&) = default;
};

struct ValueTupleIFD {
    double item3 = 0.0;   // CLR places the 8-byte field first
    int32_t item1 = 0;
    float item2 = 0.f;
    friend bool operator==(const ValueTupleIFD&, const ValueTupleIFD&) = default;
};

struct TuplePacket {
    std::optional<std::tuple<int32_t, std::string>> refTuple;
    ValueTuple2i unmanagedValueTuple;
    std::pair<int32_t, std::string> mixedValueTuple;
    ValueTupleIFD tripleUnmanaged;
};

// ── .NET types ─────────────────────────────────────────────────────────────────

struct DotNetTypes {
    memorypack::Guid guidValue;
    memorypack::DateTime dateTimeValue;
    memorypack::TimeSpan timeSpanValue;
    memorypack::DateTimeOffset dateTimeOffsetValue;
    char16_t charValue = u'\0';
    memorypack::Decimal decimalValue;
    memorypack::Half halfValue;
    memorypack::Int128 int128Value;
    memorypack::UInt128 uint128Value;
};

// ── Unions ─────────────────────────────────────────────────────────────────────

struct CircleShape { float radius = 0.f; };
struct RectShape { float width = 0.f; float height = 0.f; };
struct WideShape { int32_t sides = 0; };

using Shape = std::variant<CircleShape, RectShape, WideShape>;

struct UnionHolder {
    std::optional<Shape> shape;
};

// ── Special float values ───────────────────────────────────────────────────────

struct SpecialFloats {
    float nanF = 0.f;
    float posInfF = 0.f;
    float negInfF = 0.f;
    double nanD = 0.0;
    double posInfD = 0.0;
    double negZeroD = 0.0;
};

// ── VersionTolerant ────────────────────────────────────────────────────────────

struct VersionTolerantPacket {
    int32_t id = 0;
    std::string name;
    float score = 0.f;
};

struct VersionTolerantLong {
    int32_t id = 0;
    std::string big;
    int32_t tail = 0;
};

} // namespace interop

// ── Serializer definitions ─────────────────────────────────────────────────────

MEMORYPACK_UNMANAGED(interop::Vec3, 12)
MEMORYPACK_UNMANAGED(interop::PaddedStruct, 8)
MEMORYPACK_UNMANAGED(interop::PackedStruct, 5)
MEMORYPACK_UNMANAGED(interop::ValueTuple2i, 8)
MEMORYPACK_UNMANAGED(interop::ValueTupleIFD, 16)

MEMORYPACK_UNION_TAG(interop::CircleShape, 0)
MEMORYPACK_UNION_TAG(interop::RectShape, 1)
MEMORYPACK_UNION_TAG(interop::WideShape, 300)

MEMORYPACK_DEFINE(interop::SimplePacket, id, name)
MEMORYPACK_DEFINE(interop::AllPrimitives, boolValue, byteValue, sbyteValue, shortValue,
                  ushortValue, intValue, uintValue, longValue, ulongValue, floatValue, doubleValue)
MEMORYPACK_DEFINE_EMPTY(interop::EmptyPacket)
MEMORYPACK_DEFINE(interop::StringPacket, ascii, korean, emoji, empty, nullString)
MEMORYPACK_DEFINE(interop::Item, itemId, itemName, count)
MEMORYPACK_DEFINE(interop::Inventory, ownerId, items)
MEMORYPACK_DEFINE(interop::NestedObject, id, child)
MEMORYPACK_DEFINE(interop::NestedList, rows)
MEMORYPACK_DEFINE(interop::ListOfNullableObjects, items)
MEMORYPACK_DEFINE(interop::NullMembers, nullString, emptyString, nullList, emptyList, nullChild)
MEMORYPACK_DEFINE(interop::BoolCollection, flags, flagArray)
MEMORYPACK_DEFINE(interop::ArrayPacket, shorts, ints, longs, bytes, sbytes, floats, doubles, strings)
MEMORYPACK_DEFINE(interop::ManyMembers, m00, m01, m02, m03, m04, m05, m06, m07, m08, m09)
MEMORYPACK_DEFINE(interop::OrderedPacket, first, second)
MEMORYPACK_DEFINE(interop::EnumPacket, u16, i8, i32)
MEMORYPACK_DEFINE(interop::UnmanagedHolder, position, padded)
MEMORYPACK_DEFINE(interop::ManagedStruct, id, label)
MEMORYPACK_DEFINE(interop::ManagedStructHolder, value)
MEMORYPACK_DEFINE(interop::NumericsTypes, v2, v3, v4, quat)
MEMORYPACK_DEFINE(interop::NullablePacket, maybeInt, nullInt, maybeFloat, maybeVec, nullVec)
MEMORYPACK_DEFINE(interop::ManagedNullableTarget, a, b)
MEMORYPACK_DEFINE(interop::DictPacket, intMap, stringMap, intToString)
MEMORYPACK_DEFINE(interop::DictObjectPacket, items)
MEMORYPACK_DEFINE(interop::SetPacket, intSet, stringSet)
MEMORYPACK_DEFINE(interop::KvpPacket, intPair, mixedPair)
MEMORYPACK_DEFINE(interop::TuplePacket, refTuple, unmanagedValueTuple, mixedValueTuple, tripleUnmanaged)
MEMORYPACK_DEFINE(interop::DotNetTypes, guidValue, dateTimeValue, timeSpanValue, dateTimeOffsetValue,
                  charValue, decimalValue, halfValue, int128Value, uint128Value)
MEMORYPACK_DEFINE(interop::SpecialFloats, nanF, posInfF, negInfF, nanD, posInfD, negZeroD)

// UnmanagedList uses the bulk unmanaged-collection encoding rather than the
// generic per-element one, so it is written by hand.
namespace memorypack {
template<>
struct IMemoryPackable<interop::UnmanagedList> {
    static void Serialize(MemoryPackWriter& w, const interop::UnmanagedList* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(1);
        w.WriteUnmanagedCollection(std::span<const interop::Vec3>(v->points));
    }
    static void Deserialize(MemoryPackReader& r, interop::UnmanagedList& v) {
        const auto header = r.ReadObjectHeader();
        if (header.isNull) return;
        if (header.count >= 1) r.ReadUnmanagedCollection(v.points);
    }
};

// C# `Nullable<managed struct>` carries a `[1B 1]` marker before the value.
template<>
struct IMemoryPackable<interop::NullableManagedHolder> {
    static void Serialize(MemoryPackWriter& w, const interop::NullableManagedHolder* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(1);
        w.WriteNullableObject(v->value);
    }
    static void Deserialize(MemoryPackReader& r, interop::NullableManagedHolder& v) {
        const auto header = r.ReadObjectHeader();
        if (header.isNull) return;
        if (header.count >= 1) v.value = r.ReadNullableObject<interop::ManagedNullableTarget>();
    }
};

// Unions live behind a tag byte; the holder's member is a nullable union.
template<>
struct IMemoryPackable<interop::UnionHolder> {
    static void Serialize(MemoryPackWriter& w, const interop::UnionHolder* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(1);
        w.Write(v->shape);
    }
    static void Deserialize(MemoryPackReader& r, interop::UnionHolder& v) {
        const auto header = r.ReadObjectHeader();
        if (header.isNull) return;
        if (header.count >= 1) r.Read(v.shape);
    }
};

// VersionTolerant objects prefix every member with its byte length.
template<>
struct IMemoryPackable<interop::VersionTolerantPacket> {
    static void Serialize(MemoryPackWriter& w, const interop::VersionTolerantPacket* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        VersionTolerantWriter vt(w);
        vt.WriteMember(v->id);
        vt.WriteMember(v->name);
        vt.WriteMember(v->score);
    }
    static void Deserialize(MemoryPackReader& r, interop::VersionTolerantPacket& v) {
        VersionTolerantReader vt(r);
        if (vt.IsNull()) return;
        vt.ReadMember(v.id);
        vt.ReadMember(v.name);
        vt.ReadMember(v.score);
        vt.Finish();
    }
};

template<>
struct IMemoryPackable<interop::VersionTolerantLong> {
    static void Serialize(MemoryPackWriter& w, const interop::VersionTolerantLong* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        VersionTolerantWriter vt(w);
        vt.WriteMember(v->id);
        vt.WriteMember(v->big);
        vt.WriteMember(v->tail);
    }
    static void Deserialize(MemoryPackReader& r, interop::VersionTolerantLong& v) {
        VersionTolerantReader vt(r);
        if (vt.IsNull()) return;
        vt.ReadMember(v.id);
        vt.ReadMember(v.big);
        vt.ReadMember(v.tail);
        vt.Finish();
    }
};

// CircleShape / RectShape / WideShape are ordinary objects behind their tag.
} // namespace memorypack

MEMORYPACK_DEFINE(interop::CircleShape, radius)
MEMORYPACK_DEFINE(interop::RectShape, width, height)
MEMORYPACK_DEFINE(interop::WideShape, sides)
