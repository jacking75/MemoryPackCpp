using MemoryPack;

namespace Cases.Collections;

[MemoryPackable]
public partial class CollectionPacket
{
    public Dictionary<int, string>? Scores { get; set; }
    public Dictionary<string, int>? Lookup { get; set; }
    public HashSet<int>? Tags { get; set; }
    public KeyValuePair<int, string> Head { get; set; }
    public List<List<int>>? Rows { get; set; }
    public int? MaybeInt { get; set; }
    public float? MaybeFloat { get; set; }
}

[MemoryPackable]
public partial class DotNetPacket
{
    public Guid Id { get; set; }
    public DateTime CreatedAt { get; set; }
    public TimeSpan Duration { get; set; }
    public DateTimeOffset Offset { get; set; }
    public decimal Price { get; set; }
    public Half Ratio { get; set; }
    public Int128 Big { get; set; }
    public UInt128 UBig { get; set; }
    public char Initial { get; set; }
}
