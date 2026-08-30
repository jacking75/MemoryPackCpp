# Unity Integration

Unity is the easy side of this: it runs C#, so it uses the real
[MemoryPack](https://github.com/Cysharp/MemoryPack) package. MemoryPackCpp is for
whatever sits on the other end of the socket — a dedicated C++ game server, a
native tool, a bot.

This page is about keeping the two definitions in sync.

---

## The shape of a project

```
GameProtocol/            <- the one place packets are defined
  Packets.cs             <- [MemoryPackable] classes, shared by Unity and the server
UnityClient/
  Assets/Plugins/MemoryPack/     <- the C# MemoryPack package
  Assets/Scripts/Net/Packets.cs  <- a link or copy of GameProtocol/Packets.cs
CppServer/
  include/memorypack/    <- this library
  src/packets.hpp        <- GENERATED from GameProtocol/Packets.cs by cs2cpp
```

The rule that keeps this working: **`Packets.cs` is the source of truth, and the
C++ header is generated from it.** Never hand-edit both.

---

## 1. Unity side

Install MemoryPack as documented by Cysharp (UPM package or NuGetForUnity), then
define your packets normally:

```csharp
using MemoryPack;

public enum PacketId : ushort
{
    LoginRequest  = 1,
    LoginResponse = 2,
    PlayerState   = 3,
}

[MemoryPackable]
public partial class LoginRequest
{
    public string? UserName { get; set; }
    public int Level { get; set; }
}

[MemoryPackable]
public partial class PlayerState
{
    public int PlayerId { get; set; }
    public float PosX { get; set; }
    public float PosY { get; set; }
    public float PosZ { get; set; }
}
```

Sending, with the same `[2B packetId][4B bodyLength]` framing the C++ side uses:

```csharp
static byte[] MakePacket<T>(PacketId id, T body)
{
    var payload = MemoryPackSerializer.Serialize(body);
    var packet  = new byte[6 + payload.Length];
    BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(0, 2), (ushort)id);
    BinaryPrimitives.WriteInt32LittleEndian(packet.AsSpan(2, 4), payload.Length);
    payload.CopyTo(packet.AsSpan(6));
    return packet;
}
```

### IL2CPP and AOT

MemoryPack's source generator emits the formatters at compile time, so IL2CPP
works without reflection. Two things still bite people:

- **Managed code stripping.** Add a `link.xml` preserving your packet assembly, or
  mark it `[Preserve]`, if a packet type is only referenced through generics.
- **`partial`.** Every `[MemoryPackable]` type must be `partial`, or the generator
  has nowhere to put the formatter.

---

## 2. C++ server side

Generate the header from the same `Packets.cs`:

```bash
dotnet run --project tools/cs2cpp -- GameProtocol/Packets.cs -o CppServer/src/packets.hpp
```

Then use it:

```cpp
#include "packets.hpp"
#include "memorypack/packet.hpp"

memorypack::PacketFrameParser parser(64 * 1024);

void OnBytes(std::span<const uint8_t> bytes) {
    if (!parser.Feed(bytes, [](uint16_t id, std::span<const uint8_t> body) {
            switch (static_cast<PacketId>(id)) {
            case PacketId::LoginRequest: {
                auto req = memorypack::Deserialize<LoginRequest>(body);
                HandleLogin(req);
                break;
            }
            default: break;
            }
        })) {
        CloseConnection();     // malformed frame
    }
}
```

Add the generator to CI so the header cannot drift:

```bash
dotnet run --project tools/cs2cpp -- GameProtocol/Packets.cs \
    -o CppServer/src/packets.hpp --check
```

`--check` writes nothing and exits non-zero if the committed header differs from
what the current `Packets.cs` would produce.

---

## 3. Type mapping cheat sheet for Unity code

| Unity / C# | C++ |
|---|---|
| `int`, `float`, `bool`, `long` | `int32_t`, `float`, `bool`, `int64_t` |
| `string?` | `std::string` (or `std::optional<std::string>` if null is meaningful) |
| `List<T>`, `T[]` | `std::vector<T>` |
| `Vector3` (UnityEngine) | see below |
| `Quaternion` (UnityEngine) | `memorypack::Quaternion` |
| `[MemoryPackable] class` | `MEMORYPACK_DEFINE(T, ...)` |
| `[MemoryPackable] struct` (no references) | `MEMORYPACK_UNMANAGED(T, size)` |
| `[MemoryPackUnion]` | `std::variant` + `MEMORYPACK_UNION_TAG` |
| `int?` | `std::optional<int32_t>` |

Full table: [type-mapping.md](type-mapping.md).

### UnityEngine.Vector3

`UnityEngine.Vector3` is three `float`s laid out sequentially, so it is an
unmanaged struct and maps to `memorypack::Vector3`:

```cpp
struct PlayerState {
    int32_t             playerId = 0;
    memorypack::Vector3 position;
};
MEMORYPACK_DEFINE(PlayerState, playerId, position)
```

MemoryPack serializes it with no object header — 12 bytes. A `List<Vector3>` is
`[4B count]` followed by `count * 12` bytes, which the C++ side can read as one
`memcpy`:

```cpp
w.WriteUnmanagedCollection(std::span<const memorypack::Vector3>(path));
r.ReadUnmanagedCollection(path);
```

That is the fast path worth using for position streams and spline data.

---

## 4. Nullability

Unity code written with nullable reference types enabled will produce `string?`,
`List<T>?` and `MyClass?` members. These are three **different** null encodings on
the wire, and the C++ type has to match:

| C# | C++ |
|---|---|
| `string?` | `std::optional<std::string>` |
| `List<int>?` | `std::optional<std::vector<int32_t>>` |
| `MyClass?` | `std::optional<MyClass>` |
| `int?` | `std::optional<int32_t>` |

If a member is never actually null, using plain `std::string` on the C++ side is
fine and simpler — but then the C# side must never send null, or the C++ reader
will decode it as an empty string. `cs2cpp` follows the C# nullability annotations
when you pass `--nullable-strings`.

---

## 5. Testing the pair

The pattern this repository uses for its own samples works well for a game:

1. Write a tiny C# console app that serializes one instance of every packet and
   writes the bytes to files.
2. Have a C++ test read those files, decode them, assert the values, re-encode,
   and compare bytes.
3. Run both in CI.

That is exactly what [`tools/FormatProbe`](../tools/FormatProbe) and
`tests/interop_tests.cpp` do here, and it catches a member-order mistake the day
it is introduced rather than during a playtest.

---

## 6. Checklist

- [ ] `Packets.cs` is the single source of truth; the C++ header is generated.
- [ ] `cs2cpp --check` runs in CI.
- [ ] Every `[MemoryPackable]` type is `partial`.
- [ ] IL2CPP stripping configured (`link.xml`) if packets are only used generically.
- [ ] Framing agreed between both sides (`[2B id][4B length]` unless you changed it).
- [ ] `ReaderOptions` limits set on the server for client-supplied data.
- [ ] Nullability of each member decided deliberately, not by accident.
