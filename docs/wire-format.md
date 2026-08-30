# MemoryPack Wire Format

This is a complete description of the binary format MemoryPackCpp implements, as
observed from the real C# [MemoryPack](https://github.com/Cysharp/MemoryPack)
library (version **1.21.4**).

Every hex dump below was **captured from actual C# output** by
[`tools/FormatProbe`](../tools/FormatProbe), committed under
[`tests/fixtures/`](../tests/fixtures), and is replayed on every CI run by
`tests/interop_tests.cpp`. Nothing here is inferred from documentation — if a
byte in this document is wrong, a test fails.

Regenerate the fixtures and the raw report with:

```bash
dotnet run --project tools/FormatProbe -- generate tests/fixtures
```

The annotated dump of every case lives in
[`tests/fixtures/report.txt`](../tests/fixtures/report.txt).

---

## Design in one paragraph

MemoryPack is a **zero-encoding** format. There is no varint, no field tag, no
type marker, and no name. Values are written in declaration order at their
natural width, little-endian, so that on a little-endian machine serializing an
array of structs is one `memcpy`. The price is that the sender and the receiver
must agree on the member order — which is exactly what the fixture tests in this
repository verify between C# and C++.

---

## Summary table

| Kind | Encoding | Null |
|---|---|---|
| Primitive | fixed width, little-endian, no header | n/a |
| Object | `[1B memberCount][member0][member1]...` | `[1B 255]` |
| Collection | `[4B int32 count][elem0][elem1]...` | `[4B -1]` |
| String (UTF-8) | `[4B ~utf8ByteCount][4B utf16Length][utf8 bytes]` | `[4B -1]` |
| String (UTF-16) | `[4B utf16Length][UTF-16LE units]` | `[4B -1]` |
| Union | `[1B tag]` or `[1B 250][2B tag]`, then the value | `[1B 255]` |
| Unmanaged struct | the struct's bytes verbatim, padding included | n/a |
| `Nullable<T>` (unmanaged T) | the `Nullable<T>` struct verbatim | `hasValue` byte is 0 |
| `Nullable<T>` (managed T) | `[1B 1][value]` | `[1B 255]` |
| VersionTolerant object | `[1B count][len0]...[lenN-1][members]` | `[1B 255]` |

Reserved object-header values: **250–254**. `250` is the union wide-tag marker
and `255` means null, so an object may declare at most **249** members.

---

## Primitives

Written at their exact width in little-endian order, with no header at all.

| C# | Bytes | C++ |
|---|---|---|
| `bool` | 1 (`00` / `01`) | `bool` |
| `byte` / `sbyte` | 1 | `uint8_t` / `int8_t` |
| `short` / `ushort` | 2 | `int16_t` / `uint16_t` |
| `int` / `uint` | 4 | `int32_t` / `uint32_t` |
| `long` / `ulong` | 8 | `int64_t` / `uint64_t` |
| `float` | 4, IEEE 754 | `float` |
| `double` | 8, IEEE 754 | `double` |
| `char` | 2, a UTF-16 code unit | `char16_t` |
| enum | the underlying integer type | `enum class : T` |

`NaN`, `+Inf`, `-Inf` and `-0.0` are copied bit-for-bit; there is no
normalization. Fixture `special_floats`:

```
06                    memberCount = 6
00 00 C0 FF           float  NaN
00 00 80 7F           float  +Inf
00 00 80 FF           float  -Inf
00 00 00 00 00 00 F8 FF   double NaN
00 00 00 00 00 00 F0 7F   double +Inf
00 00 00 00 00 00 00 80   double -0.0
```

Enums use their declared underlying type, including signed ones. Fixture
`enum_packet` — `ushort Green=2`, `sbyte Neg=-2`, `int B=1000000`:

```
03  02 00  FE  40 42 0F 00
```

---

## Objects

```
[1B memberCount] [member0] [member1] ... [memberN-1]
```

Member **names are never written**. The receiver matches members by position, so
the C# declaration order and the C++ member order must agree.

Fixture `simple_packet` — `class SimplePacket { int Id = 42; string? Name = "ABC"; }`:

```
02                       memberCount = 2
2A 00 00 00              int32 42
FC FF FF FF              ~3  (UTF-8 byte count)
03 00 00 00              utf16Length = 3
41 42 43                 "ABC"
```

A nested object is written inline, complete with its own header. Fixture
`nested_object`:

```
02                       NestedObject: 2 members
05 00 00 00              int32 Id = 5
03                         Item: 3 members
09 00 00 00                int32 ItemId = 9
FC FF FF FF 03 00 00 00 47 65 6D   "Gem"
03 00 00 00                int32 Count = 3
```

An object with no members is a single `00` byte (fixture `empty_packet`); a null
object is a single `FF` byte (fixture `null_object`).

### Version tolerance without the VersionTolerant mode

Because the member count is on the wire, the default layout already tolerates
*appended* members: a reader that knows 3 members and receives a header saying 2
simply leaves the third at its default. This is what `MEMORYPACK_DEFINE`
generates, and it is why members may only ever be **added at the end**.

Removing or reordering a member is a breaking protocol change. Use the
[VersionTolerant](#versiontolerant-objects) layout if you need to remove members.

### `[MemoryPackOrder]` and `[MemoryPackIgnore]`

`[MemoryPackOrder(n)]` decides the position, `[MemoryPackIgnore]` removes the
member and it is not counted. Fixture `ordered_packet`, where `First` has order 0,
`Second` has order 1, and `Ignored` is ignored:

```
02  01 00 00 00  02 00 00 00
```

---

## Collections

```
[4B int32 count] [elem0] [elem1] ... [elemN-1]
```

`count = -1` means null. An empty collection is `00 00 00 00`, which is
**distinct from null** — a difference `std::optional<std::vector<T>>` preserves.

`List<T>`, `T[]`, `IEnumerable<T>`, `Memory<T>` and `ArraySegment<T>` all use this
encoding, so they are interchangeable on the wire.

Fixture `int_list_top_level` — `List<int> { 10, 20, 30 }`:

```
03 00 00 00  0A 00 00 00  14 00 00 00  1E 00 00 00
```

### `bool` elements

`List<bool>` is **one byte per element**, not a bitset. Fixture `bool_collection`
— `Flags = [true, false, true]`, `FlagArray = [false, true]`:

```
02
03 00 00 00 01 00 01
02 00 00 00 00 01
```

> In C++, `std::vector<bool>` is a bit-packed specialization with no `data()`.
> MemoryPackCpp handles it explicitly so it still encodes one byte per element.

### Collections of objects

Each element carries its own object header. Fixture `inventory` —
`{ int OwnerId = 7; List<Item> Items = [3 items] }`:

```
02                        Inventory: 2 members
07 00 00 00               int32 OwnerId = 7
03 00 00 00               3 items
  03 01 00 00 00 FA FF FF FF 05 00 00 00 53 77 6F 72 64 0A 00 00 00    Item{1,"Sword",10}
  03 02 00 00 00 F9 FF FF FF 06 00 00 00 53 68 69 65 6C 64 14 00 00 00 Item{2,"Shield",20}
  03 03 00 00 00 F9 FF FF FF 06 00 00 00 50 6F 74 69 6F 6E 1E 00 00 00 Item{3,"Potion",30}
```

A null element inside a collection is the usual `FF` object-null byte. Fixture
`list_of_nullable_objects`:

```
01  02 00 00 00
    03 01 00 00 00 FE FF FF FF 01 00 00 00 41 01 00 00 00   Item{1,"A",1}
    FF                                                       null
```

Nested collections just nest. Fixture `nested_list` — `[[1,2],[3],[]]`:

```
01  03 00 00 00
    02 00 00 00 01 00 00 00 02 00 00 00
    01 00 00 00 03 00 00 00
    00 00 00 00
```

### Dictionary, HashSet, KeyValuePair

`Dictionary<K,V>` is a collection of key/value pairs written back to back, with
**no per-entry header**:

```
[4B count] [key0][value0] [key1][value1] ...
```

Fixture `dict_packet`, first member `Dictionary<int,int> { 1:10, 2:20, 3:30 }`:

```
03 00 00 00  01 00 00 00 0A 00 00 00  02 00 00 00 14 00 00 00  03 00 00 00 1E 00 00 00
```

`HashSet<T>` is an ordinary collection. `KeyValuePair<K,V>` is the key followed
by the value, with no header — the same shape as a dictionary entry. Fixture
`kvp_packet`:

```
02
01 00 00 00 02 00 00 00                        KeyValuePair<int,int>(1,2)
03 00 00 00 FD FF FF FF 02 00 00 00 6B 76      KeyValuePair<int,string>(3,"kv")
```

> **Ordering caveat.** C# `Dictionary`/`HashSet` enumerate in an
> implementation-defined order, while `std::map`/`std::set` are sorted. Values
> round-trip either way, but the *bytes* only match when the C# side happens to
> enumerate in sorted order. Do not rely on byte equality for hash containers.

---

## Strings

The first `int32` decides everything:

| First `int32` | Meaning |
|---|---|
| `-1` | null |
| `0` | empty string |
| `>= 1` | UTF-16: that many UTF-16LE code units follow |
| `<= -2` | UTF-8: `~value` is the byte count, then an `int32` UTF-16 length, then the bytes |

So the UTF-8 form has **two** `int32` header fields:

```
[4B ~utf8ByteCount] [4B utf16Length] [utf8 bytes...]
```

`~utf8ByteCount` is the one's complement of the byte count (3 bytes -> `~3` = `-4`
= `FC FF FF FF`). `utf16Length` is the number of UTF-16 code units the text would
occupy: 1 for a BMP code point, 2 for a supplementary one (a surrogate pair).

MemoryPackCpp always **writes** UTF-8 and **reads** all four forms, so a C++
sender never has to care which form the C# side prefers.

Fixture `string_packet` — `"Hello"`, `"한글"`, `"😀"`, `""`, `null`:

```
05
FA FF FF FF 05 00 00 00 48 65 6C 6C 6F     "Hello"    (~5, 5 UTF-16 units)
F9 FF FF FF 02 00 00 00 ED 95 9C EA B8 80  "한글"      (~6, 2 UTF-16 units)
FB FF FF FF 02 00 00 00 F0 9F 98 80        "😀"       (~4, 2 units: surrogate pair)
00 00 00 00                                 ""
FF FF FF FF                                 null
```

Note how `"한글"` is 6 UTF-8 bytes but 2 UTF-16 units, and `"😀"` is 4 UTF-8 bytes
but 2 UTF-16 units. The two header fields are genuinely independent.

**Malformed UTF-16 on read.** An unpaired surrogate is decoded as U+FFFD
(`EF BF BD`) rather than being emitted as invalid UTF-8.

---

## Unions

A C# `[MemoryPackUnion(tag, typeof(T))]` hierarchy writes a tag, then the
concrete value **with its own object header**:

```
tag <  250 :  [1B tag]            [value]
tag >= 250 :  [1B 250] [2B tag]   [value]
null       :  [1B 255]
```

Fixture `union_circle` — `UnionHolder { IShape? Shape = new CircleShape { Radius = 2.5f } }`,
where `CircleShape` has tag 0:

```
01        UnionHolder: 1 member
00        union tag 0
01        CircleShape: 1 member
00 00 20 40   float 2.5
```

Fixture `union_wide`, where `WideShape` has tag 300:

```
01  FA 2C 01  01  0C 00 00 00
    ^^ ^^^^^  wide-tag marker 250, then uint16 300
```

Fixture `union_null` is simply `01 FF`.

In C++ a union maps to `std::variant` with `MEMORYPACK_UNION_TAG` declaring each
alternative's tag. A **nullable** union is `std::optional<std::variant<...>>`,
because a bare `std::variant` cannot be empty.

---

## Unmanaged structs

A C# `struct` that contains no reference-type fields is *unmanaged*, and
MemoryPack copies `Unsafe.SizeOf<T>()` bytes of it verbatim — **no object
header**, and **padding bytes are included**.

This is the highest-value fast path in the format and the easiest one to get
wrong, so read carefully:

- The layout is .NET's `LayoutKind.Sequential` with **natural alignment**. It is
  *not* packed. Fixture `padded_struct_top_level`, for
  `struct { byte Tag = 7; int Value = 1000; }`:

  ```
  07 00 00 00 E8 03 00 00        8 bytes: 1 byte, 3 bytes of padding, then the int
  ```

- Only when the C# side declares `[StructLayout(LayoutKind.Sequential, Pack = 1)]`
  does the padding disappear. Fixture `packed_struct_top_level` for the same
  fields is 5 bytes:

  ```
  07 E8 03 00 00
  ```

- A struct that holds a reference type is **not** unmanaged and keeps its object
  header. Fixture `managed_struct_holder` for `struct { int Id; string? Label; }`:

  ```
  01  02 04 00 00 00 FC FF FF FF 03 00 00 00 6C 62 6C
      ^^ the struct still has a memberCount byte
  ```

- `List<UnmanagedStruct>` is `[4B count]` followed by `count * sizeof(T)` bytes in
  one block. Fixture `unmanaged_list` with two `Vec3`:

  ```
  01  02 00 00 00
      00 00 80 3F 00 00 00 40 00 00 40 40
      00 00 80 40 00 00 A0 40 00 00 C0 40
  ```

In C++, declare the mapping with `MEMORYPACK_UNMANAGED(Type, ExpectedSize)`. The
macro asserts `sizeof(Type) == ExpectedSize`, which is what catches a layout
mismatch at compile time instead of on the wire.

> **Big-endian hosts.** A struct cannot be byte-swapped field-by-field without
> knowing its fields, so the unmanaged path is compile-time restricted to
> little-endian targets. Serialize the members individually if you need to
> support a big-endian machine.

> **Padding goes on the wire.** Because the struct is copied verbatim, its
> padding bytes are transmitted too. They are only guaranteed to be zero when the
> object was value-initialized (`T v{};`) - after a plain `T v;` they are
> indeterminate, which makes the output non-reproducible and can disclose stack
> contents. See [security.md](security.md#unmanaged-struct-padding).

---

## `Nullable<T>`

C# has two different encodings depending on whether `T` is unmanaged.

### Unmanaged `T` — the struct is copied verbatim

`Nullable<T>` is itself an unmanaged struct laid out as
`{ bool hasValue; T value; }` with natural alignment, and it is copied whole —
including the padding and including the value bytes when `hasValue` is false.

Fixture `nullable_int_top_level` (`(int?)42`) and `nullable_int_top_level_null`:

```
01 00 00 00 2A 00 00 00      hasValue=1, 3 bytes padding, int32 42
00 00 00 00 00 00 00 00      hasValue=0, value zeroed
```

Sizes follow from the alignment rule: `int?` and `float?` are 8 bytes, `Vec3?`
(align 4, size 12) is 16 bytes, `byte?` is 2 bytes.

### Managed `T` — an explicit marker byte

Fixture `nullable_managed_holder`, for `Nullable<struct { int A; string? B; }>`:

```
01        the holder's memberCount
01        hasValue marker
02 03 00 00 00 FE FF FF FF 01 00 00 00 6E    the struct, with its own header
```

and `nullable_managed_holder_null` is `01 FF`.

### The four nulls, side by side

This is the single most common source of confusion, so the C++ mapping is
explicit about it:

| C# member | Null bytes | C++ type |
|---|---|---|
| `MyClass?` (reference) | `FF` | `std::optional<MyClass>` |
| `string?` | `FF FF FF FF` | `std::optional<std::string>` |
| `List<int>?` | `FF FF FF FF` | `std::optional<std::vector<int32_t>>` |
| `int?` (`Nullable<int>`) | `00 00 00 00 00 00 00 00` | `std::optional<int32_t>` |
| `Nullable<managed struct>` | `FF` preceded by a marker byte when set | `WriteNullableObject` / `ReadNullableObject` |

MemoryPackCpp picks the right one automatically from `WireNullEncoding<T>`;
`Nullable<managed struct>` is the one case that needs the explicit call, because
it is indistinguishable from a nullable reference by type alone.

---

## Tuples

A C# **reference** `Tuple<...>` gets an object header and then its items:

```
02  07 00 00 00  FC FF FF FF 03 00 00 00 72 65 66     Tuple<int,string>(7,"ref")
```

A **`ValueTuple` whose fields are all unmanaged** is treated as an unmanaged
struct and copied verbatim, with **no header** — and the CLR is free to reorder
its fields for packing. Fixture `value_tuple`, member `TripleUnmanaged` for
`(int, float, double)(44, 1.5f, 2.5)`:

```
00 00 00 00 00 00 04 40   double 2.5    <- the 8-byte field comes FIRST
2C 00 00 00               int 44
00 00 C0 3F               float 1.5
```

A `ValueTuple` with any managed field is written as its fields in declaration
order with no header — the same shape as `KeyValuePair`:

```
21 00 00 00 FA FF FF FF 05 00 00 00 74 75 70 6C 65    (33, "tuple")
```

> Mixed-width `ValueTuple` field order is a CLR implementation detail. If you
> must interoperate with one, mirror the observed layout with a
> `MEMORYPACK_UNMANAGED` struct as `tests/interop_types.hpp` does — do not assume
> declaration order.

---

## .NET value types

All of these are unmanaged structs, so they are copied verbatim.

| Type | Size | Layout |
|---|---|---|
| `Guid` | 16 | little-endian `int32`, `int16`, `int16`, then 8 bytes in order (same as `Guid.ToByteArray()`) |
| `DateTime` | 8 | `ulong _dateData`: top 2 bits `DateTimeKind`, low 62 bits ticks |
| `TimeSpan` | 8 | `int64` ticks |
| `DateTimeOffset` | 16 | `int16` offset minutes, 6 bytes padding, then the 8-byte `DateTime` |
| `decimal` | 16 | the raw .NET representation |
| `Half` | 2 | IEEE 754 binary16 |
| `Int128` / `UInt128` | 16 | little-endian |
| `Vector2/3/4`, `Quaternion` | 8/12/16/16 | packed floats |

Fixture `guid_top_level` for `01020304-0506-0708-090a-0b0c0d0e0f10`:

```
04 03 02 01  06 05  08 07  09 0A 0B 0C 0D 0E 0F 10
^^^^^^^^^^^  ^^^^^  ^^^^^  ^^^^^^^^^^^^^^^^^^^^^^^
int32 LE     i16 LE i16 LE 8 bytes verbatim
```

Fixture `datetime_top_level` for `2026-08-30T12:34:56Z`:

```
00 D8 0A 19 93 06 DF 48
                     ^^ 0x48 = 0b01001000, top 2 bits = 01 = Utc
```

Ticks are 100-nanosecond units since `0001-01-01`; the Unix epoch is
`621355968000000000` ticks. `memorypack::DateTime` provides the conversion.

> `DateTimeOffset`'s field order is determined by the CLR's automatic layout, not
> by declaration order. The order above is what MemoryPack 1.21.4 emits and is
> asserted by the `dotnet_types` fixture.

---

## VersionTolerant objects

`[MemoryPackable(GenerateType.VersionTolerant)]` prefixes every member with its
byte length, so a reader can skip members it does not understand — which makes it
safe to **remove** or **replace** a member, not just append one.

```
[1B memberCount] [len0] [len1] ... [lenN-1] [member0] [member1] ...
```

Each length uses MemoryPack's length encoding:

| Value | Encoding |
|---|---|
| `<= 127` | that single byte |
| `<= 65535` | `0x84` then a little-endian `uint16` |
| otherwise | `0x82` then a little-endian `uint32` |

Fixture `version_tolerant` — `{ int Id = 77; string Name = "vt"; float Score = 0.5f }`:

```
03           3 members
04 0A 04     member lengths: 4, 10, 4
4D 00 00 00                                     int32 77
FD FF FF FF 02 00 00 00 76 74                   "vt"  (4 + 4 + 2 = 10 bytes)
00 00 00 3F                                     float 0.5
```

The three length encodings are each locked down by a fixture: `vt_len_120`
(single byte), `vt_len_130` (`84 82 00` = 130), and `vt_len_70000`
(`82 70 11 01 00` = 70000).

This layout costs one to five extra bytes per member. Use it for persisted data
and long-lived protocols; the default layout is better for hot packets.

---

## Endianness

The format is little-endian everywhere. MemoryPackCpp byte-swaps automatically on
a big-endian host for every primitive, string length, collection length and
header — the only exception is the unmanaged-struct fast path, which is
compile-time restricted to little-endian targets because a struct cannot be
swapped without knowing its fields.

---

## What this library does not implement

| Feature | Status |
|---|---|
| `GenerateType.CircularReference` | not implemented — rare in game packets, and it requires an object-identity table |
| `[MemoryPackAllowSerialize]` custom formatters | not implemented as an attribute; write an `IMemoryPackable<T>` specialization instead |
| Brotli / compression wrappers | out of scope — compress the byte range yourself |
| `System.Version`, `Uri`, `BigInteger`, `BitArray`, `Type` | not mapped |

If you need one of these, the fixture harness makes it cheap to add: describe the
C# type in `tools/FormatProbe/Types.cs`, regenerate, read the bytes, then
implement against them.
