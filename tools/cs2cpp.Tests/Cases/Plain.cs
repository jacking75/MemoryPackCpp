using MemoryPack;

namespace Cases.Plain;

[MemoryPackable]
public partial class LoginRequest
{
    public string? Username { get; set; }
    public int Level { get; set; }
    public bool Remember { get; set; }
    public double Score { get; set; }
}

// 공개 필드 + required + init 접근자
[MemoryPackable]
public partial class FieldPacket
{
    public int Id;
    public required string Name { get; init; }
    public long Ticks;
    internal int NotSerialized { get; set; }
    public int Computed => Id * 2;
}

// positional record
[MemoryPackable]
public partial record RecordPacket(int Id, string Name);
