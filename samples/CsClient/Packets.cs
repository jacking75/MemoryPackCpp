using System.Numerics;
using MemoryPack;

namespace CsClient;

// ============================================================================
// C# mirror of samples/CppServer/packets.hpp.
//
// MemoryPack serializes by member POSITION and never writes member names, so
// the members below must appear in the SAME ORDER as the C++ struct fields.
// The names may differ (C# PascalCase vs C++ camelCase); the order may not.
//
// Type mapping used here:
//
//   C++                          C#
//   ---------------------------  --------------------------------------------
//   int32_t                      int
//   int64_t                      long
//   std::string                  string?          (length-prefixed UTF-8)
//   std::vector<int32_t>         List<int>?       ([4B count][count * 4B])
//   std::vector<Entity>          List<Entity>?    ([4B count][objects...])
//   memorypack::Vector3          System.Numerics.Vector3
//
// Vector3 is an UNMANAGED struct on both sides (three floats, no references),
// so MemoryPack copies its 12 bytes verbatim with no object header. That is why
// the C++ header can use memorypack::Vector3 and this file can use the stock
// System.Numerics type without any custom formatter on either side.
// ============================================================================

/// Must match the PacketId enum in packets.hpp.
public enum PacketId : ushort
{
    EchoRequest   = 201,
    EchoResponse  = 202,
    SumRequest    = 203,
    SumResponse   = 204,
    SpawnRequest  = 205,
    SpawnResponse = 206,
}

[MemoryPackable]
public partial class EchoRequest
{
    public int Seq { get; set; }
    public string? Text { get; set; }
}

[MemoryPackable]
public partial class EchoResponse
{
    public int Seq { get; set; }
    public string? Text { get; set; }
    public long ServerTimeTicks { get; set; }
}

[MemoryPackable]
public partial class SumRequest
{
    public List<int>? Values { get; set; }
}

[MemoryPackable]
public partial class SumResponse
{
    public long Total { get; set; }
}

[MemoryPackable]
public partial class Entity
{
    public int Id { get; set; }
    public Vector3 Position { get; set; }
    public string? Name { get; set; }
}

[MemoryPackable]
public partial class SpawnRequest
{
    public int Count { get; set; }
}

[MemoryPackable]
public partial class SpawnResponse
{
    public List<Entity>? Entities { get; set; }
}
