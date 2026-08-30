# Unreal Engine Integration

Unreal builds differ from a normal C++ project in three ways that matter here:
exceptions are off, the string type is UTF-16, and the container library is
`TArray`/`TMap` rather than `std::`. All three are workable.

---

## 1. Adding the library

MemoryPackCpp is header-only, so a module just needs the include path.

Copy `include/memorypack/` into your plugin or module (for example
`Source/MyModule/ThirdParty/memorypack/`), then in your `*.Build.cs`:

```csharp
public class MyModule : ModuleRules
{
    public MyModule(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "ThirdParty"));

        // MemoryPackCpp requires C++23.
        CppStandard = CppStandardVersion.Cpp20;   // see the note below
        bEnableExceptions = false;                // the library supports this
    }
}
```

> **C++ standard.** Unreal 5.4+ exposes `CppStandardVersion.Cpp20`; a
> `Latest`/`Cpp23` option appears in newer versions and in `EngineCppStandard`.
> The library needs C++23 (`std::span`, concepts, `std::endian`). If your engine
> version cannot be pushed to C++23, compile the serialization code into a small
> separate static library built with your own toolchain settings and expose a
> plain-C++ interface to the Unreal module.

Alternatively drop in the single-header build:

```bash
python tools/amalgamate.py --include-packet -o dist/memorypack.hpp
```

---

## 2. Exceptions

Unreal sets `bEnableExceptions = false` by default. The library detects that and
switches to error-state reporting automatically — you do not need to define
anything, though you may be explicit:

```cpp
#define MEMORYPACK_NO_EXCEPTIONS
#include "memorypack/memorypack.hpp"
```

Then check the state instead of catching:

```cpp
memorypack::MemoryPackReader Reader(Body);
FLoginResponse Response;
Reader.Read(Response);

if (Reader.Failed())
{
    UE_LOG(LogNet, Warning, TEXT("Bad packet: %hs at offset %llu"),
           memorypack::ToString(Reader.Error()),
           static_cast<uint64>(Reader.Position()));
    return;
}
```

Or use `std::expected` when your standard library provides it:

```cpp
if (auto Result = memorypack::TryDeserialize<FLoginResponse>(Body))
{
    Handle(*Result);
}
```

See [error-handling.md](error-handling.md).

---

## 3. Strings

`FString` is UTF-16 internally. MemoryPack's wire format accepts both UTF-8 and
UTF-16, and C# reads either, so you have two options.

### Option A — convert to UTF-8 (recommended)

Smaller on the wire for mostly-ASCII text, and it is the form the rest of this
library writes by default:

```cpp
static std::string ToUtf8(const FString& In)
{
    FTCHARToUTF8 Converted(*In);
    return std::string(Converted.Get(), Converted.Length());
}

static FString FromUtf8(const std::string& In)
{
    return FString(UTF8_TO_TCHAR(In.c_str()));
}
```

Then keep `std::string` in your packet structs and convert at the UI/gameplay
boundary.

### Option B — write UTF-16 directly

Skips the conversion entirely when your text is mostly non-ASCII:

```cpp
Writer.WriteStringUtf16(std::u16string_view(
    reinterpret_cast<const char16_t*>(*Str), Str.Len()));
```

This is safe on Windows, where `TCHAR` is `wchar_t` and UTF-16. On platforms
where Unreal uses UTF-32 `TCHAR`, convert instead.

---

## 4. Containers

The library speaks `std::` containers. Two approaches:

**Convert at the boundary** — simplest, and fine for packet-sized data:

```cpp
std::vector<int32_t> ToStd(const TArray<int32>& In)
{
    return std::vector<int32_t>(In.GetData(), In.GetData() + In.Num());
}

TArray<int32> ToUnreal(const std::vector<int32_t>& In)
{
    TArray<int32> Out;
    Out.Append(In.data(), static_cast<int32>(In.size()));
    return Out;
}
```

**Or write the members yourself** — no conversion, no temporary:

```cpp
namespace memorypack {
template<>
struct IMemoryPackable<FPlayerState>
{
    static void Serialize(MemoryPackWriter& W, const FPlayerState* V)
    {
        if (!V) { W.WriteNullObjectHeader(); return; }
        W.WriteObjectHeader(2);
        W.WriteInt32(V->PlayerId);
        W.WriteArray(V->Scores.GetData(), V->Scores.Num());   // TArray -> collection
    }
    static void Deserialize(MemoryPackReader& R, FPlayerState& V)
    {
        const auto Header = R.ReadObjectHeader();
        if (Header.isNull) return;
        if (Header.count >= 1) V.PlayerId = R.ReadInt32();
        if (Header.count >= 2)
        {
            const int32_t Count = R.ReadCollectionLength(sizeof(int32));
            if (Count > 0)
            {
                V.Scores.SetNumUninitialized(Count);
                R.ReadArray(V.Scores.GetData(), Count);
            }
        }
    }
};
}
```

`WriteArray(const T*, int32_t)` and `ReadArray(T*, int32_t)` work directly on
`TArray::GetData()`, so this path has no copy at all for arithmetic element types.

---

## 5. Vector types

`FVector` is three `double`s in UE5, while C# `System.Numerics.Vector3` is three
`float`s. Do not map them directly. Use `memorypack::Vector3` (three floats) on
the wire and convert:

```cpp
memorypack::Vector3 ToWire(const FVector& V)
{
    return { static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z) };
}
```

If you use `FVector3f` (single precision), the layout already matches and you can
declare it directly:

```cpp
MEMORYPACK_UNMANAGED(FVector3f, 12)
```

Verify with the size assertion — that is exactly what it is for.

---

## 6. Networking

Unreal's replication is separate from this library. Use MemoryPackCpp where you
own the socket: a dedicated backend server, a matchmaking service, a custom TCP
channel. [`memorypack/packet.hpp`](../include/memorypack/packet.hpp) provides
`[2B packetId][4B bodyLength]` framing and a stream reassembler:

```cpp
memorypack::PacketFrameParser Parser(64 * 1024);

void OnBytesReceived(const uint8* Data, int32 Num)
{
    const bool bOk = Parser.Feed(
        std::span<const uint8_t>(Data, static_cast<size_t>(Num)),
        [this](uint16_t Id, std::span<const uint8_t> Body) { Dispatch(Id, Body); });

    if (!bOk)
    {
        UE_LOG(LogNet, Warning, TEXT("Malformed frame; closing connection"));
        Close();
    }
}
```

Always pass a `ReaderOptions` with limits appropriate to your protocol when the
data comes from a client — see [security.md](security.md).

---

## 7. Checklist

- [ ] Include path added in `*.Build.cs`; C++23 available (or the serialization
      code isolated in its own library).
- [ ] `bEnableExceptions = false` and the error-state or `std::expected` API used.
- [ ] `FString` conversion helpers in one place, not scattered.
- [ ] `FVector` vs `Vector3` precision decided deliberately.
- [ ] `MEMORYPACK_UNMANAGED` size assertions on every struct you copy raw.
- [ ] `ReaderOptions` limits set for client-facing data.
- [ ] The C++ packet definitions generated from the C# ones with
      [`cs2cpp`](../tools/cs2cpp), so they cannot drift.
