using MemoryPack;

namespace Cases.VersionTolerant;

[MemoryPackable(GenerateType.VersionTolerant)]
public partial class VersionTolerantPacket
{
    public int Id { get; set; }
    public string? Name { get; set; }
    public float Score { get; set; }
}
