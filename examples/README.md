# MemoryPackCpp Examples

Ten small, self-contained programs that double as documentation for the
header-only [MemoryPackCpp](../README.md) library.

Each file is a single `.cpp` with its own `main()`, depends on nothing but
`include/`, prints what it is doing (with hex dumps wherever the bytes matter),
and **exits `0` only if every claim it makes actually held**. That last part is
deliberate: the examples are executable documentation, so they cannot quietly
rot when the library changes.

Read them in order. Each one assumes the previous ones.

| # | File | What it covers |
|---|------|----------------|
| 01 | [`01_quick_start.cpp`](01_quick_start.cpp) | Define a struct, `MEMORYPACK_DEFINE` it, `Serialize`/`Deserialize`, and read the resulting bytes field by field. |
| 02 | [`02_nested_and_collections.cpp`](02_nested_and_collections.cpp) | Nested objects, `std::vector<UserType>`, nested vectors, `std::map`, `std::set`, `std::optional` members - and what each one encodes to. |
| 03 | [`03_nullable.cpp`](03_nullable.cpp) | The four different C# null encodings (`int?`, `string?`, `List<int>?`, `MyClass?`), which C++ type maps to each, and why null is not the same as empty. |
| 04 | [`04_union_variant.cpp`](04_union_variant.cpp) | `std::variant` as a C# `[MemoryPackUnion]` via `MEMORYPACK_UNION_TAG`, including the wide-tag encoding for tags >= 250 and nullable unions. |
| 05 | [`05_unmanaged_struct.cpp`](05_unmanaged_struct.cpp) | `MEMORYPACK_UNMANAGED` for C# unmanaged structs: no object header, matching .NET's natural alignment, bulk arrays, and why struct padding reaches the wire. |
| 06 | [`06_fixed_buffer.cpp`](06_fixed_buffer.cpp) | Zero-allocation serialization: `SerializeTo` into a `std::array`, a reused writer with `Clear()`, the "reserve header, serialize, patch the length" pattern, `RemainingCapacity()`, and overflow behaviour. |
| 07 | [`07_packet_framing.cpp`](07_packet_framing.cpp) | `memorypack/packet.hpp`: `MakePacket`, `WritePacket`, `PeekPacketHeader`, and `PacketFrameParser` reassembling a TCP-like stream delivered in small chunks. |
| 08 | [`08_error_handling.cpp`](08_error_handling.cpp) | Bounds-checked reads, `ReaderOptions` limits rejecting hostile payloads, `DeserializeExact` catching trailing bytes, and the `std::expected` API. |
| 09 | [`09_version_tolerance.cpp`](09_version_tolerance.cpp) | Two schema versions reading each other's bytes, where the default layout falls short, and the explicit `VersionTolerantWriter`/`VersionTolerantReader`. |
| 10 | [`10_dotnet_types.cpp`](10_dotnet_types.cpp) | `Guid`, `DateTime` (ticks and `std::chrono`), `TimeSpan`, `Half`, and how each appears on the wire. |

## Building

### With CMake (all platforms)

The examples are off by default. Turn them on with `MEMORYPACK_BUILD_EXAMPLES`:

```bash
cmake -B build -DMEMORYPACK_BUILD_EXAMPLES=ON
cmake --build build
```

The binaries land in `build/examples/` (single-config generators such as Ninja
and Makefiles) or `build/examples/<Config>/` (multi-config generators such as
Visual Studio), named after the source file:

```bash
./build/examples/01_quick_start
./build/examples/07_packet_framing
```

If the unit tests are enabled as well, every example is also registered as a
CTest case, so `ctest` runs all ten and fails the build if any of them reports a
mismatch:

```bash
cmake -B build -DMEMORYPACK_BUILD_EXAMPLES=ON -DMEMORYPACK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build -R example --output-on-failure
```

### Without CMake

Every example compiles standalone against `include/` alone - no build system,
no library to link, no dependencies.

GCC or Clang:

```bash
g++ -std=c++23 -Iinclude -Wall -Wextra -Wpedantic -Werror \
    examples/01_quick_start.cpp -o 01_quick_start
./01_quick_start
```

MSVC (from a Developer Command Prompt, or after running `vcvars64.bat`):

```bat
cl /nologo /std:c++latest /EHsc /permissive- /utf-8 /W4 /WX /I include ^
   examples\01_quick_start.cpp /Fe:01_quick_start.exe
01_quick_start.exe
```

Build them all at once:

```bash
# bash
for f in examples/[0-9][0-9]_*.cpp; do
    g++ -std=c++23 -Iinclude -Wall -Wextra -Wpedantic -Werror "$f" -o "$(basename "${f%.cpp}")" || exit 1
done
```

```bat
:: cmd, after vcvars64.bat
for %f in (examples\*.cpp) do cl /nologo /std:c++latest /EHsc /permissive- /utf-8 /W4 /WX /I include %f /Fe:%~nf.exe
```

## Requirements

* A C++23 compiler: MSVC v143 (Visual Studio 2022) or newer, GCC 13+, Clang 16+.
* Nothing else. The examples use only the standard library and the headers in
  `include/memorypack/`.

Example 08 additionally exercises the `std::expected` API, which is compiled in
only when `<expected>` is available; the file guards that section with
`#if MEMORYPACK_HAS_EXPECTED` and prints a note when it is not.

## Conventions used in these files

* **The hex dump helper is duplicated in every file** rather than shared through
  a common header. That is intentional: each example has to stand on its own so
  you can copy a single file out of the repository and compile it.
* **Comments explain the *why* and the C# equivalent**, not the mechanics of the
  C++ syntax. The wire format is the interesting part.
* `MEMORYPACK_DEFINE`, `MEMORYPACK_UNMANAGED` and `MEMORYPACK_UNION_TAG` always
  appear at **global scope** - they open `namespace memorypack` internally, so
  they cannot be used inside a function, class or namespace.
* Sources are ASCII-only, matching the rest of the project.
