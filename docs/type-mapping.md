# C# to C++ Type Mapping

Every mapping in the "verified" column is checked byte-for-byte on every CI run
against output captured from the real C# MemoryPack library. The fixture name
tells you where to look in [`tests/fixtures/report.txt`](../tests/fixtures/report.txt).

Legend: ✅ verified against C# · 🚧 works, but not byte-verified · ❌ unsupported

---

## Primitives

| C# | C++ | Bytes | Status | Fixture |
|---|---|---|---|---|
| `bool` | `bool` | 1 | ✅ | `all_primitives` |
| `byte` | `uint8_t` | 1 | ✅ | `all_primitives` |
| `sbyte` | `int8_t` | 1 | ✅ | `all_primitives` |
| `short` | `int16_t` | 2 | ✅ | `all_primitives` |
| `ushort` | `uint16_t` | 2 | ✅ | `all_primitives` |
| `int` | `int32_t` | 4 | ✅ | `all_primitives` |
| `uint` | `uint32_t` | 4 | ✅ | `all_primitives` |
| `long` | `int64_t` | 8 | ✅ | `all_primitives` |
| `ulong` | `uint64_t` | 8 | ✅ | `all_primitives` |
| `float` | `float` | 4 | ✅ | `all_primitives`, `special_floats` |
| `double` | `double` | 8 | ✅ | `all_primitives`, `special_floats` |
| `char` | `char16_t` | 2 | ✅ | `dotnet_types` |
| `enum : T` | `enum class : T` | sizeof(T) | ✅ | `enum_packet` |

`NaN`, infinities and `-0.0` round-trip bit-for-bit.

## Strings

| C# | C++ | Status | Notes |
|---|---|---|---|
| `string` | `std::string` | ✅ | written as UTF-8; all four wire forms are read |
| `string` | `std::u16string` | ✅ | written as UTF-16, which C# also accepts |
| `string?` | `std::optional<std::string>` | ✅ | null is `FF FF FF FF`, distinct from `""` |
| reading only | `std::string_view` via `ReadStringView()` | ✅ | borrows the input buffer, no copy; returns `nullopt` for a UTF-16 payload |
| writing only | `std::string_view`, `const char*` | ✅ | `WriteString` takes a `string_view` |

Fixtures: `string_packet`, `string_top_level*`.

## Collections

| C# | C++ | Status | Fixture |
|---|---|---|---|
| `List<T>`, `T[]` (arithmetic T) | `std::vector<T>` | ✅ | `array_packet` |
| `List<T>`, `T[]` (arithmetic T) | `std::array<T,N>`, C array + count | ✅ | unit tests |
| `List<bool>`, `bool[]` | `std::vector<bool>` | ✅ | `bool_collection` |
| `List<string>` | `std::vector<std::string>` | ✅ | `array_packet` |
| `List<UserType>` | `std::vector<UserType>` | ✅ | `inventory` |
| `List<UserType?>` | `std::vector<std::optional<UserType>>` | ✅ | `list_of_nullable_objects` |
| `List<List<T>>` | `std::vector<std::vector<T>>` | ✅ | `nested_list` |
| `List<UnmanagedStruct>` | `std::vector<T>` + `WriteUnmanagedCollection` | ✅ | `unmanaged_list` |
| `List<T>?` | `std::optional<std::vector<T>>` | ✅ | `null_members` |
| `IEnumerable<T>`, `Memory<T>`, `ArraySegment<T>` | any of the above | 🚧 | identical encoding |
| any | `std::deque<T>`, `std::list<T>` | 🚧 | same encoding as `vector` |

`std::array<T,N>` and C arrays read defensively: a longer incoming collection is
truncated and the excess skipped, a shorter one leaves the tail at its default.

## Dictionaries, sets, pairs

| C# | C++ | Status | Fixture |
|---|---|---|---|
| `Dictionary<K,V>` | `std::map<K,V>` | ✅ | `dict_packet` |
| `Dictionary<K,V>` | `std::unordered_map<K,V>` | ✅ | unit tests |
| `Dictionary<int, UserType>` | `std::map<int32_t, UserType>` | ✅ | `dict_object_packet` |
| `HashSet<T>` | `std::set<T>`, `std::unordered_set<T>` | ✅ | `set_packet` |
| `KeyValuePair<K,V>` | `std::pair<K,V>` | ✅ | `kvp_packet` |

> C# hash containers enumerate in an implementation-defined order while
> `std::map`/`std::set` are sorted. Values round-trip either way, but byte
> equality only holds when C# happens to enumerate in sorted order.

## Objects

| C# | C++ | Status | Fixture |
|---|---|---|---|
| `[MemoryPackable] class` | `MEMORYPACK_DEFINE(T, ...)` | ✅ | `simple_packet`, `many_members` |
| `[MemoryPackable] class` | hand-written `IMemoryPackable<T>` | ✅ | unit tests |
| class with no members | `MEMORYPACK_DEFINE_EMPTY(T)` | ✅ | `empty_packet` |
| nested `[MemoryPackable]` member | `std::optional<T>` (nullable reference) | ✅ | `nested_object` |
| `[MemoryPackOrder(n)]` | member position in `MEMORYPACK_DEFINE` | ✅ | `ordered_packet` |
| `[MemoryPackIgnore]` | omit the member | ✅ | `ordered_packet` |
| `struct` holding a reference type | ordinary object with a header | ✅ | `managed_struct_holder` |
| `GenerateType.VersionTolerant` | `VersionTolerantWriter/Reader` | ✅ | `version_tolerant`, `vt_len_*` |
| `GenerateType.CircularReference` | — | ❌ | |

An object may declare at most **249** members (250–254 are reserved, 255 is null).

## Unmanaged structs

| C# | C++ | Status | Fixture |
|---|---|---|---|
| `[MemoryPackable] struct` with only value fields | `MEMORYPACK_UNMANAGED(T, size)` | ✅ | `vec3_top_level`, `unmanaged_holder` |
| struct with natural-alignment padding | matching C++ struct, padding included | ✅ | `padded_struct_top_level` |
| `[StructLayout(Pack = 1)]` struct | `#pragma pack(push,1)` struct | ✅ | `packed_struct_top_level` |
| `Vector2/3/4`, `Quaternion` | `memorypack::Vector2/3/4`, `Quaternion` | ✅ | `numerics_types` |

`MEMORYPACK_UNMANAGED(T, ExpectedSize)` asserts `sizeof(T) == ExpectedSize`, which
turns a layout mismatch into a compile error instead of a wire bug. The path is
restricted to little-endian targets at compile time.

## Nullability

C# spells null four different ways. The C++ type you pick has to match, and
MemoryPackCpp derives it from `WireNullEncoding<T>` automatically.

| C# | Null bytes | C++ | Status | Fixture |
|---|---|---|---|---|
| `MyClass?` (reference) | `FF` | `std::optional<MyClass>` | ✅ | `null_members` |
| `MyClass?` (reference) | `FF` | `std::unique_ptr<T>`, `std::shared_ptr<T>` | 🚧 | unit tests |
| `string?` | `FF FF FF FF` | `std::optional<std::string>` | ✅ | `null_members` |
| `List<T>?` | `FF FF FF FF` | `std::optional<std::vector<T>>` | ✅ | `null_members` |
| `int?` and other `Nullable<unmanaged>` | `hasValue` byte 0 in a fixed-size blob | `std::optional<T>` | ✅ | `nullable_packet` |
| `Nullable<managed struct>` | `FF` | `WriteNullableObject` / `ReadNullableObject<T>` | ✅ | `nullable_managed_holder` |
| `[MemoryPackUnion]` member, null | `FF` | `std::optional<std::variant<...>>` | ✅ | `union_null` |

`Nullable<managed struct>` is the one case that needs the explicit call: by C++
type alone it is indistinguishable from a nullable reference, but on the wire it
carries an extra `01` marker byte before the value.

## Unions

| C# | C++ | Status | Fixture |
|---|---|---|---|
| `[MemoryPackUnion(tag, typeof(T))]`, tag < 250 | `std::variant` + `MEMORYPACK_UNION_TAG(T, tag)` | ✅ | `union_circle`, `union_rect` |
| tag >= 250 (wide tag) | same | ✅ | `union_wide` |
| null union member | `std::optional<std::variant<...>>` | ✅ | `union_null` |

## Tuples

| C# | C++ | Status | Fixture |
|---|---|---|---|
| `Tuple<...>` (reference) | `std::tuple<...>` | ✅ | `value_tuple` |
| `ValueTuple` with a managed field | `std::pair` / field-by-field | ✅ | `value_tuple` |
| `ValueTuple` fully unmanaged | `MEMORYPACK_UNMANAGED` struct | ✅ | `value_tuple_top_level` |

> A fully-unmanaged `ValueTuple` is copied verbatim and **the CLR reorders its
> fields for packing** — `(int, float, double)` is laid out as `double, int,
> float`. Mirror the observed layout; do not assume declaration order.

## .NET value types

| C# | C++ | Bytes | Status | Fixture |
|---|---|---|---|---|
| `Guid` | `memorypack::Guid` | 16 | ✅ | `guid_top_level` |
| `DateTime` | `memorypack::DateTime` | 8 | ✅ | `datetime_top_level` |
| `TimeSpan` | `memorypack::TimeSpan` | 8 | ✅ | `dotnet_types` |
| `DateTimeOffset` | `memorypack::DateTimeOffset` | 16 | ✅ | `dotnet_types` |
| `decimal` | `memorypack::Decimal` (opaque 16 bytes) | 16 | ✅ | `dotnet_types` |
| `Half` | `memorypack::Half` | 2 | ✅ | `dotnet_types` |
| `Int128` / `UInt128` | `memorypack::Int128` / `UInt128` | 16 | ✅ | `dotnet_types` |
| `Version`, `Uri`, `BigInteger`, `BitArray`, `Type` | — | | ❌ | |

`Guid` parses and formats the canonical text form. `DateTime` and `TimeSpan`
convert to and from `std::chrono`. `Half` converts to and from `float`. `Decimal`
is deliberately opaque — it round-trips exactly, but no arithmetic is provided.

## Adding a mapping

The fixture harness makes this cheap and safe:

1. Add the C# type to `tools/FormatProbe/Types.cs` and a case to `FixtureCases.cs`.
2. `dotnet run --project tools/FormatProbe -- generate tests/fixtures`
3. Read the bytes in `tests/fixtures/report.txt` — that is the ground truth.
4. Implement a `MemoryPackFormatter<T>` specialization (or an
   `IMemoryPackable<T>` one for a user type).
5. Add the case to `tests/interop_tests.cpp` and to the `check-cpp` validation in
   `tools/FormatProbe/Program.cs`.

Both directions are then enforced forever by CI.
