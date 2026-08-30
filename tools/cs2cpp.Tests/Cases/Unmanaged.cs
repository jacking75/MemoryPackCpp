using System.Runtime.InteropServices;
using MemoryPack;

namespace Cases.Unmanaged;

[MemoryPackable]
public partial struct Vec3
{
    public float X { get; set; }
    public float Y { get; set; }
    public float Z { get; set; }
}

// byte 다음에 3바이트 패딩이 들어가 8바이트가 된다.
[MemoryPackable]
public partial struct PaddedStruct
{
    public byte Tag { get; set; }
    public int Value { get; set; }
}

[MemoryPackable]
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public partial struct PackedStruct
{
    public byte Tag { get; set; }
    public int Value { get; set; }
}

// 문자열이 있으면 unmanaged 가 아니다 — 오브젝트 헤더가 붙는다.
[MemoryPackable]
public partial struct ManagedStruct
{
    public int Id { get; set; }
    public string? Label { get; set; }
}

[MemoryPackable]
public partial class UnmanagedHolder
{
    public Vec3 Position { get; set; }
    public PaddedStruct Padded { get; set; }
    public PackedStruct Packed { get; set; }
    public Vec3? MaybeVec { get; set; }
    public List<Vec3>? Points { get; set; }
    public ManagedStruct Managed { get; set; }
}
