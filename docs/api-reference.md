# API Reference

A complete list of the public API. For how to use it, start with
[serialization.md](serialization.md); for what the bytes mean, see
[wire-format.md](wire-format.md).

---

## Top-level functions

| Function | Description |
|---|---|
| `std::vector<uint8_t> Serialize(const T&)` | Serializes into a new buffer |
| `void Serialize(const T&, std::vector<uint8_t>& out)` | Appends into a caller-owned buffer |
| `size_t SerializeTo(std::span<uint8_t>, const T&)` | Serializes into a fixed buffer; returns bytes written, or 0 on failure |
| `T Deserialize<T>(std::span<const uint8_t>)` | Deserializes and returns by value |
| `T Deserialize<T>(const std::vector<uint8_t>&)` | Same, from a vector |
| `void Deserialize(const uint8_t*, size_t, T& out)` | Deserializes into an existing object |
| `T DeserializeExact<T>(std::span<const uint8_t>)` | Also requires the whole input to be consumed |
| `std::expected<T, MemoryPackError> TryDeserialize<T>(std::span<const uint8_t>)` | Exception-free deserialization |
| `std::expected<size_t, MemoryPackError> TrySerializeTo(std::span<uint8_t>, const T&)` | Exception-free serialization |

The `Try*` functions exist only when `MEMORYPACK_HAS_EXPECTED` is 1.

---

## `MemoryPackWriter`

### Construction

```cpp
MemoryPackWriter w;                                   // internal growable buffer
MemoryPackWriter w(std::vector<uint8_t>& external);   // appends to a caller-owned vector
MemoryPackWriter w(uint8_t* data, size_t capacity);   // fixed buffer
MemoryPackWriter w(std::span<uint8_t>);               // fixed buffer
MemoryPackWriter w(std::span<std::byte>);             // fixed buffer
MemoryPackWriter w(std::array<uint8_t, N>&);          // fixed buffer
```

Non-copyable, movable.

### Headers

| Method | Description |
|---|---|
| `WriteObjectHeader(uint8_t memberCount)` | Object header; fails above 249 |
| `WriteNullObjectHeader()` | Null object (`0xFF`) |
| `WriteCollectionHeader(int32_t length)` | Collection element count |
| `WriteNullCollectionHeader()` | Null collection (`-1`) |
| `WriteUnionHeader(uint16_t tag)` | `[1B tag]`, or `[1B 250][2B tag]` for tag >= 250 |
| `WriteNullUnionHeader()` | Null union (`0xFF`) |
| `WriteVarIntLength(uint32_t)` | VersionTolerant member length |

### Primitives

`WriteBool`, `WriteInt8`, `WriteUInt8`, `WriteInt16`, `WriteUInt16`, `WriteInt32`,
`WriteUInt32`, `WriteInt64`, `WriteUInt64`, `WriteFloat`, `WriteDouble`,
`WriteChar16`, `WriteEnum<T>`.

### Strings

| Method | Description |
|---|---|
| `WriteString(std::string_view)` | UTF-8 string (binds `const char*` and `std::string`) |
| `WriteNullString()` | Null string (`-1`) |
| `WriteOptionalString(const std::optional<std::string>&)` | Value or null |
| `WriteStringUtf16(std::u16string_view)` | UTF-16 form |

### Collections

| Method | Description |
|---|---|
| `WriteVector(const std::vector<T>&)` | Arithmetic elements, bulk copy |
| `WriteVector(const std::vector<bool>&)` | One byte per element |
| `WriteStringVector(const std::vector<std::string>&)` | Collection of strings |
| `WriteArray(const T* arr, int32_t count)` | C array, writes `count` elements |
| `WriteArray(const std::array<T, N>&)` | Fixed-size array |
| `WriteCollection(std::span<const T>)` | Any serializable element type |
| `WriteCollection(const std::vector<T, A>&)` | Any serializable element type |
| `WriteUnmanagedCollection(std::span<const T>)` | Bulk copy of layout-compatible structs |
| `WriteMap(const std::map<K,V>&)` | Key/value pairs |
| `WriteMap(const std::unordered_map<K,V>&)` | Key/value pairs |
| `WriteTuple(const std::tuple<Ts...>&)` | Object header plus items |

### Nullable and raw

| Method | Description |
|---|---|
| `WriteOptional(const std::optional<T>&)` | Value or null object header |
| `WritePointer(const T*)` | Value or null object header |
| `WriteNullable(const std::optional<T>&)` | C# `Nullable<T>` for unmanaged `T` |
| `WriteNullableObject(const std::optional<T>&)` | C# `Nullable<T>` for managed `T` |
| `WriteUnmanaged(const T&)` | Struct bytes verbatim |
| `WriteBytes(std::span<const uint8_t>)` | Raw bytes, no header |
| `Write(const T&)` | **Generic dispatch — the recommended entry point** |

### Buffer access and state

| Method | Description |
|---|---|
| `Data()` | Pointer to the written bytes |
| `Size()` | Number of bytes written |
| `GetSpan()` | `std::span<const uint8_t>` over the written bytes |
| `GetBuffer()` | The underlying vector (vector modes only) |
| `TakeBuffer()` | Moves the internal buffer out (owned mode only) |
| `RemainingCapacity()` | Space left in a fixed buffer |
| `Reserve(size_t)` | Pre-allocates capacity |
| `Clear()` | Resets the write position, keeping capacity |
| `Error()` / `Failed()` | Error state (used when exceptions are off) |

---

## `MemoryPackReader`

### Construction

```cpp
MemoryPackReader r(const uint8_t* data, size_t size);
MemoryPackReader r(std::span<const uint8_t>);
MemoryPackReader r(std::span<const std::byte>);
MemoryPackReader r(std::span<const uint8_t>, const ReaderOptions&);
```

### Headers

| Method | Returns | Description |
|---|---|---|
| `ReadObjectHeader()` | `ObjectHeader{count, isNull}` | Rejects the reserved 250-254 range |
| `PeekIsNull()` | `bool` | Whether the next byte is the null object header |
| `ReadCollectionHeader()` | `int32_t` | Raw length; `-1` is null |
| `ReadCollectionLength(size_t minElementSize)` | `int32_t` | Length validated against the remaining input and the configured limits |
| `ReadUnionHeader()` | `std::optional<uint16_t>` | `nullopt` for a null union |
| `ReadVarIntLength()` | `uint32_t` | VersionTolerant member length |

### Primitives

`ReadBool`, `ReadInt8`, `ReadUInt8`, `ReadInt16`, `ReadUInt16`, `ReadInt32`,
`ReadUInt32`, `ReadInt64`, `ReadUInt64`, `ReadFloat`, `ReadDouble`, `ReadChar16`,
`ReadEnum<T>`, `ReadByte`.

### Strings

| Method | Returns | Description |
|---|---|---|
| `ReadString()` | `std::optional<std::string>` | `nullopt` for null; handles all four wire forms |
| `ReadString(std::string& out)` | `bool` | In-place; `false` for null |
| `ReadStringView()` | `std::optional<std::string_view>` | Zero-copy view; `nullopt` for null or a UTF-16 payload |

### Collections

| Method | Returns | Description |
|---|---|---|
| `ReadVector<T>()` | `std::vector<T>` | Arithmetic elements |
| `ReadVector(std::vector<T>& out)` | `void` | In-place |
| `ReadStringVector()` / `ReadStringVector(out)` | `std::vector<std::string>` | Strings |
| `ReadArray(T* arr, int32_t maxCount)` | `int32_t` | Into a C array; skips the excess |
| `ReadArray<T, N>()` | `std::array<T, N>` | Into a fixed-size array |
| `ReadCollection<T>()` / `ReadCollection(out)` | `std::vector<T>` | Any serializable element type |
| `ReadUnmanagedCollection<T>()` / `(out)` | `std::vector<T>` | Bulk copy |
| `ReadMap<K,V>()` | `std::map<K,V>` | |
| `ReadUnorderedMap<K,V>()` | `std::unordered_map<K,V>` | |
| `ReadTuple<Ts...>()` | `std::tuple<Ts...>` | |

### Nullable and raw

| Method | Returns |
|---|---|
| `ReadOptional<T>()` | `std::optional<T>` — nullable reference |
| `ReadNullable<T>()` | `std::optional<T>` — C# `Nullable<T>` for unmanaged `T` |
| `ReadNullableObject<T>()` | `std::optional<T>` — C# `Nullable<T>` for managed `T` |
| `ReadUnmanaged<T>()` / `ReadUnmanaged(T& out)` | `T` — struct bytes verbatim |
| `Read(T& out)` / `Read<T>()` | **Generic dispatch — the recommended entry point** |

### Position and state

| Method | Description |
|---|---|
| `Position()` / `Remaining()` / `IsEnd()` | Cursor state |
| `Advance(size_t)` / `Seek(size_t)` / `Reset()` | Move the cursor |
| `SubReader(size_t length)` | A reader bounded to the next `length` bytes |
| `RequireEnd()` | Fails if any input is left |
| `EnterObject()` / `LeaveObject()` | Depth tracking (used by generated readers) |
| `Options()` / `SetOptions()` | Read limits |
| `Error()` / `Failed()` | Error state |

---

## Macros

| Macro | Purpose |
|---|---|
| `MEMORYPACK_DEFINE(Type, m1, m2, ...)` | Generates the serializer from a member list (global scope, up to 32 members) |
| `MEMORYPACK_DEFINE_EMPTY(Type)` | For a type with no serialized members |
| `MEMORYPACK_UNMANAGED(Type, ExpectedSize)` | Maps a C# unmanaged struct; asserts `sizeof`. Unchecked with respect to padding - see [docs/security.md#unmanaged-struct-padding](security.md#unmanaged-struct-padding) |
| `MEMORYPACK_UNMANAGED_EXACT(Type, ExpectedSize, m1, m2, ...)` | Like `MEMORYPACK_UNMANAGED`, plus a compile-time proof that `Type` has no padding (member sizes sum to `sizeof(Type)`). Zero runtime cost; use for `[StructLayout(Pack = 1)]` structs or any naturally packed type |
| `MEMORYPACK_UNMANAGED_SCRUBBED(Type, ExpectedSize, m1, m2, ...)` | Like `MEMORYPACK_UNMANAGED`, for a type that DOES have padding: `Serialize` builds the wire bytes in a zero-filled buffer at each member's real offset, so padding is always zero regardless of the source object. Costs a small stack buffer and a per-member `memcpy` |
| `MEMORYPACK_UNION_TAG(Type, Tag)` | Declares a `std::variant` alternative's union tag |
| `MEMORYPACK_NO_EXCEPTIONS` | Define before including to force the exception-free path |
| `MEMORYPACK_HAS_EXCEPTIONS` | 1 when exceptions are in use |
| `MEMORYPACK_HAS_EXPECTED` | 1 when the `Try*` API is available |
| `MEMORYPACK_VERSION_MAJOR/MINOR/PATCH/STRING` | Library version |

---

## Types

| Type | Purpose |
|---|---|
| `MemoryPackError` | Error code enum; `ToString(MemoryPackError)` renders it |
| `MemoryPackException` | Thrown on failure when exceptions are enabled; carries `code()` and `offset()` |
| `ReaderOptions` | `maxCollectionLength`, `maxStringLength`, `maxDepth` |
| `ObjectHeader` | `{ uint8_t count; bool isNull; }` |
| `IMemoryPackable<T>` | User extension point |
| `MemoryPackFormatter<T>` | Library-level dispatch; specialize to add a new type mapping |
| `IsUnmanaged<T>` | Marks a type as a C# unmanaged struct |
| `WireNullEncoding<T>` | Which of C#'s four null encodings `T` uses |
| `NullableLayout<T>` | Byte layout of C# `Nullable<T>` |
| `MemoryPackUnionTag<T>` | A union alternative's tag |
| `Serializable<T>` | Concept: true when `Write<T>`/`Read<T>` can handle `T` |
| `VersionTolerantWriter` / `VersionTolerantReader` | The `GenerateType.VersionTolerant` layout |

### Constants

`NULL_OBJECT` (255), `WIDE_TAG` (250), `MAX_MEMBER_COUNT` (249),
`NULL_COLLECTION` (-1), `VARINT_MAX_SINGLE` (127), `VARINT_UINT16_TAG` (0x84),
`VARINT_UINT32_TAG` (0x82).

---

## Packet framing (`memorypack/packet.hpp`)

Not included by the umbrella header.

| API | Description |
|---|---|
| `PACKET_HEADER_SIZE` | 6 (`[2B packetId][4B bodyLength]`) |
| `PacketHeader` | `{ uint16_t id; int32_t bodyLength; }` |
| `WritePacket(out, id, body)` | Serializes behind a reserved header and patches the length |
| `MakePacket(id, body)` | Same, into a new buffer |
| `PeekPacketHeader(span)` | `std::optional<PacketHeader>` |
| `PacketFrameParser` | TCP stream reassembly with a maximum body length |
| `DefaultPacketHeaderPolicy` | Swap in your own header layout via `BasicPacketFrameParser<Policy>` |

---

## .NET types (`memorypack/dotnet.hpp`)

| Type | Helpers |
|---|---|
| `Guid` | `Parse`, `ToString`, `FromBytes` |
| `DateTime` | `FromTicks`, `GetTicks`, `GetKind`, `FromTimePoint`, `ToTimePoint` |
| `TimeSpan` | `FromDuration`, `ToDuration` |
| `DateTimeOffset` | `offsetMinutes`, `dateTime` |
| `Decimal` | Opaque 16 bytes; round-trips exactly |
| `Half` | `FromFloat`, `ToFloat` |
| `Int128` / `UInt128` | `low`, `high` |
| `Vector2` / `Vector3` / `Vector4` / `Quaternion` | Unmanaged structs |
| `DateTimeKind` | `Unspecified`, `Utc`, `Local` |
| `TICKS_AT_UNIX_EPOCH`, `TICKS_PER_SECOND` | Tick conversion constants |

---

## Appendix: why buffers are `uint8_t`, not `char`

The C++ standard leaves the signedness of `char` implementation-defined:

| Platform | `char` signedness |
|---|---|
| MSVC (Windows) | signed by default |
| GCC/Clang on x86 | signed by default |
| Some ARM compilers | unsigned |

That matters as soon as you treat the buffer as data rather than text:

```cpp
char buf[4];
buf[0] = 0xFF;      // on a signed-char platform this is -1
int v = buf[0];     // v == -1, not 255

uint8_t ubuf[4];
ubuf[0] = 0xFF;     // always 255
int u = ubuf[0];    // u == 255
```

| | `char` | `uint8_t` |
|---|---|---|
| Signedness | platform-dependent | always unsigned |
| Intent | text | raw bytes |
| Bit operations | sign extension surprises | always well-defined |
| Cross-platform | subtle bugs | consistent |

`uint8_t` states that the buffer is binary data, and removes a whole class of
sign-extension bugs when the same code is built for Windows, Linux and ARM. The
API still accepts `std::span<std::byte>` where that suits your codebase better.
