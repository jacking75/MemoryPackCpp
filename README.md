# MemoryPackCpp

[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue)](#requirements)
[![Header-only](https://img.shields.io/badge/header--only-yes-brightgreen)](#installation)

**A header-only C++23 library that speaks Cysharp
[MemoryPack](https://github.com/Cysharp/MemoryPack)'s binary wire format,
byte for byte — so a C++ game server or client can talk to C# and Unity.**

Read [한국어 README](README.ko.md) · [Wire format spec](docs/wire-format.md) ·
[Type mapping](docs/type-mapping.md) · [Roadmap](ROADMAP.md)

> Not affiliated with Cysharp. This is an independent implementation of the
> MemoryPack wire format, verified against the real C# library.

---

## Why

MemoryPack is the fastest general-purpose serializer in the .NET ecosystem, and
Unity projects use it heavily. The problem starts when the other side of the
socket is C++: a dedicated game server, a native client, a tool. You end up
hand-rolling a second protocol, and every schema change becomes a two-language
chore.

MemoryPackCpp removes that by implementing the format itself. You define the same
members in the same order, and the bytes match.

**How we know they match.** [`tools/FormatProbe`](tools/FormatProbe) serializes
53 cases with the actual C# MemoryPack package and commits the bytes to
[`tests/fixtures/`](tests/fixtures). The test suite asserts both directions:

- the C++ reader decodes those C# bytes to the expected values, **and**
- the C++ writer re-emits byte-identical output, **and**
- C# reads back what C++ produced.

That covers unions, unmanaged structs with padding, `Nullable<T>`, surrogate
pairs, `Guid`/`DateTime`, the VersionTolerant layout, and every length-encoding
edge case. If a byte drifts, a test fails. See
[Building and testing](#building-and-testing) for the commands.

---

## Quick start

```cpp
#include "memorypack/memorypack.hpp"

struct LoginRequest {
    std::string userName;
    int32_t     level;
};
MEMORYPACK_DEFINE(LoginRequest, userName, level)   // one line, at global scope

int main() {
    auto bytes = memorypack::Serialize(LoginRequest{"Player1", 42});
    auto back  = memorypack::Deserialize<LoginRequest>(bytes);
}
```

The matching C#:

```csharp
[MemoryPackable]
public partial class LoginRequest
{
    public string? UserName { get; set; }
    public int Level { get; set; }
}
```

Member **order** is the contract — MemoryPack writes no names. Adding a member at
the end is safe in both directions; reordering or removing one is a breaking
change (unless you use the [VersionTolerant](docs/wire-format.md#versiontolerant-objects)
layout).

---

## Installation

Header-only. Add `include/` to your include path, or use CMake:

```cmake
# (A) FetchContent
include(FetchContent)
FetchContent_Declare(memorypack
    GIT_REPOSITORY https://github.com/jacking75/MemoryPackCpp.git
    GIT_TAG        v0.2.0)
FetchContent_MakeAvailable(memorypack)
target_link_libraries(my_app PRIVATE memorypack::memorypack)

# (B) Subdirectory
add_subdirectory(MemoryPackCpp)
target_link_libraries(my_app PRIVATE memorypack::memorypack)

# (C) Installed package
find_package(memorypack CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE memorypack::memorypack)
```

Or generate a **single-header build** and drop that one file into your tree:

```bash
python tools/amalgamate.py --include-packet -o dist/memorypack.hpp
```

### Headers

| Header | Contents |
|---|---|
| `memorypack/memorypack.hpp` | umbrella — includes the three below |
| `memorypack/core.hpp` | writer, reader, primitives, strings, objects, collections, unions, unmanaged structs |
| `memorypack/containers.hpp` | `std::` containers, `optional`, smart pointers, `variant` as a union |
| `memorypack/dotnet.hpp` | `Guid`, `DateTime`, `TimeSpan`, `decimal`, `Half`, `Int128` |
| `memorypack/packet.hpp` | optional TCP framing helpers (not in the umbrella) |

### Requirements

- **C++23**: MSVC v143 (Visual Studio 2022) or newer, GCC 13+, Clang 16+
- CMake 3.21+ (only if you build with CMake)
- .NET 10 SDK (only for the C# tools and samples)

---

## Supported types

| C# | C++ | |
|---|---|---|
| `bool`, `byte`/`sbyte`, `short`/`ushort`, `int`/`uint`, `long`/`ulong`, `float`, `double` | matching fixed-width types | ✅ |
| `enum : T` | `enum class : T` | ✅ |
| `string` (UTF-8 and UTF-16 on read) | `std::string`, `std::u16string`, `std::string_view` | ✅ |
| `List<T>`, `T[]` | `std::vector`, `std::array`, C arrays, `std::deque`, `std::list` | ✅ |
| `List<bool>` | `std::vector<bool>` | ✅ |
| `List<UserType>`, nested collections | `std::vector<UserType>`, `std::vector<std::vector<T>>` | ✅ |
| `Dictionary<K,V>` | `std::map`, `std::unordered_map` | ✅ |
| `HashSet<T>` | `std::set`, `std::unordered_set` | ✅ |
| `KeyValuePair<K,V>` | `std::pair` | ✅ |
| `Tuple<...>`, `ValueTuple` | `std::tuple`, `MEMORYPACK_UNMANAGED` struct | ✅ |
| `[MemoryPackable] class` | `MEMORYPACK_DEFINE` or a hand-written `IMemoryPackable<T>` | ✅ |
| `[MemoryPackable] struct` (unmanaged) | `MEMORYPACK_UNMANAGED(T, size)` | ✅ |
| `[MemoryPackUnion]` | `std::variant` + `MEMORYPACK_UNION_TAG` | ✅ |
| `Nullable<T>`, nullable references | `std::optional`, `std::unique_ptr`, `std::shared_ptr` | ✅ |
| `Guid`, `DateTime`, `TimeSpan`, `DateTimeOffset`, `decimal`, `Half`, `Int128`, `char` | `memorypack::` equivalents, `char16_t` | ✅ |
| `Vector2/3/4`, `Quaternion` | `memorypack::Vector2/3/4`, `Quaternion` | ✅ |
| `GenerateType.VersionTolerant` | `VersionTolerantWriter` / `VersionTolerantReader` | ✅ |
| `GenerateType.CircularReference` | — | ❌ |

Full details and the exact bytes for each: [docs/type-mapping.md](docs/type-mapping.md).

---

## Features

**One-line type definitions.** `MEMORYPACK_DEFINE(T, a, b, c)` generates the
serializer, keeps member count and order in one place, and builds in version
tolerance. Hand-written `IMemoryPackable<T>` specializations still work for
anything unusual.

**Buffer control.** Serialize into a fresh buffer, into a caller-owned
`std::vector` (appending behind a reserved packet header), or into a fixed
`std::array` with no heap allocation at all. Readers can borrow strings with
`ReadStringView()` and fill existing containers in place.

**Safe on untrusted input.** Every read is bounds-checked; declared collection
and string lengths are validated against the bytes actually remaining *before*
anything is allocated, so a 4-byte packet can't ask for gigabytes. `ReaderOptions`
adds explicit caps on collection length, string length and nesting depth. A
libFuzzer harness runs the whole decoder against random bytes under ASan/UBSan.

**Works without exceptions.** Unreal Engine and most console toolchains disable
them. Build with `-fno-exceptions` and errors surface through the reader's error
state and the `std::expected`-based `TryDeserialize` / `TrySerializeTo` API
instead.

**Cross-platform.** Windows, Linux and macOS; little- and big-endian (byte
swapping is automatic, apart from the unmanaged-struct fast path).

**Code generation.** [`tools/cs2cpp`](tools/cs2cpp) reads your C#
`[MemoryPackable]` definitions and emits the matching C++ header, so the two
sides cannot drift.

---

## Examples

```cpp
// Nested objects and collections work through one generic entry point.
struct Item      { int32_t id; std::string name; int32_t count; };
struct Inventory { int32_t ownerId; std::vector<Item> items; };
MEMORYPACK_DEFINE(Item, id, name, count)
MEMORYPACK_DEFINE(Inventory, ownerId, items)
```

```cpp
// Zero-allocation hot path: reuse one writer.
memorypack::MemoryPackWriter writer;
writer.Reserve(4096);
for (const auto& update : updates) {
    writer.Clear();
    writer.Write(update);
    send(sock, writer.Data(), writer.Size(), 0);
}
```

```cpp
// TCP framing: [2B packetId][4B bodyLength][body]
memorypack::PacketFrameParser parser;
parser.Feed(receivedBytes, [](uint16_t id, std::span<const uint8_t> body) {
    if (id == 101) handle(memorypack::Deserialize<LoginRequest>(body));
});
```

```cpp
// Hostile input is rejected before it allocates.
memorypack::ReaderOptions limits;
limits.maxCollectionLength = 10'000;
limits.maxStringLength     = 64 * 1024;
memorypack::MemoryPackReader reader(untrustedBytes, limits);
```

Ten runnable, commented programs live in [`examples/`](examples).

---

## Building and testing

```bash
cmake -B build -DMEMORYPACK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| Target | What it proves |
|---|---|
| `memorypack_tests` | wire format, buffer modes, limits, error handling, framing |
| `memorypack_interop_tests` | byte equality with fixtures captured from real C# MemoryPack |

Regenerate the fixtures (needs the .NET 10 SDK):

```bash
dotnet run --project tools/FormatProbe -- generate tests/fixtures   # capture C# bytes
dotnet run --project tools/FormatProbe -- verify   tests/fixtures   # detect upstream drift
```

Other options: `-DMEMORYPACK_BUILD_SAMPLES=ON`, `-DMEMORYPACK_BUILD_BENCHMARKS=ON`,
`-DMEMORYPACK_BUILD_EXAMPLES=ON`.

### Full verification

There is no hosted CI in this repository, so this is the checklist to run before
trusting a change. Everything here is reproducible locally.

```bash
# 1. Library, tests, samples and examples, with warnings as errors
cmake -B build -DCMAKE_BUILD_TYPE=Release       -DMEMORYPACK_BUILD_TESTS=ON -DMEMORYPACK_BUILD_SAMPLES=ON       -DMEMORYPACK_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure

# 2. The committed fixtures still match the installed C# MemoryPack
dotnet run --project tools/FormatProbe -c Release -- verify tests/fixtures

# 3. C# can read what C++ produced (the reverse direction)
./build/tests/memorypack_interop_tests "" build/cpp-fixtures
dotnet run --project tools/FormatProbe -c Release -- check-cpp build/cpp-fixtures

# 4. The generated sample headers have not drifted from the C# definitions
dotnet test tools/cs2cpp.Tests -c Release
dotnet run --project tools/cs2cpp -c Release --     samples/CSharpServer/Packets.cs -o samples/CppClient/packets.hpp --check
dotnet run --project tools/cs2cpp -c Release --     samples/ChatServer/Packets.cs -o samples/ChatClient/packets.hpp --check
```

Optional, and worth running when touching the reader:

```bash
# Fuzz the deserializer under ASan/UBSan
clang++ -std=c++23 -g -O1 -Iinclude     -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all     tests/fuzz/fuzz_deserialize.cpp -o fuzz_deserialize
./fuzz_deserialize -max_total_time=600
```

The samples are also end-to-end tests: start `CSharpServer` and run `CppClient`,
or start `CppServer` and run `CsClient` - both assert their results and exit
non-zero on a mismatch.

---

## Samples

| Sample | What it shows |
|---|---|
| `CSharpServer` + `CppClient` | every supported data type, exercised over TCP against a real C# server |
| `CppServer` + `CsClient` | the reverse direction — a **C++ server** serving a **C# client** |
| `ChatServer` + `ChatClient` | a multi-user chat app — rooms, broadcast, whispers (Win32 GUI) |
| `ChatServer` + `ChatClientConsole` | the same chat protocol from a cross-platform console client |

See [samples/README.md](samples/README.md) for how to run them.

---

## Documentation

| Document | Contents |
|---|---|
| [api-reference.md](docs/api-reference.md) | every public function, type and macro |
| [wire-format.md](docs/wire-format.md) | the complete binary format, with real captured bytes |
| [type-mapping.md](docs/type-mapping.md) | every C# type and its C++ counterpart |
| [serialization.md](docs/serialization.md) | defining your own types, version tolerance, null handling |
| [performance.md](docs/performance.md) | buffer reuse, zero-copy reads, the unmanaged fast path |
| [security.md](docs/security.md) | untrusted input, limits, fuzzing, threat model |
| [error-handling.md](docs/error-handling.md) | exceptions, `std::expected`, no-exception builds |
| [compatibility.md](docs/compatibility.md) | verified MemoryPack versions, compilers, platforms |
| [benchmarks.md](docs/benchmarks.md) | how to run the benchmark suite |
| [faq.md](docs/faq.md) | common questions and gotchas |
| [cs2cpp](tools/cs2cpp/README.md) | the C# → C++ code generator |

---

## Contributing

Bug reports, wire-format findings and PRs are welcome — see
[CONTRIBUTING.md](CONTRIBUTING.md). The one hard rule: **any change to the wire
format must come with a fixture captured from real C# MemoryPack**, not with an
argument about what the format should be.

Security issues: [SECURITY.md](SECURITY.md).

## License

MIT — see [LICENSE](LICENSE).

MemoryPack itself is © Cysharp, Inc. and MIT licensed. This project implements
its wire format independently and is not endorsed by or affiliated with Cysharp.
