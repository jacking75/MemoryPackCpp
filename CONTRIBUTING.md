# Contributing to MemoryPackCpp

Thanks for taking the time to contribute. MemoryPackCpp is a C++23 header-only
library that implements the binary wire format of
[Cysharp's MemoryPack](https://github.com/Cysharp/MemoryPack) so that C++ servers and
clients can exchange data with C#/Unity applications byte-for-byte.

That single goal shapes every rule below: **compatibility with the real C# library is
not an opinion, it is a measurable property**, and every change has to keep it true.

- [Code of Conduct](#code-of-conduct)
- [Getting started](#getting-started)
- [Building and testing](#building-and-testing)
- [Wire-format changes require golden fixtures](#wire-format-changes-require-golden-fixtures)
- [Coding conventions](#coding-conventions)
- [Warnings are errors](#warnings-are-errors)
- [Commits and pull requests](#commits-and-pull-requests)
- [Reporting a wire-format incompatibility](#reporting-a-wire-format-incompatibility)
- [Reporting a security issue](#reporting-a-security-issue)
- [License](#license)

## Code of Conduct

This project ships a [Code of Conduct](CODE_OF_CONDUCT.md). By participating you are
expected to uphold it. Report unacceptable behavior to <jacking75@gmail.com>.

## Getting started

Prerequisites:

| Tool | Version |
|------|---------|
| C++ compiler | MSVC v143 (Visual Studio 2022) or newer, GCC 13+, Clang 16+ |
| CMake | 3.21 or newer |
| .NET SDK | 10.0 (only for `tools/FormatProbe`, `tools/cs2cpp` and the C# samples) |

The library itself is header-only and has no dependencies. The unit tests use a small
self-contained harness in `tests/`, so there is nothing to fetch or vendor.

Good first contributions:

- A missing C# type mapping (with a fixture proving the bytes match).
- A test that pins down behavior the suite does not cover yet.
- Documentation fixes, especially in `README.md` and the header doc comments.
- Portability fixes for compilers or platforms we do not test on.

If you are planning something large — a new public API, a change to the reader's error
model, a new buffer mode — please open an issue first so we can agree on the shape
before you write the code.

## Building and testing

```bash
cmake -B build -DMEMORYPACK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows with the Visual Studio generator, add a configuration to the build and test
steps:

```bash
cmake -B build -DMEMORYPACK_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Useful CMake options:

| Option | Default | Meaning |
|--------|---------|---------|
| `MEMORYPACK_BUILD_TESTS` | `ON` when top level | Build and register the unit tests |
| `MEMORYPACK_BUILD_SAMPLES` | `OFF` | Build the C++ sample clients (`ChatClient` is Windows-only) |
| `MEMORYPACK_INSTALL` | `ON` when top level | Generate install and `find_package` rules |

The C# side is built with `dotnet`, not CMake:

```bash
dotnet build tools/FormatProbe -c Release
dotnet build tools/cs2cpp     -c Release
dotnet build samples/CSharpServer -c Release
```

**Run the full test suite before you open a pull request.** A change that compiles but
was never run against `ctest` is not ready for review.

## Wire-format changes require golden fixtures

The wire format is defined by whatever the C# MemoryPack library actually emits — not
by our reading of the spec, and not by what our own writer happens to produce. A test
where our writer feeds our reader proves round-tripping and nothing about compatibility.

Therefore: **any change that affects the bytes on the wire must be proven against real
C# MemoryPack via the golden fixtures in `tests/fixtures/`.**

That covers, at minimum:

- New or changed `IMemoryPackable` specializations.
- Changes to object headers, collection headers, string encoding, union tags, null
  representations, or unmanaged struct layout.
- New C# type mappings (`DateTime`, `Guid`, `decimal`, `TimeSpan`, nullable value types,
  tuples, dictionaries, sets, and friends).
- Changes to version-tolerance behavior.

### Regenerating the fixtures

`tools/FormatProbe` is a .NET 10 console app that references the real
`MemoryPack` NuGet package and serializes a catalogue of types defined in
`tools/FormatProbe/FixtureCases.cs`. It writes one `.bin` per case plus a
`manifest.json` (name, C# type, description, length, hex) and a human-readable
`report.txt`.

```bash
# 1. Add or edit a case in tools/FormatProbe/FixtureCases.cs (and Types.cs).
# 2. Regenerate the committed fixtures:
dotnet run --project tools/FormatProbe -- generate tests/fixtures

# 3. Confirm the committed fixtures still match what C# produces:
dotnet run --project tools/FormatProbe -- verify tests/fixtures

# 4. Confirm C# can read what C++ wrote (after running the C++ tests):
dotnet run --project tools/FormatProbe -- check-cpp <cppOutputDir>
```

`generate` deletes stale `.bin` files, so a renamed case cannot linger. `verify` exits
with code 1 on any mismatch, which is how you notice that an upstream MemoryPack
release changed the format. Run it after any MemoryPack package upgrade.

Rules for fixtures:

- **Commit the regenerated `.bin` files, `manifest.json` and `report.txt` together with
  the code change.** A fixture update in isolation, or a code change without the
  fixture it claims to match, will be sent back.
- Do not hand-edit a `.bin`. If the bytes are wrong, the fix belongs in the C# case or
  in our implementation.
- If regenerating changes fixtures you did not intend to touch, stop and say so in the
  pull request. That usually means the MemoryPack package version moved, and it is a
  finding in its own right — the version is recorded in
  `tools/FormatProbe/FormatProbe.csproj` and echoed into `manifest.json`.
- Keep each case minimal and focused on one aspect of the format. Fixtures are
  documentation as much as they are test data.

## Coding conventions

The library is header-only; everything under `include/memorypack/` is compiled by every
consumer, so it is held to a stricter standard than the samples and tools.

- **Namespace**: everything public lives in `memorypack`. Implementation details go in
  `memorypack::detail`.
- **Types** (classes, structs, enums, concepts): `PascalCase` — `MemoryPackWriter`,
  `ReaderOptions`, `IMemoryPackable`.
- **Methods and functions**: `PascalCase` — `WriteInt32`, `ReadString`,
  `ReadObjectHeader`.
- **Variables, parameters, members**: `camelCase`. Private data members carry a
  trailing underscore — `options_`, `depth_`.
- **Constants and macros**: `UPPER_SNAKE_CASE`.
- **Indentation**: 4 spaces, never tabs. Column limit 100. `.clang-format` and
  `.editorconfig` in the repository root encode this; run `clang-format` on the files
  you touched rather than reformatting whole files.
- **Header guards**: `#pragma once`, not include guards.
- **Headers must stay ASCII-only.** No box-drawing characters, no smart quotes, no
  non-English text in comments or identifiers. MSVC emits `C4819` for non-UTF-8
  codepages (CP949, CP932, ...) when a header contains bytes outside ASCII, which turns
  into a hard error for downstream users compiling with `/WX`. Use `--` or `===` for
  comment rules and plain `'` / `"` for quotes.
- **Comments in the public headers are written in English**, because they are read by
  every consumer of the library. Repository documentation may be bilingual;
  `README.md` is Korean and that is intentional.
- Prefer `std::span`, `std::optional`, `std::string_view` and structured bindings over
  raw pointer/length pairs in new API surface.
- No exceptions on the hot path and no allocations the caller did not ask for. The
  reader reports failures through `MemoryPackError`, not by throwing.
- No new third-party dependencies in `include/`. The library must remain drop-in
  copyable.

## Warnings are errors

Headers must compile clean at the highest warning levels on every supported compiler:

```bash
# MSVC
/W4 /WX /permissive- /utf-8

# GCC / Clang
-Wall -Wextra -Wpedantic -Werror
```

A pull request that introduces a new warning will not be merged — including warnings
that only appear on a compiler you do not have locally. There is no hosted CI here,
so say in the pull request which compilers you actually built with; if a reviewer
hits a warning you cannot reproduce, we will work it out together.
`#pragma warning(disable: ...)` is a last resort and needs a comment explaining why.

Static analysis configuration lives in `.clang-tidy`. It is advisory
(`WarningsAsErrors: ''`) but new findings in code you touched should be fixed or
explained.

## Commits and pull requests

- **Keep pull requests small and focused.** One behavior change per pull request.
  Formatting-only churn belongs in its own commit, separate from the logic it touches.
- **Every behavior change comes with a test.** Bug fixes get a test that fails before
  the fix; features get tests covering the happy path, the null case, and the truncated
  or malformed input case.
- **Regenerate fixtures if the wire format changed** — see above.
- **Run the verification checklist before asking for review.** There is no hosted CI,
  so the checks in [README.md](README.md#full-verification) are the safety net: build
  and `ctest`, `FormatProbe verify`, the `check-cpp` reverse direction, and
  `cs2cpp --check`. Say in the pull request which ones you ran and on what.
- **Update the docs.** If you change public API, update `README.md`, the header doc
  comment, and `CLAUDE.md` where it describes the affected area.
- **Do not bump the project version** in `CMakeLists.txt`. Releases are cut by the
  maintainer.

Commit messages: a short imperative subject line (72 characters or fewer), a blank
line, then the why. Reference issues with `Fixes #123`. Either English or Korean is
fine for commit messages.

```
Reject collection lengths above the remaining input

A malformed packet could declare a 2^30-element list in a 12-byte buffer,
causing the reader to reserve memory it would never fill. The reader now
bounds every declared length by the bytes actually remaining.

Fixes #42
```

A useful pull request description says: what changed, why, whether the wire format is
affected, and how you verified it.

## Reporting a wire-format incompatibility

This is the most valuable kind of bug report, and it needs specific evidence. Open an
issue with the **Bug report** template and include all of the following:

1. **The C# type** — the complete `[MemoryPackable]` declaration, with member order
   preserved and every attribute (`[MemoryPackOrder]`, `[MemoryPackIgnore]`,
   `[MemoryPackUnion]`, ...) intact.
2. **The C++ struct** and its `IMemoryPackable` specialization, or the `cs2cpp` output
   if it was generated.
3. **The bytes C# produced** — the output of `MemoryPackSerializer.Serialize(value)`,
   as hex, with the value that produced them.
4. **The bytes C++ produced** — the output of our writer for the same logical value, as
   hex.
5. The MemoryPack (C#) package version, the .NET version, your compiler and version,
   and your platform.

Hex dumps beat prose. If you can express the case as a `FixtureCase` in
`tools/FormatProbe/FixtureCases.cs`, that is even better — attach the diff and we can
reproduce it in one command.

If C# and C++ agree but the *values* are wrong after a round trip, that is a different
bug; say so explicitly and include the input and the round-tripped output.

## Reporting a security issue

Do not open a public issue for a vulnerability. See [SECURITY.md](SECURITY.md) for the
private reporting process and the project's threat model.

## License

By contributing, you agree that your contributions are licensed under the
[MIT License](LICENSE) that covers this project.
