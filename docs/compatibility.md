# Compatibility

## MemoryPack (C#) versions

The wire format is verified against a pinned MemoryPack package. The fixtures in
[`tests/fixtures/`](../tests/fixtures) were captured from that exact version.
Re-run `FormatProbe verify` after a package upgrade so an upstream format change
shows up as a failing check rather than as a production bug.

| MemoryPack | Status | Notes |
|---|---|---|
| **1.21.4** | ✅ verified | 53 fixtures, byte-identical in both directions |
| 1.x (other patch releases) | expected to work | the 1.x wire format has been stable; `FormatProbe verify` will tell you |
| 2.x | untested | if a 2.x line appears, regenerate the fixtures before trusting it |

To check a different version yourself:

```bash
# edit the PackageReference in tools/FormatProbe/FormatProbe.csproj
dotnet run --project tools/FormatProbe -- verify tests/fixtures
```

A non-zero exit means the installed MemoryPack no longer produces the committed
bytes — read the diff it prints before changing anything.

## Verified fixture coverage

| Area | Cases |
|---|---|
| primitives, special floats, enums | 3 |
| strings (ASCII, Korean, emoji, empty, null, top-level) | 5 |
| collections, nested collections, bool lists, object lists | 7 |
| objects, nested objects, null members, ordering, empty | 8 |
| dictionaries, sets, key/value pairs | 4 |
| unmanaged structs, padding, Pack=1, numerics | 7 |
| `Nullable<T>` (value and managed) | 6 |
| tuples and value tuples | 2 |
| unions (small tag, wide tag, null, top-level) | 5 |
| .NET types (Guid, DateTime, TimeSpan, decimal, Half, Int128) | 3 |
| VersionTolerant, including all three length encodings | 5 |

53 fixtures, ~72 KB of golden bytes.

---

## Compilers

C++23 is required — the library uses `std::span`, `if constexpr`, concepts,
`std::endian`, designated defaults and (optionally) `std::expected`.

| Compiler | Minimum | Verified | Notes |
|---|---|---|---|
| MSVC | v143 (Visual Studio 2022) | **19.51 (VS 2026)** — built and tested | build with `/std:c++latest` (or `/std:c++23preview`) and `/utf-8` |
| GCC | 13 | not yet built | `-std=c++23` |
| Clang | 16 | not yet built | `-std=c++23`; `std::expected` needs libc++ 17+ or libstdc++ 13+ |
| Apple Clang | Xcode 15 | not yet built | |

> Only the MSVC column has actually been exercised. The code is written to the
> other toolchains' warning sets (`-Wall -Wextra -Wpedantic -Werror -Wshadow
> -Wconversion -Wsign-conversion`) and uses no MSVC-specific constructs, but that
> is an expectation, not a measurement. If you build on GCC or Clang, a report
> either way is welcome.

If `<expected>` is unavailable, `MEMORYPACK_HAS_EXPECTED` is 0 and the
`TryDeserialize` / `TrySerializeTo` API is simply not declared. Everything else
works unchanged.

### How this is verified

There is no hosted CI in this repository; the verification is the local checklist
in the [README](../README.md#full-verification). The current tree was built and
tested with:

- MSVC 19.51.36248 (Visual Studio 2026 Community), Ninja, Debug and Release
- CMake 4.3.1
- .NET SDK 10.0.301
- All library headers compile warning-free under `/W4 /WX /permissive-`, and the
  tests additionally under `-Wall -Wextra -Wpedantic -Werror -Wshadow
  -Wconversion -Wsign-conversion`.

---

## Platforms

| Platform | Status |
|---|---|
| Windows x64 | ✅ built and tested (MSVC 19.51, Debug and Release) |
| Linux x64 (GCC, Clang) | expected to work; not yet built |
| macOS (Apple Clang) | expected to work; not yet built |
| 32-bit targets | supported by design — the bounds check is written so it cannot overflow on 32-bit `size_t`; not yet built |
| Big-endian | supported by design — all primitives, lengths and headers are byte-swapped automatically; not yet built |

### Big-endian caveat

Everything is byte-swapped except the **unmanaged struct** path
(`WriteUnmanaged`, `ReadUnmanaged`, `WriteUnmanagedCollection`,
`ReadUnmanagedCollection`, and `std::optional<unmanaged>`), which copies raw
bytes and therefore cannot be swapped without knowing the struct's fields. Those
functions `static_assert` on a big-endian target. Serialize the members
individually if you need to support one.

---

## Build configurations

| Configuration | Supported |
|---|---|
| Exceptions enabled (default) | ✅ built and tested |
| `MEMORYPACK_NO_EXCEPTIONS` / `_HAS_EXCEPTIONS=0` | ✅ built and tested (ctest: `memorypack_tests_noexcept`, runs on every build — not a one-time manual check) — errors surface through the reader/writer error state and `std::expected` |
| `-fno-exceptions` (GCC/Clang spelling) | expected to work; the same code path, not yet built |
| `-fno-rtti` | ✅ by construction — no RTTI is used |
| AddressSanitizer (MSVC) | ✅ built and tested — `-DMEMORYPACK_SANITIZE=address`, no extra install on Windows; `tools/verify.ps1 -Asan` |
| AddressSanitizer + UndefinedBehaviorSanitizer (Clang/libFuzzer) | ✅ run, not yet CI — see the fuzzing log in [docs/security.md](security.md#fuzzing) |
| Static analysis (`clang-tidy`, MSVC `/analyze`) | configured via `.clang-tidy` |

---

## Language and runtime versions for the tooling

The C# side of this repository (samples, `FormatProbe`, `cs2cpp`) targets
**.NET 10**. The library itself has no .NET dependency — you only need the SDK to
regenerate fixtures, run the samples, or use the code generator.

---

## Semantic versioning

The project follows SemVer. Before 1.0, minor versions may change the C++ API;
the **wire format** will not change except to correct a proven incompatibility
with C# MemoryPack, and any such correction is documented in
[CHANGELOG.md](../CHANGELOG.md) with the fixture that proves it.
