# Performance

MemoryPack is a zero-encoding format: no varints, no field tags, no names. On a
little-endian machine, serializing is close to `memcpy` plus a length prefix. Most
of the remaining cost in a real program is **allocation**, not encoding — so this
page is mostly about avoiding allocations.

To measure any of this on your own hardware, see [benchmarks.md](benchmarks.md).

---

## Buffer modes

`MemoryPackWriter` has three, and the choice matters more than anything else in a
hot loop.

```cpp
memorypack::MemoryPackWriter w;                      // 1. internal growable buffer
memorypack::MemoryPackWriter w(myVector);            // 2. caller-owned vector, appended to
memorypack::MemoryPackWriter w(std::span<uint8_t>(myArray));   // 3. fixed buffer, no heap
```

| Mode | Allocates | Use when |
|---|---|---|
| internal | on growth; `TakeBuffer()` hands you the vector | one-shot serialization |
| external vector | on growth of your vector | you own a send buffer and reuse it |
| fixed buffer | never | hot paths, stack buffers, bounded packets |

### Reuse a writer instead of creating one per packet

```cpp
// Once, per connection or per thread.
memorypack::MemoryPackWriter writer;
writer.Reserve(4096);

// Per packet: no allocation after the buffer has grown once.
for (const auto& update : updates) {
    writer.Clear();
    writer.Write(update);
    send(sock, writer.Data(), writer.Size(), 0);
}
```

`Clear()` resets the write position and keeps the capacity.

### Stack buffers for bounded packets

```cpp
std::array<uint8_t, 256> buffer;
size_t n = memorypack::SerializeTo(std::span<uint8_t>(buffer), state);
if (n == 0) { /* did not fit */ }
```

No heap traffic at all. Use `TrySerializeTo` if you want the failure as a
`std::expected` rather than an exception.

---

## Zero-copy and in-place reads

### Borrow strings instead of copying them

`ReadStringView()` returns a `std::string_view` into the input buffer:

```cpp
memorypack::MemoryPackReader reader(body);
if (auto name = reader.ReadStringView()) {
    // No allocation, no copy. Valid only while `body` is alive.
    lookupPlayer(*name);
}
```

It returns `nullopt` for a null string **and** for a UTF-16 payload, which cannot
be viewed as UTF-8 without transcoding. Fall back to `ReadString()` there.

Use this when you only need to inspect or hash the text. If you are going to store
it anyway, the copy is unavoidable.

### Fill existing containers

The in-place overloads reuse whatever capacity the target already has:

```cpp
std::vector<int32_t> scores;   // reused across packets
std::string          name;

reader.ReadVector(scores);     // instead of scores = reader.ReadVector<int32_t>()
reader.ReadString(name);
reader.ReadCollection(items);
memorypack::Deserialize(ptr, size, existingObject);
```

Reading an arithmetic vector is a single bounds-checked `memcpy` into an
`assign()` — no zero-fill pass, no per-element loop.

---

## The unmanaged fast path

For a C# `struct` with no reference fields, MemoryPack copies raw bytes. Declare
the mapping and arrays of it become one `memcpy`:

```cpp
struct Vec3 { float x = 0, y = 0, z = 0; };
MEMORYPACK_UNMANAGED(Vec3, 12)

w.WriteUnmanagedCollection(std::span<const Vec3>(points));   // [4B count][N*12 bytes]
r.ReadUnmanagedCollection(points);
```

This is the single biggest win available for position updates, tile maps, vertex
data and similar payloads: it skips the per-element dispatch entirely.

Two requirements: the C++ layout must match .NET's (natural alignment, padding
included — the macro's size assertion enforces it), and the target must be
little-endian (enforced at compile time).

---

## Packet framing without a second pass

`WritePacket` reserves the header, serializes the body directly behind it, then
patches the length in place — the body is never serialized twice or copied:

```cpp
std::vector<uint8_t> sendBuffer;   // reused
sendBuffer.clear();
memorypack::WritePacket(sendBuffer, packetId, body);
send(sock, sendBuffer.data(), sendBuffer.size(), 0);
```

If you frame by hand, the same pattern applies: reserve the header bytes, build a
writer over the vector, then `memcpy` the length into the reserved space. The
caller-owned-vector mode keeps `size()` exact throughout, so
`sendBuffer.size() - headerSize` is the body length.

---

## Sizing hints

- `Reserve(n)` on the writer before a large payload avoids repeated growth.
- Collections write their element count first, so the reader can size the
  destination in one step — nothing to tune there.
- Strings cost two `int32` headers plus the UTF-8 bytes. Many tiny strings are
  more expensive than one larger one; consider a single joined string or an id.

---

## What costs what

| Operation | Cost |
|---|---|
| primitive write/read | a bounds check plus a fixed-size copy |
| `std::vector<arithmetic>` | one `memcpy` in each direction |
| `std::vector<bool>` | one byte per element, element-by-element (no `data()` to copy from) |
| unmanaged struct array | one `memcpy` |
| object | one header byte plus its members |
| string write | a UTF-16 length scan (ASCII fast path, 8 bytes at a time) plus a `memcpy` |
| string read | a `memcpy`, or free with `ReadStringView()` |
| UTF-16 string read | a transcode pass — unavoidable |
| `std::map` / `std::set` | element-by-element, plus the container's own insertion cost |
| VersionTolerant object | one to five extra bytes per member, plus a buffering pass on write |

---

## Compile time

The umbrella header pulls in `<map>`, `<set>`, `<variant>`, `<chrono>` and
friends. If that matters, include only what you use:

```cpp
#include "memorypack/core.hpp"        // writer, reader, strings, objects,
                                      // collections, unions, unmanaged structs
#include "memorypack/containers.hpp"  // std:: containers, optional, variant
#include "memorypack/dotnet.hpp"      // Guid, DateTime, decimal, Half, Int128
#include "memorypack/packet.hpp"      // TCP framing (never included by default)
```

`core.hpp` alone covers most packet code.

---

## Rules of thumb

1. Reuse one writer per connection or thread; call `Clear()`, not the constructor.
2. Use a fixed buffer when the packet has a known upper bound.
3. Use `ReadStringView()` when you only inspect the text.
4. Use the in-place `Read*` overloads for containers you already own.
5. Mark layout-stable structs with `MEMORYPACK_UNMANAGED` and use the bulk
   collection calls.
6. Measure before optimizing further — [benchmarks.md](benchmarks.md) has the
   harness, including a raw `memcpy` baseline so you can see how much room is
   actually left.
