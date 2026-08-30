# Samples

Four pairs of programs, each with a C# side and a C++ side talking over TCP with
real MemoryPack payloads - in both directions.

| Pair | Port | What it demonstrates |
|---|---|---|
| `CSharpServer` + `CppClient` | 25001 | every supported data type, round-tripped through a real C# server |
| `ChatServer` + `ChatClient` | 25002 | a multi-user application: rooms, broadcast, whispers (Win32 GUI) |
| `ChatServer` + `ChatClientConsole` | 25002 | the same chat protocol from a cross-platform console client |
| `CppServer` + `CsClient` | 25003 | the reverse direction: a **C++ server** serving a **C# client** |

They all use the same framing: `[2B packetId][4B bodyLength][body...]`, all
little-endian. That framing is not part of MemoryPack — it is the samples'
choice, and [`memorypack/packet.hpp`](../include/memorypack/packet.hpp) provides
helpers for it.

> Ports follow this project's development convention: TCP 25001-25199.

---

## Prerequisites

- .NET 10 SDK (for the C# projects)
- A C++23 compiler and CMake 3.21+ (for the C++ projects)
- `ChatClient` is a Win32 GUI application and builds on Windows only. Everything
  else is cross-platform.

Build the C++ programs:

```bash
cmake -B build -DMEMORYPACK_BUILD_SAMPLES=ON
cmake --build build
```

Or open the `.sln` in each sample directory with Visual Studio (x64, C++23).

---

## Pair 1: CSharpServer + CppClient

A type-coverage test. The C++ client sends one packet of each supported shape,
the C# server deserializes it with the real MemoryPack library, transforms the
values in a recognisable way, and sends it back. The client prints both sides so
you can see the round trip.

### Run

```bash
# Terminal 1
cd samples/CSharpServer
dotnet run

# Terminal 2
./build/samples/CppClient          # or run the Visual Studio output
```

### What it covers

| Packet | Types exercised |
|---|---|
| `LoginRequest` / `LoginResponse` | `string`, `int32`, `bool` |
| `PlayerState` | `float` coordinates |
| `ChatMessage` | `int64` timestamp, `string` |
| `ScoreUpdate` | `vector<int32>`, `double` |
| `InventoryData` | `vector<string>`, `vector<int32>` |
| `BufferData` | `vector<uint8>`, `vector<int8>` |
| `IntArrayPacket` | `vector<int16>`, `vector<int32>`, `vector<int64>` |
| `SkillSlotData` | C fixed arrays `int32[8]`, `float[8]` |
| `MapTileRow` | C fixed arrays `uint8[64]`, `int16[64]` |
| `MixedFormatPacket` | vector + fixed array + char array in one packet |

The server transforms each payload (doubles the scores, sorts the arrays, XORs
the tiles, uppercases the tag) so a silently-wrong decode is visible rather than
looking like a successful echo.

### Fixed arrays

`SkillSlotData`, `MapTileRow` and `MixedFormatPacket` show the pattern game code
usually wants: a C array with a separate count, rather than a heap-allocated
vector.

```cpp
struct SkillSlotData {
    static constexpr int32_t MAX_SKILLS = 8;
    int32_t playerId = 0;
    int32_t skillIds[MAX_SKILLS] = {};
    int32_t skillCount = 0;          // tracked locally, NOT serialized
};
```

The count is not a wire member — the collection header already carries it. On the
C# side these are plain `List<int>`, because the encoding is identical.

---

## Pair 2: ChatServer + ChatClient

A small but complete application: log in, join a room, chat, whisper.

### Run

```bash
# Terminal 1
cd samples/ChatServer
dotnet run

# Terminal 2..N (Windows)
./build/samples/ChatClient         # start several to see the broadcast
```

### Protocol

| ID | Packet | Direction | Purpose |
|---|---|---|---|
| 101 | `LoginRequest` | C -> S | claim a username |
| 102 | `LoginResponse` | S -> C | accepted, or rejected as a duplicate |
| 103 | `RoomJoinRequest` | C -> S | join or switch room |
| 104 | `RoomJoinResponse` | S -> C | success plus the current member list |
| 105 | `RoomChat` | both | a message to everyone in the room |
| 106 | `PrivateChat` | both | a whisper to one user |
| 107 | `UserEntered` | S -> C | someone joined |
| 108 | `UserLeft` | S -> C | someone left |

The server is thread-per-client with a lock around the shared room and user
tables. The client runs a background receive thread and hands each packet to the
UI thread through a custom `WM_NET_PACKET` message — the usual shape for a Win32
networked client.

---

## Pair 3: CppServer + CsClient (the reverse direction)

The other deployment shape: the game server is C++ and the client is C# - a Unity
client talking to a dedicated backend. Port 25003.

### Run

```bash
# Terminal 1
./build/samples/CppServer

# Terminal 2
dotnet run --project samples/CsClient
```

The client asserts every response and exits non-zero on a mismatch, so it doubles
as an end-to-end interop test.

### What it demonstrates

| Packet | Types exercised |
|---|---|
| `EchoRequest` / `EchoResponse` | `string`, `int32`, `int64` (server ticks) |
| `SumRequest` / `SumResponse` | `List<int>` -> `std::vector<int32_t>`, `int64` |
| `SpawnRequest` / `SpawnResponse` | `List<Entity>` - a collection of objects, each holding a `Vector3` unmanaged struct |

The server side is worth reading as a template for real code:

- `memorypack::PacketFrameParser` with an explicit maximum body length handles
  reassembly; the connection is dropped when `Feed()` returns false.
- A tightened `memorypack::ReaderOptions` is passed to every read of client data.
- One `MemoryPackWriter` per connection, reused with `Clear()`.
- `SpawnRequest.Count` is clamped server-side, because a well-formed packet can
  still ask for a million entities.

`memorypack::Vector3` maps to `System.Numerics.Vector3` on the C# side: an
unmanaged struct copied verbatim, 12 bytes, no object header.

---

## Pair 4: ChatServer + ChatClientConsole

`ChatClient` is a Win32 GUI application, so Linux and macOS could not run the chat
sample at all. `ChatClientConsole` speaks the same protocol from a terminal and
builds everywhere.

```bash
./build/samples/ChatClientConsole [username] [room] [host] [port]
```

Commands: plain text sends a room message, `/w <user> <message>` whispers,
`/join <room>` switches room, `/quit` exits.

Its `packets.hpp` is written with `MEMORYPACK_DEFINE` rather than hand-written
`IMemoryPackable` specializations - the same packets in a fraction of the code,
which is worth comparing against `samples/ChatClient/packets.hpp`.

---

## Packet definitions

Each pair has a C# `Packets.cs` and a C++ `packets.hpp` whose members must line
up exactly, because MemoryPack serializes by position and never writes names.

`samples/CppClient/packets.hpp` and `samples/ChatClient/packets.hpp` are
**generated** from the C# files by [`tools/cs2cpp`](../tools/cs2cpp) - do not edit
them by hand. Regenerate with:

```bash
dotnet run --project tools/cs2cpp -- samples/ChatServer/Packets.cs \
    -o samples/ChatClient/packets.hpp
```

`--check` regenerates in memory and compares instead of writing, so it fails when
the header has drifted from the C# definition. Run it after editing either side:

```bash
dotnet run --project tools/cs2cpp -- samples/ChatServer/Packets.cs \
    -o samples/ChatClient/packets.hpp --check
```

See [tools/cs2cpp/README.md](../tools/cs2cpp/README.md).

---

## Using these as a starting point

If you are wiring up your own server, the pieces worth copying are:

- **Framing.** Use [`memorypack/packet.hpp`](../include/memorypack/packet.hpp)
  rather than hand-rolling it — `PacketFrameParser` handles partial reads,
  multiple packets per chunk, and a maximum body length.
- **Limits.** Pass a `ReaderOptions` with your protocol's real bounds to every
  reader that touches client data. See [docs/security.md](../docs/security.md).
- **Buffer reuse.** One `MemoryPackWriter` per connection with `Clear()` between
  packets, instead of allocating per send. See
  [docs/performance.md](../docs/performance.md).
- **Generated definitions.** Keep `Packets.cs` as the source of truth and
  generate the header.
