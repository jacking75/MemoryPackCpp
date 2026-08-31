# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Before 1.0, minor versions may change the C++ API. The **wire format** will not
change except to correct a proven incompatibility with C# MemoryPack, and any
such correction is listed here together with the fixture that proves it.

---

## [Unreleased]

Hardening pass closing the verification gaps identified in the 2026-08-31
review (see `docs/dev/hardening-plan.md`): fuzzing that had never actually run,
a no-exceptions build with no regression coverage, unmanaged struct padding
protected only by documentation, and a nine-command manual verification
checklist.

### Added

- **Fuzzing is now a standing regression asset, not a one-off.** Three layers:
  - Layer A: `tests/fuzz/fuzz_replay.cpp` replays the committed corpus
    (`tests/fuzz/corpus/`, seeded from the real C# fixtures by
    `tools/seed_fuzz_corpus.py`) as the `memorypack_fuzz_replay` `ctest`
    target — no sanitizer or clang required, runs on every build.
  - Layer B: `-DMEMORYPACK_SANITIZE=address` builds the test suite under MSVC
    AddressSanitizer with no extra install on Windows.
  - Layer C: an actual libFuzzer session (WSL on Windows), which found and
    fixed two real bugs on its first run — see "Fixed" and the fuzzing log in
    `docs/security.md#fuzzing`.
- **`memorypack_tests_noexcept`**: the unit test suite, built with
  `MEMORYPACK_NO_EXCEPTIONS`/`_HAS_EXCEPTIONS=0`, now runs in `ctest` and
  reports the same check count as the exception-enabled build — the
  no-exceptions path (used by Unreal Engine and console toolchains) can no
  longer regress silently.
- **`MEMORYPACK_UNMANAGED_EXACT(Type, size, m1, ...)`** and
  **`MEMORYPACK_UNMANAGED_SCRUBBED(Type, size, m1, ...)`**: the unmanaged
  struct padding leak documented in `docs/security.md#unmanaged-struct-padding`
  is now either proven impossible at compile time (`_EXACT`, for a struct with
  no padding) or always zeroed at runtime (`_SCRUBBED`), instead of depending
  on the caller value-initializing every instance. `tools/cs2cpp` generates
  one or the other automatically for every unmanaged struct, so generated code
  is structurally immune to the leak.
- **`tools/verify.ps1`**: runs the full local verification checklist — build,
  `ctest`, `FormatProbe verify`/`check-cpp`, `cs2cpp.Tests`, `cs2cpp --check`,
  and the sample E2E pairs — as one command with one exit code, finding the
  Visual Studio developer environment itself. `-Quick` skips the
  .NET-dependent and E2E steps; `-Asan` adds the MSVC ASan build. An opt-in
  `.githooks/pre-push` runs `-Quick` before every push.

### Fixed

- **`MemoryPackReader::ReadVector<T>`** could dereference a misaligned
  pointer when bulk-reading an arithmetic vector: the read position is the sum
  of every preceding field's size and is not generally a multiple of
  `alignof(T)` (found by fuzzing within the first few thousand executions —
  this is reachable from real packet layouts, e.g. a one-byte field followed
  by an `int32` vector, not just a fuzzer-constructed input). Now falls back
  to `memcpy` when the source is misaligned.
- The wire format did not change — all 53 fixtures in `tests/fixtures/` and
  all 22 `FormatProbe check-cpp` cases remain byte-identical.

### Changed

- `docs/security.md`, `docs/compatibility.md`, `docs/api-reference.md`,
  `docs/benchmarks.md`, `docs/performance.md`, `CLAUDE.md`: updated for the
  above.

---

## [0.2.0] - 2026-08-30

The release that turns a working prototype into something you can put in front of
a real client. The headline change is that compatibility with C# MemoryPack is no
longer asserted — it is **tested**, in both directions, on every push.

### Added

#### Verified interoperability

- `tools/FormatProbe`: a .NET tool that serializes 53 cases with the real
  MemoryPack package (pinned to 1.21.4) and writes golden byte fixtures to
  `tests/fixtures/`, with an annotated hex report.
  - `generate` captures fixtures, `verify` fails when the installed MemoryPack
    stops producing the committed bytes, `check-cpp` validates bytes produced by
    the C++ tests by deserializing them in C#.
- `tests/interop_tests.cpp`: replays every fixture and asserts that the C++ reader
  decodes it correctly **and** that the C++ writer re-emits byte-identical output.
- A local verification checklist (see the README) that runs both directions, so a
  wire-format regression is caught before it is committed.

#### Type support

- **Collections of arbitrary element types** — `List<UserType>`, nested
  collections, `std::vector<std::optional<T>>` via `WriteCollection`/`ReadCollection`.
- **Unions** — `std::variant` mapped to C# `[MemoryPackUnion]`, including the
  wide-tag (>= 250) encoding and null unions, declared with `MEMORYPACK_UNION_TAG`.
- **Unmanaged structs** — `MEMORYPACK_UNMANAGED(T, size)`, `WriteUnmanaged`,
  `ReadUnmanaged`, and the bulk `WriteUnmanagedCollection`/`ReadUnmanagedCollection`
  path for `List<UnmanagedStruct>`. The size argument is asserted at compile time.
- **`Nullable<T>`** — `std::optional<T>` now follows whichever of C#'s four null
  encodings matches `T`, driven by `WireNullEncoding<T>`. Added
  `WriteNullable`/`ReadNullable` (unmanaged `T`) and
  `WriteNullableObject`/`ReadNullableObject` (managed struct `T`).
- **`std::vector<bool>`** — previously failed to compile; now encodes one byte per
  element like C# `List<bool>`.
- **More containers** — `std::set`, `std::unordered_set`, `std::deque`,
  `std::list`, `std::pair` (as `KeyValuePair`), `std::unique_ptr`,
  `std::shared_ptr`.
- **.NET value types** — `memorypack::Guid` (with parse/format), `DateTime`
  (`std::chrono` conversion), `TimeSpan`, `DateTimeOffset`, `Decimal`, `Half`
  (float conversion), `Int128`/`UInt128`, `Vector2/3/4`, `Quaternion`, and
  `char16_t` for C# `char`.
- **VersionTolerant objects** — `VersionTolerantWriter`/`VersionTolerantReader`
  for `[MemoryPackable(GenerateType.VersionTolerant)]`, including all three
  member-length encodings (`<= 127`, `0x84` + uint16, `0x82` + uint32).
- **UTF-16 strings** — `WriteStringUtf16` and `std::u16string` support.

#### API

- `MEMORYPACK_DEFINE(Type, members...)` generates the whole serializer from a
  member list, with version tolerance built in. `MEMORYPACK_DEFINE_EMPTY` covers
  member-less types.
- Generic `writer.Write(x)` / `reader.Read(x)` / `reader.Read<T>()` dispatch,
  backed by a `MemoryPackFormatter<T>` customization point and a `Serializable<T>`
  concept.
- Zero-copy and in-place reads: `ReadStringView()`, `ReadString(std::string&)`,
  `ReadVector(std::vector<T>&)`, `ReadCollection(std::vector<T>&)`.
- `Serialize(value, outVector)` and `SerializeTo(span, value)` for allocation-free
  serialization.
- `DeserializeExact<T>()` fails when the input is not fully consumed — a cheap way
  to catch a C#/C++ member-order mismatch during development.
- `memorypack/packet.hpp`: `[2B packetId][4B bodyLength]` framing with
  `MakePacket`, `WritePacket`, `PeekPacketHeader`, and a `PacketFrameParser` that
  reassembles TCP streams with a maximum body length.
- Reader navigation: `Seek`, `Reset`, `SubReader`, and a named `ObjectHeader`
  result type (structured bindings keep working).
- `MemoryPackWriter` is now movable, and accepts `std::span<uint8_t>` and
  `std::span<std::byte>` buffers.
- Version macros: `MEMORYPACK_VERSION_MAJOR/MINOR/PATCH/STRING`.

#### Safety

- `ReaderOptions` — configurable `maxCollectionLength`, `maxStringLength` and
  `maxDepth` for untrusted input.
- Exception-free operation: `MEMORYPACK_NO_EXCEPTIONS` (auto-detected from
  `__cpp_exceptions`), a reader/writer error state, and the `std::expected`-based
  `TryDeserialize` / `TrySerializeTo`.
- `MemoryPackError` codes and `MemoryPackException` carrying the code and the byte
  offset of the failure.
- `tests/fuzz/fuzz_deserialize.cpp` — a libFuzzer harness covering objects,
  containers, unions, the raw reader API and the frame parser, to be run under
  ASan/UBSan when touching the reader.

#### Tooling and infrastructure

- A documented verification checklist covering the Debug and Release builds,
  `ctest`, fixture verification in both directions, the generator drift check and
  the fuzz harness. This repository deliberately has no hosted CI, so those
  commands are the safety net.
- `tools/amalgamate.py` produces a single-header build.
- Benchmarks (`-DMEMORYPACK_BUILD_BENCHMARKS=ON`) with a raw `memcpy` baseline.
- Ten runnable, commented examples under `examples/`, each self-checking and
  registered as a CTest case.
- Two new sample pairs: `CppServer` + `CsClient` (a **C++ server** serving a
  **C# client**, the Unity-style deployment) and `ChatClientConsole` (a
  cross-platform console chat client, so the chat sample is no longer
  Windows-only).
- The sample packet headers are now **generated** from the C# definitions by
  `cs2cpp`; `--check` reports drift without writing.
- `CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md`, issue and PR templates,
  `.editorconfig`, `.gitattributes`, `.clang-format`, `.clang-tidy`.
- Documentation set under `docs/`: wire format (with real captured bytes), type
  mapping, serialization guide, performance, security, error handling,
  compatibility, benchmarks, FAQ, and Unreal/Unity integration guides.
- English `README.md` with the Korean version moved to `README.ko.md`.

### Fixed

Found by reading the code against the newly captured fixtures:

- **`std::vector<bool>` did not compile.** `WriteVector`/`ReadVector` assumed
  `data()`, which the bit-packed specialization does not have — so C# `List<bool>`
  was unreachable from C++.
- **Unbounded allocation from a 4-byte length field.** `ReadUnorderedMap` called
  `reserve()` with an unvalidated length read from the input, letting a tiny
  hostile packet request gigabytes. All collection and string readers now validate
  the declared length against the bytes actually remaining *before* allocating.
- **Integer overflow in the bounds check.** `pos_ + n > size_` can wrap on a
  32-bit `size_t`, letting an oversized read pass the check. Rewritten as
  `n > size_ - pos_`, which cannot overflow.
- **Reserved header values were accepted.** `WriteObjectHeader` allowed member
  counts of 250-255, which collide with the union wide-tag marker and the null
  code; the reader accepted them too. Both now reject the reserved range.
- **`size()` to `int32_t` truncation.** Collections and strings larger than
  `INT32_MAX` were silently truncated; they now report `LengthLimit`.
- **Lone UTF-16 surrogates produced invalid UTF-8.** The decoder did not verify
  that a low surrogate followed a high one. Unpaired surrogates now decode to
  U+FFFD.
- **Missing `IMemoryPackable<T>` specializations produced a linker error.** The
  primary template is no longer defined, so the failure is a compile-time message
  pointing at the documentation.
- **`C4702` from the library header.** In an optimized MSVC build the compiler
  proved `Fail()` never returns, so the `return` after each call - the path the
  no-exceptions build takes - looked unreachable. Every consumer building at
  `/W4 /WX` hit it; now suppressed inside the header.
- **Exceptions could escape a destructor.** `VersionTolerantWriter` flushes in
  its destructor; a failure there would have called `std::terminate`. It now
  records the error in the writer instead, and refuses to truncate a member
  count above 249.
- **Silent truncation in the container formatters.** `std::deque`/`list`/`set`/
  `map` wrote an empty collection header when the size exceeded `INT32_MAX`
  instead of reporting `LengthLimit`.
- **`GetBuffer()` mutated through a const `this`.** The owned buffer is now
  `mutable`, so trimming it to the write position is well-defined.
- **Documentation errors.** The repository URL (`heungbae` -> `jacking75`), the
  claim that unmanaged structs map to *packed* C++ structs (they use natural
  alignment, padding included), the claim that C++ must match C#'s string
  encoding (it does not — the C# reader detects it), and an out-of-date string
  example in the wire-format section.

### Changed

- `ReadObjectHeader()` returns a named `ObjectHeader { count, isNull }` instead of
  `std::pair`. Structured bindings are unaffected.
- `WriteString` takes `std::string_view`, so `const char*` and `string_view` bind
  without a temporary.
- `ReadVector` reads in a single pass (`assign`) instead of zero-filling and then
  copying.
- Headers are split into `core.hpp`, `containers.hpp`, `dotnet.hpp` and
  `packet.hpp`; `memorypack.hpp` remains the umbrella, so existing includes keep
  working.
- Library headers are now ASCII-only, so consumers on a non-UTF-8 codepage do not
  get MSVC C4819.
- `MemoryPackWriter` is movable (it was neither copyable nor movable).
- Tests build with `-Werror` / `/WX` plus `-Wshadow -Wconversion -Wsign-conversion`.

### Known limitations

- **Unmanaged struct padding is transmitted.** `WriteUnmanaged` copies a struct
  verbatim, padding included, and C++ only guarantees those bytes are zero after
  *value*-initialization (`T v{};`). After a plain `T v;` they are indeterminate,
  which makes the output non-reproducible and can disclose stack contents. The
  library cannot zero them without knowing the layout, so this is documented and
  tested rather than fixed - see
  [docs/security.md](docs/security.md#unmanaged-struct-padding).
- **Hash containers do not round-trip byte-identically.** C# `Dictionary`/
  `HashSet` enumerate in an implementation-defined order while `std::map`/
  `std::set` are sorted. Values survive; bytes only match when C# happens to
  enumerate in sorted order.
- **A fully-unmanaged `ValueTuple` has CLR-determined field order.** `(int, float,
  double)` is laid out as `double, int, float`. Mirror the observed layout rather
  than the declaration order.
- `GenerateType.CircularReference` is not implemented.

### Notes for upgrading from 0.1.0

The existing API is source-compatible; the 0.1.0 test suite passes unchanged
against 0.2.0. Two behaviours are stricter on purpose:

- `WriteObjectHeader(n)` now fails for `n > 249`. Such a header was never valid on
  the wire, so any code hitting this was already producing bytes C# could not read.
- Collection and string lengths that cannot fit the remaining input are rejected.
  Well-formed payloads are unaffected.

---

## [0.1.0] - 2026-06-23

Initial release.

### Added

- Header-only `MemoryPackWriter` / `MemoryPackReader` with primitives, strings,
  objects, collections, fixed arrays, maps, tuples and enums.
- Three buffer modes: internal vector, caller-owned vector, fixed-size buffer.
- `IMemoryPackable<T>` extension point and top-level `Serialize`/`Deserialize`.
- CMake package with an installable `memorypack::memorypack` interface target.
- A dependency-free unit test suite (59 checks).
- `tools/cs2cpp`, a regex-based C# to C++ packet-definition generator.
- Four samples: a C# test server with a C++ console client, and a C# chat server
  with a Win32 C++ chat client.

### Fixed

- **String wire format was incompatible with C# MemoryPack.** `WriteString` /
  `ReadString` used `[int32 byteLen][utf8]` instead of the real
  `[int32 ~byteCount][int32 utf16Length][utf8]`, which meant every packet
  containing a string failed against a real C# peer. Corrected and verified
  against actual MemoryPack output.

[0.2.0]: https://github.com/jacking75/MemoryPackCpp/releases/tag/v0.2.0
[0.1.0]: https://github.com/jacking75/MemoryPackCpp/releases/tag/v0.1.0
