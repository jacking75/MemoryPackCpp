# Security Policy

MemoryPackCpp parses binary data that, in a typical deployment, arrives over a network
socket from a client you do not control. Parser bugs in that position are security bugs,
and we treat them as such.

## Supported Versions

| Version | Supported |
|---------|-----------|
| 0.2.x   | Yes       |
| < 0.2   | No        |

Only the latest `0.2.x` release line receives security fixes. Because the library is
header-only, "upgrading" means replacing the contents of `include/memorypack/` — there
is no ABI to worry about and no binary to redeploy separately from your application.

While the project is pre-1.0, a security fix may ship together with a source-compatible
API adjustment if the vulnerability cannot be fixed otherwise. Such changes are called
out in the release notes.

## Reporting a Vulnerability

**Please do not open a public GitHub issue for a security vulnerability.**

Report it privately through either channel:

1. **GitHub private security advisory** (preferred) —
   <https://github.com/jacking75/MemoryPackCpp/security/advisories/new>. This keeps the
   discussion, the fix, and the CVE request in one place and lets us credit you when the
   advisory is published.
2. **Email** — <jacking75@gmail.com>, with `[MemoryPackCpp security]` in the subject.

A good report includes:

- The affected version or commit hash, compiler, and platform.
- The input bytes that trigger the problem, as hex or an attached `.bin` file.
- The C++ type and the `IMemoryPackable` specialization used to read them.
- What happens: out-of-bounds read, crash, unbounded allocation, infinite loop, and any
  sanitizer output (ASan/UBSan traces are extremely helpful).
- The `ReaderOptions` in effect, if not the defaults.

### What to expect

This is a volunteer-maintained project with a single maintainer, so response times are
**best effort** rather than contractual:

| Stage | Target |
|-------|--------|
| Acknowledgement of your report | within 7 days |
| Initial assessment (valid / not / severity) | within 14 days |
| Fix or mitigation for a confirmed issue | as soon as practical, typically within 30 days |

If you have not heard back within 14 days, please ping the same channel — mail does get
lost. We will keep you informed as the fix progresses, and we ask that you give us a
reasonable window to ship before disclosing publicly. Reporters are credited in the
advisory unless they ask not to be.

## Threat Model

Understanding what this library does and does not defend against is essential to using
it safely.

### The deserializer is designed to be safe on untrusted input

`MemoryPackReader` is written on the assumption that the bytes it is handed are hostile.
Specifically:

- **All reads are bounds-checked.** Every primitive, header, and payload read verifies
  that the requested bytes are actually present in the buffer before touching them. A
  truncated packet produces a `MemoryPackError`, not an out-of-bounds read.
- **Declared lengths are validated against the remaining input.** A collection or string
  header that claims more elements or bytes than the buffer could possibly contain is
  rejected immediately, before any memory is reserved. A malformed packet cannot make the
  reader allocate gigabytes on the strength of a four-byte length field.
- **Negative and impossible lengths are rejected**, including values that would overflow
  a 32-bit element count.
- **Configurable limits are available through `ReaderOptions`** for callers that want to
  reject implausible packets earlier and harder than "it fits in the buffer":

  | Field | Default | Purpose |
  |-------|---------|---------|
  | `maxCollectionLength` | `0xFFFFFFFF` | Maximum element count for any single collection |
  | `maxStringLength` | `0xFFFFFFFF` | Maximum byte count for any single string payload |
  | `maxDepth` | `256` | Maximum object nesting depth, bounding recursion |

  ```cpp
  memorypack::ReaderOptions options;
  options.maxCollectionLength = 4096;      // no packet of ours has a bigger list
  options.maxStringLength     = 64 * 1024; // nor a bigger string
  options.maxDepth            = 16;

  memorypack::MemoryPackReader reader(buffer, size, options);
  ```

  Servers accepting connections from the public internet **should** set these to values
  derived from their own protocol rather than relying on the permissive defaults. The
  nesting-depth cap in particular is what prevents a deeply nested adversarial packet
  from exhausting the C++ call stack.

- **Errors are reported, not thrown.** Failure sets a sticky error state on the reader
  (`MemoryPackError`) that the caller must check; a reader in the failed state does not
  produce further data. Check it before trusting the deserialized value.

Within that scope, a crafted input that causes an out-of-bounds read or write, an
unbounded allocation, unbounded recursion, or a hang **is a vulnerability**, and we want
to hear about it.

### `WriteUnmanaged` / `ReadUnmanaged` are a sharp edge, by design

The `WriteUnmanaged`, `ReadUnmanaged`, `WriteUnmanagedCollection`, and
`ReadUnmanagedCollection` family exists because C# serializes unmanaged structs by
copying their memory verbatim, with no per-member header, and matching that is the whole
point of this library. These functions therefore **copy raw struct bytes in and out of
the buffer**.

The compile-time guards are real but limited: the type must be trivially copyable, and
byte-for-byte copying is only accepted on little-endian hosts. Nothing else can be
checked. In particular the library **cannot** verify that:

- your C++ struct's size, field order, and padding match the C# struct's,
- the incoming bytes were produced by the type you are reading them into,
- the struct contains no pointers, indices, handles, enum values, or lengths that other
  code will later trust.

Consequences of getting it wrong range from silent data corruption (a padding or layout
mismatch) to memory-safety failures elsewhere in your program (a struct field used as an
index or size, filled from attacker-controlled bytes).

**Use `ReadUnmanaged` only with types whose layout you control**, whose C# counterpart
you have verified with a golden fixture, and that contain nothing an attacker could turn
into a pointer, a length, or an index without further validation. Prefer the
member-by-member `IMemoryPackable` path for anything reachable from untrusted input where
you are not certain of the layout. This is a documented sharp edge, so a report amounting
to "`ReadUnmanaged` into a mismatched struct produced garbage" will be answered with this
section rather than an advisory.

### Out of scope

- Confidentiality, integrity, and authenticity of data in transit. MemoryPack is a
  serialization format, not a secure channel: it is neither encrypted nor authenticated.
  Use TLS, and authenticate the peer, before you trust anything you decoded.
- Application-level validation. The reader guarantees that a value was decoded within the
  bounds of the buffer; it does not know that your `itemCount` should be at most 8 or that
  a `playerId` must belong to the sender. Validate semantics yourself.
- Resource limits you declined to set. Accepting a 500 MB packet from an anonymous client
  because `maxCollectionLength` was left at its default is a deployment decision, not a
  library bug — although reports of limits that fail to hold when set *are* in scope.
- Vulnerabilities in the C# MemoryPack library itself. Report those to
  <https://github.com/Cysharp/MemoryPack>.
- Bugs in the `samples/` and `tools/` directories, which are demonstration code and
  explicitly not hardened for production use. They are still worth reporting as ordinary
  issues.
