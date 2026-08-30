# FAQ

## Is this an official Cysharp project?

No. MemoryPackCpp is an independent implementation of the MemoryPack binary wire
format. It is not endorsed by or affiliated with Cysharp. MemoryPack itself is
MIT licensed and © Cysharp, Inc.

## How do you know the bytes actually match C#?

Because the tests replay bytes produced by the real C# library.
[`tools/FormatProbe`](../tools/FormatProbe) is a .NET program that references the
actual MemoryPack NuGet package, serializes 53 cases, and writes the bytes to
[`tests/fixtures/`](../tests/fixtures). CI then asserts three things on every push:

1. the C++ reader decodes those bytes to the expected values,
2. the C++ writer re-emits byte-identical output, and
3. C# reads back what C++ produced (`FormatProbe check-cpp`).

Nothing in the documentation is inferred — if a byte is wrong, a test fails.

---

## Wire format

### Why does a string header have two `int32` fields?

`[~utf8ByteCount][utf16Length][bytes]`. The first tells a reader how many bytes to
consume; the second tells a C# reader how large a `string` to allocate *before*
decoding, since .NET strings are UTF-16. Pre-sizing the destination is why
MemoryPack can decode a string in one pass with no reallocation.

The two are genuinely independent: `"한글"` is 6 UTF-8 bytes but 2 UTF-16 units,
and `"😀"` is 4 UTF-8 bytes but 2 UTF-16 units (a surrogate pair).

### Why is there no varint?

That is the whole design. Varints save bytes but cost branches, and MemoryPack
optimizes for throughput on a little-endian machine: a fixed-width little-endian
layout means an array of structs is one `memcpy` in each direction. If you need
smaller payloads, compress the resulting byte range.

### Why does my 5-byte C# struct take 8 bytes on the wire?

Because .NET lays out `struct { byte Tag; int Value; }` with natural alignment:
1 byte, 3 bytes of padding, then the 4-byte int. MemoryPack copies the struct
verbatim, padding included. Only `[StructLayout(LayoutKind.Sequential, Pack = 1)]`
removes it — and then the C++ side needs `#pragma pack(push, 1)` to match.

This is verified by the `padded_struct_top_level` and `packed_struct_top_level`
fixtures.

### Does C# have to use UTF-8 strings for this to work?

No. The C# reader detects the encoding from the sign of the string header, so a
C++ sender can always write UTF-8 and be understood. This library reads all four
forms (null, empty, UTF-16, UTF-8) and writes UTF-8 by default. Use
`WriteStringUtf16` if you specifically want the UTF-16 form.

### Can I add a field to a packet without breaking the other side?

You can **append** one. The member count is on the wire, so a reader that knows
more members than the sender wrote leaves the extras at their defaults, and a
reader that knows fewer stops early. Both directions are safe.

You cannot reorder or remove a member — MemoryPack matches by position. If you
need to remove members over time, use the
[VersionTolerant layout](wire-format.md#versiontolerant-objects), which prefixes
each member with its length so unknown ones can be skipped.

---

## C++ usage

### I get "no serializer for this type"

You have not declared how the type is serialized. Add
`MEMORYPACK_DEFINE(YourType, member1, member2, ...)` at global scope, after the
struct definition. See [serialization.md](serialization.md).

### `MEMORYPACK_DEFINE` does not compile inside my namespace

It opens `namespace memorypack` internally, so it must be used at global scope.
Qualify the type instead:

```cpp
namespace game { struct PlayerState { int32_t id; std::string name; }; }
MEMORYPACK_DEFINE(game::PlayerState, id, name)     // global scope
```

### Which `std::optional` do I use for a nullable C# member?

Whatever matches the C# type — the library derives the encoding from it:

| C# | C++ |
|---|---|
| `string?` | `std::optional<std::string>` |
| `List<int>?` | `std::optional<std::vector<int32_t>>` |
| `MyClass?` | `std::optional<MyClass>` |
| `int?` | `std::optional<int32_t>` |

The one exception is C# `Nullable<T>` where `T` is a *managed* struct: it carries
an extra marker byte, so use `WriteNullableObject` / `ReadNullableObject<T>`
explicitly.

### Why does `std::vector<bool>` need special handling?

`std::vector<bool>` is a bit-packed specialization with no `data()`, so the bulk
`memcpy` path does not apply. The library handles it explicitly so it still
encodes one byte per element, matching C# `List<bool>`. You do not have to do
anything — it just works.

### How do I represent a nullable union?

`std::optional<std::variant<...>>`. A bare `std::variant` always holds one of its
alternatives and cannot be empty, but a C# union member is a reference and can be
null.

### Can I use this without exceptions?

Yes. Build with `-fno-exceptions` (or define `MEMORYPACK_NO_EXCEPTIONS`) and
errors surface through `reader.Failed()` / `reader.Error()` and the
`TryDeserialize` / `TrySerializeTo` API. This is the configuration Unreal Engine
and most console toolchains need. See [error-handling.md](error-handling.md).

### MSVC warns C4819 when I include the headers

That warning means a source file contains bytes that are not valid in the current
codepage. The library headers are **ASCII-only** specifically to avoid it, and CI
enforces that. If you still see it, the file is one of yours — add `/utf-8` to
your compile options, which is good practice regardless.

### `MemoryPackWriter` used to be non-movable

It is movable as of 0.2.0, so you can store one in a container or return it from a
factory. It is still non-copyable.

---

## Interop troubleshooting

### C# reads my packet but the values are shifted

A member is missing or in the wrong position on one side. Serialization is
positional. Compare the two member lists in declaration order, and remember that
`[MemoryPackIgnore]` members do not appear on the wire while `[MemoryPackOrder]`
changes the position.

### Deserialization throws `InvalidHeader` at offset 0

You are probably passing the whole framed packet instead of just the body. The
`[2B packetId][4B bodyLength]` header is your framing, not part of the MemoryPack
payload — deserialize `body`, not `packet`.

### How do I debug a mismatch quickly?

Turn the argument into a diff:

1. Add the C# type to `tools/FormatProbe/Types.cs` and a case to `FixtureCases.cs`.
2. `dotnet run --project tools/FormatProbe -- generate tests/fixtures`
3. Read the annotated hex dump in `tests/fixtures/report.txt`.

That is ground truth. Then make the C++ produce the same bytes.

### Can I stop this from happening at all?

Three defences, cheapest first:

1. Generate the C++ header from the C# definitions with
   [`tools/cs2cpp`](../tools/cs2cpp), and run it with `--check` in CI.
2. Use `DeserializeExact` in debug builds so leftover bytes are reported.
3. Exchange a schema hash at connect time (cs2cpp can emit one for both sides).

---

## Project

### Which header should I include?

`memorypack/memorypack.hpp` covers everything except the optional TCP framing
helpers in `memorypack/packet.hpp`. For faster compiles, include only
`memorypack/core.hpp` — it has the writer, reader, strings, objects, collections,
unions and unmanaged structs, which is most packet code.

### Is there a single-file version?

Yes:

```bash
python tools/amalgamate.py --include-packet -o dist/memorypack.hpp
```

Releases also ship one as an attachment.

### What is not implemented?

`GenerateType.CircularReference`, compression wrappers, and a handful of rarely
used .NET types (`Version`, `Uri`, `BigInteger`, `BitArray`). See the end of
[wire-format.md](wire-format.md#what-this-library-does-not-implement).

### How do I add a missing type?

The fixture harness makes it safe: describe the C# type in
`tools/FormatProbe/Types.cs`, regenerate, read the real bytes, implement against
them, and add the case to `tests/interop_tests.cpp`. Both directions are then
enforced by CI forever. Details in
[type-mapping.md](type-mapping.md#adding-a-mapping).
