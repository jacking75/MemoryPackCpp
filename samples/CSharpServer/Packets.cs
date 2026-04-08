using MemoryPack;

namespace CSharpServer;

/// <summary>
/// Packet IDs — must match C++ PacketId enum.
/// </summary>
public enum PacketId : ushort
{
    LoginRequest  = 1,
    LoginResponse = 2,
    PlayerState   = 3,
    ScoreUpdate   = 4,
    ChatMessage   = 5,
    InventoryData  = 6,
    BufferData        = 7,
    IntArrayPacket    = 8,
    SkillSlotData     = 9,
    MapTileRow        = 10,
    MixedFormatPacket = 11,
}

// Member order MUST match C++ struct declaration order.

[MemoryPackable]
public partial class LoginRequest
{
    public string? Username { get; set; }
    public int Level { get; set; }
}

[MemoryPackable]
public partial class LoginResponse
{
    public bool Success { get; set; }
    public int PlayerId { get; set; }
    public string? Message { get; set; }
}

[MemoryPackable]
public partial class PlayerState
{
    public int PlayerId { get; set; }
    public float PosX { get; set; }
    public float PosY { get; set; }
    public float PosZ { get; set; }
    public string? Name { get; set; }
}

[MemoryPackable]
public partial class ScoreUpdate
{
    public int PlayerId { get; set; }
    public List<int>? Scores { get; set; }
    public double TotalScore { get; set; }
}

[MemoryPackable]
public partial class ChatMessage
{
    public int SenderId { get; set; }
    public string? Message { get; set; }
    public long Timestamp { get; set; }
}

[MemoryPackable]
public partial class InventoryData
{
    public int PlayerId { get; set; }
    public List<string>? ItemNames { get; set; }
    public List<int>? ItemCounts { get; set; }
}

[MemoryPackable]
public partial class BufferData
{
    public byte Tag { get; set; }
    public sbyte Grade { get; set; }
    public List<byte>? RawData { get; set; }
    public List<sbyte>? CharCodes { get; set; }
}

[MemoryPackable]
public partial class IntArrayPacket
{
    public int Id { get; set; }
    public List<short>? ShortArray { get; set; }
    public List<int>? IntArray { get; set; }
    public List<long>? LongArray { get; set; }
}

// C++ sends C fixed arrays as collections — C# sees them as List<T>
[MemoryPackable]
public partial class SkillSlotData
{
    public int PlayerId { get; set; }
    public List<int>? SkillIds { get; set; }
    public List<float>? Cooldowns { get; set; }
}

[MemoryPackable]
public partial class MapTileRow
{
    public int RowIndex { get; set; }
    public List<byte>? Tiles { get; set; }
    public List<short>? Heights { get; set; }
}

[MemoryPackable]
public partial class MixedFormatPacket
{
    public int Id { get; set; }
    public List<int>? DynamicScores { get; set; }    // from C++ vector
    public List<int>? FixedBonuses { get; set; }     // from C++ int32_t[]
    public List<sbyte>? TagBytes { get; set; }       // from C++ int8_t[] (char)
    public double Multiplier { get; set; }
}
