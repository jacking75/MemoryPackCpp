# Benchmarks

MemoryPackCpp ships a [Google Benchmark](https://github.com/google/benchmark) suite in
`benchmarks/`. It exists to answer concrete questions about the library's hot paths -
how much of a "serialization cost" is really the allocator, what the zero-copy read
paths buy, and how close the bulk paths get to a raw `memcpy` - not to produce a
marketing number.

The suite is **not built by default**. It is a single target, `memorypack_bench`, built
from `benchmarks/memorypack_bench.cpp`, and Google Benchmark v1.9.1 is pulled in
automatically with `FetchContent` (so the first configure needs network access).

---

## Building

```bash
cmake -B build-bench -DMEMORYPACK_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-bench --config Release
```

> **Always build optimized.** MemoryPackCpp is header-only: every writer and reader
> call is expected to be inlined away. A Debug or default-configured build measures
> the compiler's unoptimized output, not the library, and the numbers will be off by
> an order of magnitude or more. `benchmarks/CMakeLists.txt` deliberately does not
> force a build type - passing `-DCMAKE_BUILD_TYPE=Release` (single-config generators)
> or `--config Release` (multi-config generators such as Visual Studio) is your job.

Google Benchmark itself will print a warning on startup if it detects a debug build or
CPU frequency scaling. Do not publish results from a run that printed such a warning.

## Running

```bash
# Linux / macOS, and Windows with a single-config generator (Ninja, Makefiles)
./build-bench/benchmarks/memorypack_bench
```

```powershell
# Windows, single-config generator (Ninja)
.\build-bench\benchmarks\memorypack_bench.exe

# Windows, multi-config generator (Visual Studio) - note the extra config folder
.\build-bench\benchmarks\Release\memorypack_bench.exe
```

### Useful flags

| Flag | Why |
|------|-----|
| `--benchmark_list_tests` | Print the registered benchmark names without running them. |
| `--benchmark_filter=<regex>` | Run only the matching benchmarks, e.g. `--benchmark_filter=Serialize` or `--benchmark_filter='BM_Deserialize_IntVector.*'`. |
| `--benchmark_repetitions=10 --benchmark_report_aggregates_only=true` | Run each benchmark ten times and print only mean / median / stddev / cv. This is the mode to use for any number you intend to record - a single repetition tells you nothing about run-to-run variance. |
| `--benchmark_out=results.json --benchmark_out_format=json` | Write machine-readable results, for `tools/compare.py` from the Google Benchmark repo or for tracking regressions in CI. |
| `--benchmark_min_time=1s` | Increase per-benchmark sampling time when the coefficient of variation is high. |

A typical "record the numbers" invocation:

```bash
./build-bench/benchmarks/memorypack_bench \
    --benchmark_repetitions=10 \
    --benchmark_report_aggregates_only=true \
    --benchmark_out=results.json \
    --benchmark_out_format=json
```

---

## What each group measures

The source is organized into eight numbered sections with matching banners.

### 1. Small packet - `BM_Serialize_SmallPacket`, `BM_Deserialize_SmallPacket`

A `PlayerState { int32 id; float x, y, z; string name; }` - five members, a short
name, 32 bytes on the wire. **Question: what does one round of the common case cost
end to end?** This is the reference point every other packet-sized number is compared
against.

### 2. Buffer modes - `BM_Serialize_OwnedBuffer`, `BM_Serialize_ReusedWriter`, `BM_Serialize_ExternalVector`, `BM_Serialize_FixedBuffer`, `BM_Memcpy_Baseline`

The same 32-byte packet, serialized five different ways. **Question: how much of the
"serialization cost" is really the allocator?**

- `OwnedBuffer` - `memorypack::Serialize(value)` returns a fresh `std::vector`: one
  heap allocation per iteration.
- `ReusedWriter` - one `MemoryPackWriter` kept alive and rewound with `Clear()`: zero
  allocations after the first iteration.
- `ExternalVector` - one caller-owned `std::vector<uint8_t>`, cleared and appended to.
- `FixedBuffer` - `SerializeTo()` into a stack `std::array<uint8_t, 256>`: no heap at all.
- `Memcpy_Baseline` - a raw `std::memcpy` of exactly the same byte count.

The spread between the first and the last four is the allocator; the gap from the last
four down to the baseline is the actual encoding work.

### 3. Bulk primitives - `BM_Serialize_IntVector`, `BM_Deserialize_IntVector`

`std::vector<int32_t>` at 1024 and 65536 elements (`->Arg(1024)->Arg(65536)`).
**Question: does the arithmetic-vector path stay a bulk copy as N grows?** 1024
elements is ~4 KB and fits in L1; 65536 is ~256 KB and does not, so the pair also shows
where memory bandwidth, rather than the library, becomes the limit.

### 4. In-place bulk read - `BM_Deserialize_IntVector_InPlace`

The same input as group 3, decoded with `reader.ReadVector(out)` into a vector that is
reused across iterations. **Question: what does reusing the destination buffer save
over the returning form?** Identical bytes, identical decode - only the allocation
differs.

### 5. String-heavy payload - `BM_Serialize_ChatMessage`, `BM_Deserialize_ChatMessage`, `BM_Deserialize_ChatMessage_StringView`

A `ChatMessage { int64 timestamp; string sender; string body; }` with a realistic
~60-character body. **Question: what do variable-length UTF-8 strings cost, and how
much of that is just building `std::string`?** The `_StringView` variant answers the
second half: it reads the members manually and uses `ReadStringView()`, which borrows
the input buffer instead of copying out of it.

### 6. Nested objects - `BM_Serialize_ItemList`, `BM_Deserialize_ItemList`

An object holding 100 `Item { int32 id; string name; int32 count; }` objects.
**Question: what is the per-element overhead of the generic object path**, where every
element carries its own member-count header and its own string?

### 7. Unmanaged bulk path - `BM_Serialize_UnmanagedCollection`, `BM_Deserialize_UnmanagedCollection`, `BM_Serialize_GenericCollection`, `BM_Deserialize_GenericCollection`

4096 elements of a three-float struct, encoded two ways. `Vec3` is marked
`MEMORYPACK_UNMANAGED(Vec3, 12)` and goes through
`WriteUnmanagedCollection` / `ReadUnmanagedCollection`; `Vec3Generic` has an identical
C++ layout but is registered with `MEMORYPACK_DEFINE` and walks the generic
per-element path. **Question: how much does the unmanaged bulk path actually buy?**
Both the instruction count and the wire size differ - the bulk form emits
`[4B count][count * 12B]`, the generic form emits `[4B count][count * (1B header + 12B)]`.

### 8. End-to-end framing - `BM_PacketFraming_RoundTrip`

`MakePacket(id, value)`, then the resulting bytes through `PacketFrameParser::Feed()`,
deserializing inside the callback. **Question: what does a full send/receive hop cost**,
including the `[2B id][4B length]` header, the stream-reassembly buffer, and the decode?
This is the only benchmark that keeps an allocation inside the timed region on purpose,
because `MakePacket()` returning a fresh vector is what a naive send path does.

---

## How to read the results

Google Benchmark prints, per row:

```
Benchmark            Time      CPU    Iterations   UserCounters...
BM_Something       XX.X ns  XX.X ns     NNNNNNN    bytes_per_second=... payload_bytes=...
```

**`Time` / `CPU` (ns per iteration)** is the latency of one operation. It is the number
you care about for a request/response path where a single packet's cost sits directly
in the tail latency budget. It is only comparable between two benchmarks that move the
same number of bytes - which is why `payload_bytes` is reported next to it. Comparing
`BM_Serialize_SmallPacket` (32 bytes) against `BM_Serialize_IntVector/65536`
(~256 KB) by `Time` alone is meaningless.

**`bytes_per_second`** is the throughput, derived from `SetBytesProcessed()`, which every
benchmark in this suite sets to `iterations * payloadSize`. This is the number to
compare *across* benchmarks of different payload sizes, and the number that tells you
whether a path is bandwidth-bound or overhead-bound:

- A path that stays at roughly the same bytes/s as the payload grows is
  bandwidth-bound: it is already a bulk copy, and there is nothing left to win.
- A path whose bytes/s climbs steeply with payload size is overhead-bound at small
  sizes - the fixed per-call cost (an allocation, a function call, a header) dominates.

**`payload_bytes`** is a plain counter, not a measurement: it is the encoded size of the
value that benchmark serializes or decodes. It is there so a reader can convert between
the two columns without re-deriving the wire layout.

**Why `BM_Memcpy_Baseline` matters.** MemoryPackCpp is a zero-encoding format: no
varints, no tags, fixed-size little-endian primitives copied verbatim. The theoretical
best case for serializing N bytes is therefore *moving N bytes*, and nothing else. The
memcpy baseline measures exactly that on the machine under test, over the same 32-byte
payload as the small-packet benchmarks. It converts every other row from an absolute
number - which depends on the CPU, the memory clock, the allocator and the compiler -
into a ratio that is actually portable:

> "`BM_Serialize_FixedBuffer` is *k* times the cost of moving the same bytes."

That ratio is what should be tracked over time. A raw nanosecond figure from one laptop
says nothing about a regression on another. If a change makes the ratio worse while
leaving the absolute number unchanged, the machine got faster and the library got
slower.

Finally: read `--benchmark_repetitions` output, not a single run. If the coefficient of
variation (`_cv` row) is above a few percent, the machine is too noisy - close
background work, disable frequency scaling / turbo, pin the process to a core, and rerun.

---

## Results

**No numbers have been measured and published yet.** The table below is a template.

| Benchmark | Time (ns/op) | Throughput (bytes/s) |
|-----------|--------------|----------------------|
| `BM_Serialize_SmallPacket` | (not yet measured) | (not yet measured) |
| `BM_Deserialize_SmallPacket` | (not yet measured) | (not yet measured) |
| `BM_Serialize_OwnedBuffer` | (not yet measured) | (not yet measured) |
| `BM_Serialize_ReusedWriter` | (not yet measured) | (not yet measured) |
| `BM_Serialize_ExternalVector` | (not yet measured) | (not yet measured) |
| `BM_Serialize_FixedBuffer` | (not yet measured) | (not yet measured) |
| `BM_Memcpy_Baseline` | (not yet measured) | (not yet measured) |
| `BM_Serialize_IntVector/1024` | (not yet measured) | (not yet measured) |
| `BM_Serialize_IntVector/65536` | (not yet measured) | (not yet measured) |
| `BM_Deserialize_IntVector/1024` | (not yet measured) | (not yet measured) |
| `BM_Deserialize_IntVector/65536` | (not yet measured) | (not yet measured) |
| `BM_Deserialize_IntVector_InPlace/1024` | (not yet measured) | (not yet measured) |
| `BM_Deserialize_IntVector_InPlace/65536` | (not yet measured) | (not yet measured) |
| `BM_Serialize_ChatMessage` | (not yet measured) | (not yet measured) |
| `BM_Deserialize_ChatMessage` | (not yet measured) | (not yet measured) |
| `BM_Deserialize_ChatMessage_StringView` | (not yet measured) | (not yet measured) |
| `BM_Serialize_ItemList` | (not yet measured) | (not yet measured) |
| `BM_Deserialize_ItemList` | (not yet measured) | (not yet measured) |
| `BM_Serialize_UnmanagedCollection` | (not yet measured) | (not yet measured) |
| `BM_Deserialize_UnmanagedCollection` | (not yet measured) | (not yet measured) |
| `BM_Serialize_GenericCollection` | (not yet measured) | (not yet measured) |
| `BM_Deserialize_GenericCollection` | (not yet measured) | (not yet measured) |
| `BM_PacketFraming_RoundTrip` | (not yet measured) | (not yet measured) |

These cells must be filled in from a real run on documented hardware, using an
optimized build and `--benchmark_repetitions=10 --benchmark_report_aggregates_only=true`
(record the median). Until that happens, **no performance numbers are published for
this library** - not in this file, not in the README, not anywhere else. A benchmark
number without the machine it came from is not a result.

When you do fill the table in, record alongside it:

- CPU model, core count, and base/boost clock; whether frequency scaling was disabled
- RAM type and speed
- OS and version
- Compiler and exact version, plus the effective optimization flags
- The library commit hash
- The Google Benchmark version (v1.9.1 unless `benchmarks/CMakeLists.txt` changed)
- Whether Google Benchmark printed any startup warning (if it did, the run does not count)

---

## Performance tips

Every tip below is something the benchmarks in this suite were built to demonstrate;
each maps to a real API in `include/memorypack/`.

**Reuse one writer instead of allocating per message.** `memorypack::Serialize(value)`
returns a fresh `std::vector` every call. On a hot send path, keep one
`MemoryPackWriter` per connection and rewind it with `Clear()` - the buffer's capacity
survives, so steady state is allocation-free. `Clear()` also resets the error state.

```cpp
memorypack::MemoryPackWriter writer;   // constructed once, kept alive
writer.Reserve(1024);

// per message
writer.Clear();
writer.Write(packet);
Send(writer.GetSpan());
```

**Pre-`Reserve()` when you know the size.** `Reserve()` sizes the writer's buffer up
front and removes the growth reallocations from the first messages. The same applies to
a caller-owned vector passed to `MemoryPackWriter(std::vector<uint8_t>&)` - reserve it
once, `clear()` it per message.

**Prefer `SerializeTo` with a fixed buffer in hot paths.** For packets with a known
upper bound, `memorypack::SerializeTo(std::span<uint8_t>(buffer), value)` writes into a
stack `std::array` and never touches the heap. It returns the byte count, or `0` if the
value did not fit - check the return value, since a fixed buffer cannot grow.

**Use `ReadStringView()` to avoid copying strings out.** It returns a
`std::string_view` borrowed from the input buffer instead of constructing a
`std::string`. That view is only valid while the input buffer is alive and unmodified,
so it fits the "decode, dispatch, and discard inside the receive callback" pattern
exactly - which is precisely what `PacketFrameParser::Feed()` gives you. It returns
`nullopt` for a null string and for a UTF-16 payload (rewinding the reader, so you can
fall back to `ReadString()`).

**Use the in-place `Read*` overloads.** `ReadVector(out)`, `ReadCollection(out)`,
`ReadStringVector(out)`, `ReadString(out)`, `ReadUnmanagedCollection(out)` and
`Deserialize(ptr, size, outValue)` all write into an object you already own. Keep that
object alive across messages and its capacity is reused instead of being reallocated
every time.

**Use `WriteUnmanagedCollection` for arrays of layout-compatible structs.** Mark a
trivially-copyable struct with `MEMORYPACK_UNMANAGED(Type, ExpectedSizeInBytes)` - the
size is checked against `sizeof` at compile time so C# layout drift is caught early -
and then move whole arrays with `WriteUnmanagedCollection` / `ReadUnmanagedCollection`.
That is a single `memcpy` for the entire array, versus a per-element loop that also
pays one header byte per element on the wire. It requires a little-endian host, which
the library enforces with a `static_assert`.

**Do not pay for what the wire format already avoids.** MemoryPack has no varints and
no field tags, so a member's encoded size is fixed and known. If a benchmark shows a
path far above `BM_Memcpy_Baseline`, the cost is in allocation, string construction, or
per-element dispatch - not in the encoding - and one of the tips above usually removes it.
