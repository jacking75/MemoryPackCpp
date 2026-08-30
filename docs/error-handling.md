# Error Handling

MemoryPackCpp reports every failure through one error enum, and lets you choose
how that reaches your code: as an exception, as an error flag, or as a
`std::expected`. All three paths share the same checks — nothing is skipped in a
faster mode.

---

## Error codes

```cpp
enum class memorypack::MemoryPackError : uint8_t {
    None = 0,
    BufferUnderflow,   // the reader ran past the end of the input
    BufferOverflow,    // the writer ran past the end of a fixed-size buffer
    InvalidHeader,     // reserved/illegal object, union or length-marker value
    LengthLimit,       // a declared length is impossible or exceeds a configured cap
    InvalidString,     // malformed UTF-8/UTF-16 payload
    TrailingBytes,     // input had bytes left over when full consumption was required
    NotSupported,      // operation unavailable in this configuration
};

const char* memorypack::ToString(MemoryPackError);
```

---

## Mode 1: exceptions (default)

With exceptions enabled, a failure throws `memorypack::MemoryPackException`,
which derives from `std::runtime_error` and carries the code and the byte offset:

```cpp
try {
    auto packet = memorypack::Deserialize<LoginRequest>(bytes);
} catch (const memorypack::MemoryPackException& e) {
    log("bad packet: %s (code=%d, offset=%zu)",
        e.what(), static_cast<int>(e.code()), e.offset());
}
```

`what()` reads like:

```
memorypack: buffer underflow (not enough bytes remaining) at offset 17
```

The offset is the position in the input where the read failed, which is usually
enough to identify which member went wrong.

---

## Mode 2: no exceptions

Unreal Engine and most console toolchains build with exceptions off. The library
detects that automatically (`__cpp_exceptions` / `_CPPUNWIND`), and you can also
force it:

```cpp
#define MEMORYPACK_NO_EXCEPTIONS
#include "memorypack/memorypack.hpp"
```

In this mode nothing throws. Instead:

- the reader or writer enters a **failed state**,
- every later operation becomes a no-op returning a default value, and
- you check the state when you are done.

```cpp
memorypack::MemoryPackReader reader(bytes);
LoginRequest request;
reader.Read(request);

if (reader.Failed()) {
    log("bad packet: %s at offset %zu",
        memorypack::ToString(reader.Error()), reader.Position());
    return;
}
```

The same applies to the writer:

```cpp
std::array<uint8_t, 128> buffer;
memorypack::MemoryPackWriter writer(buffer);
writer.Write(response);
if (writer.Failed()) { /* did not fit */ }
```

Because a failed reader stops doing work, you can write a whole sequence of reads
and check once at the end rather than after every call.

---

## Mode 3: `std::expected`

When the standard library provides `<expected>` (`MEMORYPACK_HAS_EXPECTED` is 1),
you get an explicit result type that works in both of the above modes:

```cpp
auto result = memorypack::TryDeserialize<LoginRequest>(bytes);
if (!result) {
    log("bad packet: %s", memorypack::ToString(result.error()));
    return;
}
handle(*result);
```

```cpp
std::array<uint8_t, 256> buffer;
auto written = memorypack::TrySerializeTo(std::span<uint8_t>(buffer), response);
if (!written) { /* MemoryPackError::BufferOverflow */ }
else          { send(sock, buffer.data(), *written, 0); }
```

This is the recommended API for network-facing code: a malformed packet from a
client is an ordinary, expected outcome, not an exceptional one.

---

## What is checked

Reads are bounds-checked without exception:

| Check | Failure |
|---|---|
| every primitive, header and payload read stays inside the buffer | `BufferUnderflow` |
| the bounds test itself cannot overflow (`n > size - pos`, never `pos + n > size`) | — |
| object header in the reserved range 250–254 | `InvalidHeader` |
| union header in the reserved range 251–254 | `InvalidHeader` |
| unknown VersionTolerant length marker | `InvalidHeader` |
| a negative collection length other than -1 | `LengthLimit` |
| a collection length larger than the remaining bytes can hold | `LengthLimit` |
| a string length larger than the remaining bytes can hold | `LengthLimit` |
| a length above a configured `ReaderOptions` cap | `LengthLimit` |
| nesting deeper than `maxDepth` | `LengthLimit` |
| leftover input when using `DeserializeExact` | `TrailingBytes` |

Writes are checked too:

| Check | Failure |
|---|---|
| a fixed-size buffer running out of room | `BufferOverflow` |
| an object header above 249 members (collides with reserved codes) | `InvalidHeader` |
| a collection or string longer than `INT32_MAX` | `LengthLimit` |
| `TakeBuffer()`/`GetBuffer()` on the wrong buffer mode | `NotSupported` |

Length validation happens **before** any allocation, so a four-byte packet
claiming two billion elements is rejected rather than turned into a
multi-gigabyte `reserve`. See [security.md](security.md).

---

## Choosing limits

The defaults bound every length by the bytes actually present, which is enough to
stop the allocation attack. A server should tighten them to what its protocol
actually needs:

```cpp
memorypack::ReaderOptions limits;
limits.maxCollectionLength = 10'000;      // no packet has more items than this
limits.maxStringLength     = 64 * 1024;   // no chat message is longer
limits.maxDepth            = 16;          // no nesting deeper than this

memorypack::MemoryPackReader reader(untrustedBytes, limits);
```

`maxDepth` is enforced by the object readers that `MEMORYPACK_DEFINE` generates,
so it protects against a deliberately deep nesting chain.

---

## Diagnosing a mismatch

If C# and C++ disagree about a type, the symptom is usually one of:

| Symptom | Likely cause |
|---|---|
| `TrailingBytes` from `DeserializeExact` | C++ reads fewer members than C# wrote — member missing or in the wrong order |
| `BufferUnderflow` partway through | a member's type differs between the two sides (e.g. `int` vs `long`) |
| values shifted by one member | a member was inserted in the middle instead of appended |
| a string comes back as garbage | the member before it has the wrong width |
| `InvalidHeader` at offset 0 | you are deserializing a packet body that still has its framing header attached |

The fastest way to settle it: add the C# type to `tools/FormatProbe/Types.cs`,
regenerate the fixtures, and compare the bytes in `tests/fixtures/report.txt`
against what your C++ produces. That turns an argument into a diff.
