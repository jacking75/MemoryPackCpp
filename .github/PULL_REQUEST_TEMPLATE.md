<!--
Thanks for the pull request. Keep it small and focused - one behavior change per PR.
See CONTRIBUTING.md for the full expectations.
-->

## Summary

<!-- What changed, and why. One or two sentences is often enough. -->

## Related issue

<!-- e.g. Fixes #123, Refs #456. Write "none" if this stands alone. -->

## Does this change the bytes on the wire?

- [ ] No - the serialized output is byte-for-byte identical to before.
- [ ] Yes - and the golden fixtures in `tests/fixtures/` were regenerated against real
      C# MemoryPack with
      `dotnet run --project tools/FormatProbe -- generate tests/fixtures`, then verified
      with `dotnet run --project tools/FormatProbe -- verify tests/fixtures`.

<!-- If yes: which fixtures changed, and why is the new output the correct one? -->

## Checklist

- [ ] Tests added or updated for every behavior change (bug fixes include a test that
      fails without the fix; features cover the happy path, the null case, and truncated
      or malformed input).
- [ ] `ctest --test-dir build --output-on-failure` passes locally.
- [ ] Fixtures regenerated and committed if the wire format changed (see above).
- [ ] Documentation updated where affected - `README.md`, header doc comments,
      `CLAUDE.md`.
- [ ] No new compiler warnings under `/W4 /WX` (MSVC) or
      `-Wall -Wextra -Wpedantic -Werror` (GCC/Clang).
- [ ] Headers touched are still ASCII-only (no box-drawing characters, smart quotes, or
      non-English text) so MSVC does not emit C4819 on non-UTF-8 codepages.
- [ ] Coding conventions followed: namespace `memorypack`, PascalCase types and methods,
      camelCase variables, UPPER_SNAKE_CASE constants, 4-space indent, `#pragma once`.
- [ ] Verification checklist run (see `README.md`, "Full verification"): build +
      `ctest`, `FormatProbe verify`, `check-cpp`, `cs2cpp --check`. There is no
      hosted CI, so note below which of these you ran and on which compiler.

## How was this verified?

<!--
Compilers and platforms you built on, whether you ran the C#/C++ sample pair, any hex
dumps that show the before/after bytes.
-->
