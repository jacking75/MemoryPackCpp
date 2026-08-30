# Defining Your Own Types

There are two ways to make a C++ type serializable. Use the macro unless you need
something it cannot express.

---

## 1. `MEMORYPACK_DEFINE` (recommended)

```cpp
#include "memorypack/memorypack.hpp"

struct PlayerState {
    int32_t     id;
    float       x, y, z;
    std::string name;
};

MEMORYPACK_DEFINE(PlayerState, id, x, y, z, name)
```

That is the whole definition. The macro generates a
`memorypack::IMemoryPackable<PlayerState>` specialization that writes the object
header, writes each member in the listed order, and reads them back with version
tolerance built in.

Rules:

- **Invoke it at global scope**, outside any namespace — it opens
  `namespace memorypack` internally. A type inside a namespace is fine, just
  qualify it: `MEMORYPACK_DEFINE(game::PlayerState, id, name)`.
- Put it **after** the struct definition, and after any type its members need.
- The member list order **is** the wire order, and it must match the C# class.
- Up to 32 members per macro invocation (the wire format itself allows 249; write
  the specialization by hand if you genuinely need more).
- For a type with no serialized members, use `MEMORYPACK_DEFINE_EMPTY(T)`.

### Version tolerance

The generated reader checks the incoming member count before each member:

```cpp
// Sender (older) wrote 2 members; this reader knows 3.
if (header.count > 0) r.Read(v.id);      // present
if (header.count > 1) r.Read(v.name);    // present
if (header.count > 2) r.Read(v.score);   // absent -> keeps its default
```

So you may **append** members freely in either direction. You may **not** reorder
or remove them — MemoryPack matches by position, not by name. If you need to
remove a member, either keep a placeholder or switch that type to the
[VersionTolerant layout](#versiontolerant-objects).

Give every member a default initializer so a short payload leaves it in a known
state:

```cpp
struct PlayerState {
    int32_t     id = 0;
    float       x = 0.f, y = 0.f, z = 0.f;
    std::string name;
};
```

---

## 2. A hand-written `IMemoryPackable<T>`

Specialize it directly when you need custom logic — fixed-size arrays, an
unmanaged bulk path, a union, or a member whose encoding the macro cannot infer:

```cpp
namespace memorypack {
template<>
struct IMemoryPackable<SkillSlots> {
    static void Serialize(MemoryPackWriter& w, const SkillSlots* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(2);                       // member count
        w.WriteInt32(v->playerId);
        w.WriteArray(v->skillIds, v->skillCount);     // C array -> collection
    }
    static void Deserialize(MemoryPackReader& r, SkillSlots& v) {
        const auto header = r.ReadObjectHeader();
        if (header.isNull) return;
        if (header.count >= 1) v.playerId   = r.ReadInt32();
        if (header.count >= 2) v.skillCount = r.ReadArray(v.skillIds, SkillSlots::MAX_SKILLS);
    }
};
} // namespace memorypack
```

Both forms coexist; `Serialize`/`Deserialize` and `writer.Write` find either one.

A missing specialization is a **compile error with a readable message**, not a
mysterious linker error:

```
memorypack: no serializer for this type. Specialize memorypack::IMemoryPackable<T>
(or use MEMORYPACK_DEFINE) for your own types - see docs/serialization.md.
```

---

## Reading and writing

```cpp
// Whole value at once
std::vector<uint8_t> bytes = memorypack::Serialize(value);
T value = memorypack::Deserialize<T>(bytes);
memorypack::Deserialize(ptr, size, existingValue);      // no allocation for the result

// Into buffers you own
memorypack::Serialize(value, myVector);                  // appends
size_t n = memorypack::SerializeTo(std::span<uint8_t>(myArray), value);

// Strict mode: also require the whole input to be consumed
T value = memorypack::DeserializeExact<T>(bytes);
```

`DeserializeExact` is worth using during development: leftover bytes almost always
mean the C# and C++ member lists have drifted apart, and it turns that into an
immediate error instead of silently wrong values.

Inside a `Serialize`/`Deserialize` body, `writer.Write(x)` and `reader.Read(x)`
dispatch on the type for you. The explicit `WriteInt32` / `ReadString` family is
still there when you want to be unambiguous.

---

## Null handling

C# spells null four different ways, and picking the wrong C++ type produces bytes
the other side cannot read. The library derives the right one from the type:

```cpp
struct Packet {
    std::optional<std::string>          nickname;  // C# string?      -> FF FF FF FF
    std::optional<std::vector<int32_t>> scores;    // C# List<int>?   -> FF FF FF FF
    std::optional<Item>                 equipped;  // C# Item?        -> FF
    std::optional<int32_t>              level;     // C# int?         -> Nullable<int> blob
};
MEMORYPACK_DEFINE(Packet, nickname, scores, equipped, level)
```

The one case the type cannot express is C# `Nullable<T>` where `T` is a *managed*
struct — it carries an extra marker byte. Write that member explicitly:

```cpp
w.WriteNullableObject(v->value);                       // [1B 1][value] or [1B 255]
v.value = r.ReadNullableObject<ManagedTarget>();
```

`std::unique_ptr<T>` and `std::shared_ptr<T>` behave like nullable references.

See [wire-format.md](wire-format.md#nullablet) for the exact bytes of each.

---

## Unmanaged structs

A C# `struct` with no reference-type fields is copied verbatim, with no object
header and **with its padding**. Mirror the layout and declare it:

```cpp
struct Vec3 { float x = 0, y = 0, z = 0; };
MEMORYPACK_UNMANAGED(memorypack::Vector3, 12)   // asserts sizeof == 12
```

The size argument is the point: if someone adds a field or changes a type, the
`static_assert` fires at compile time rather than producing bytes C# misreads.

Three things to watch:

- The C++ struct must be trivially copyable and standard layout.
- .NET uses **natural alignment**. `struct { byte Tag; int Value; }` is 8 bytes
  (1 byte, 3 padding, 4 bytes), not 5 — unless the C# side declares
  `[StructLayout(LayoutKind.Sequential, Pack = 1)]`, in which case use
  `#pragma pack(push, 1)`.
- **Always value-initialize** such a struct: `Vec3 v{};`, not `Vec3 v;`. The
  padding is copied to the wire as-is, and only value-initialization zeroes it.
  Default *member* initializers are not enough - they initialize the members,
  not the padding. See [security.md](security.md#unmanaged-struct-padding).

For an array of them, the bulk path skips the per-element loop entirely:

```cpp
w.WriteUnmanagedCollection(std::span<const Vec3>(points));
r.ReadUnmanagedCollection(points);
```

---

## Unions

Map a C# `[MemoryPackUnion]` hierarchy to `std::variant`:

```cpp
struct CircleShape { float radius = 0.f; };
struct RectShape   { float width = 0.f, height = 0.f; };

MEMORYPACK_DEFINE(CircleShape, radius)
MEMORYPACK_DEFINE(RectShape, width, height)
MEMORYPACK_UNION_TAG(CircleShape, 0)      // must match [MemoryPackUnion(0, ...)]
MEMORYPACK_UNION_TAG(RectShape,   1)

using Shape = std::variant<CircleShape, RectShape>;
```

A C# union member is a reference, so it can be null — model that as
`std::optional<Shape>`. A bare `std::variant` always holds something and cannot
represent null.

---

## VersionTolerant objects

`[MemoryPackable(GenerateType.VersionTolerant)]` prefixes every member with its
byte length, which lets a reader skip members it does not know. That makes it safe
to **remove** a member, not just append one — at the cost of one to five extra
bytes per member.

```cpp
namespace memorypack {
template<>
struct IMemoryPackable<Profile> {
    static void Serialize(MemoryPackWriter& w, const Profile* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        VersionTolerantWriter vt(w);       // flushes in its destructor
        vt.WriteMember(v->id);
        vt.WriteMember(v->name);
        vt.WriteMember(v->score);
    }
    static void Deserialize(MemoryPackReader& r, Profile& v) {
        VersionTolerantReader vt(r);
        if (vt.IsNull()) return;
        vt.ReadMember(v.id);
        vt.ReadMember(v.name);
        vt.ReadMember(v.score);
        vt.Finish();                       // skips any members we do not know
    }
};
} // namespace memorypack
```

Always call `Finish()`: it positions the reader after the object even when the
sender wrote more members than this build understands.

Use it for persisted data and long-lived protocols. For hot packets the default
layout is smaller and faster.

---

## Keeping C# and C++ in sync

Three levels of defence, cheapest first:

1. **Generate the C++ from the C#.** [`tools/cs2cpp`](../tools/cs2cpp) reads your
   `[MemoryPackable]` definitions and emits the matching header, so the member
   list cannot drift. Run it in CI with `--check` to fail the build on a mismatch.
2. **Use `DeserializeExact`** in debug builds so leftover bytes are reported.
3. **Compare a schema hash at connect time.** `cs2cpp` can emit one for both
   sides, turning a protocol mismatch into a clear handshake failure instead of
   corrupted gameplay data.
