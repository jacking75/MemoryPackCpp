using MemoryPack;

namespace Cases.Union;

[MemoryPackable]
[MemoryPackUnion(0, typeof(CircleShape))]
[MemoryPackUnion(1, typeof(RectShape))]
[MemoryPackUnion(300, typeof(WideShape))]
public partial interface IShape
{
}

[MemoryPackable]
public partial class CircleShape : IShape
{
    public float Radius { get; set; }
}

[MemoryPackable]
public partial class RectShape : IShape
{
    public float Width { get; set; }
    public float Height { get; set; }
}

[MemoryPackable]
public partial class WideShape : IShape
{
    public int Sides { get; set; }
}

[MemoryPackable]
public partial class UnionHolder
{
    public IShape? Shape { get; set; }
}
