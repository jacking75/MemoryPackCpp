using MemoryPack;
using System.Runtime.InteropServices;

namespace FormatProbe;

// ── Basic object ───────────────────────────────────────────────────────────────

[MemoryPackable]
public partial class SimplePacket
{
    public int Id { get; set; }
    public string? Name { get; set; }
}

[MemoryPackable]
public partial class AllPrimitives
{
    public bool BoolValue { get; set; }
    public byte ByteValue { get; set; }
    public sbyte SByteValue { get; set; }
    public short ShortValue { get; set; }
    public ushort UShortValue { get; set; }
    public int IntValue { get; set; }
    public uint UIntValue { get; set; }
    public long LongValue { get; set; }
    public ulong ULongValue { get; set; }
    public float FloatValue { get; set; }
    public double DoubleValue { get; set; }
}

[MemoryPackable]
public partial class EmptyPacket
{
}

// ── Nested object / collections of objects ─────────────────────────────────────

[MemoryPackable]
public partial class Item
{
    public int ItemId { get; set; }
    public string? ItemName { get; set; }
    public int Count { get; set; }
}

[MemoryPackable]
public partial class Inventory
{
    public int OwnerId { get; set; }
    public List<Item>? Items { get; set; }
}

[MemoryPackable]
public partial class NestedObject
{
    public int Id { get; set; }
    public Item? Child { get; set; }
}

[MemoryPackable]
public partial class NestedList
{
    public List<List<int>>? Rows { get; set; }
}

// ── bool collection ────────────────────────────────────────────────────────────

[MemoryPackable]
public partial class BoolCollection
{
    public List<bool>? Flags { get; set; }
    public bool[]? FlagArray { get; set; }
}

// ── Enum ───────────────────────────────────────────────────────────────────────

public enum ColorU16 : ushort { Red = 1, Green = 2, Blue = 3 }
public enum ColorI8 : sbyte { Neg = -2, Zero = 0, Pos = 5 }
public enum ColorI32 { A = 0, B = 1000000 }

[MemoryPackable]
public partial class EnumPacket
{
    public ColorU16 U16 { get; set; }
    public ColorI8 I8 { get; set; }
    public ColorI32 I32 { get; set; }
}

// ── Unmanaged structs ──────────────────────────────────────────────────────────

[MemoryPackable]
public partial struct Vec3
{
    public float X { get; set; }
    public float Y { get; set; }
    public float Z { get; set; }
}

// Struct with natural-alignment padding: byte + int => 8 bytes in .NET sequential layout.
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

[MemoryPackable]
public partial class UnmanagedHolder
{
    public Vec3 Position { get; set; }
    public PaddedStruct Padded { get; set; }
}

[MemoryPackable]
public partial class UnmanagedList
{
    public List<Vec3>? Points { get; set; }
}

// Struct that contains a reference type => NOT unmanaged, gets an object header.
[MemoryPackable]
public partial struct ManagedStruct
{
    public int Id { get; set; }
    public string? Label { get; set; }
}

[MemoryPackable]
public partial class ManagedStructHolder
{
    public ManagedStruct Value { get; set; }
}

// ── Nullable<T> ────────────────────────────────────────────────────────────────

[MemoryPackable]
public partial class NullablePacket
{
    public int? MaybeInt { get; set; }
    public int? NullInt { get; set; }
    public float? MaybeFloat { get; set; }
    public Vec3? MaybeVec { get; set; }
    public Vec3? NullVec { get; set; }
}

// ── Nullable reference members ─────────────────────────────────────────────────

[MemoryPackable]
public partial class NullMembers
{
    public string? NullString { get; set; }
    public string? EmptyString { get; set; }
    public List<int>? NullList { get; set; }
    public List<int>? EmptyList { get; set; }
    public Item? NullChild { get; set; }
}

// ── Dictionary / HashSet / KeyValuePair ────────────────────────────────────────

[MemoryPackable]
public partial class DictPacket
{
    public Dictionary<int, int>? IntMap { get; set; }
    public Dictionary<string, int>? StringMap { get; set; }
    public Dictionary<int, string>? IntToString { get; set; }
}

[MemoryPackable]
public partial class DictObjectPacket
{
    public Dictionary<int, Item>? Items { get; set; }
}

[MemoryPackable]
public partial class SetPacket
{
    public HashSet<int>? IntSet { get; set; }
    public HashSet<string>? StringSet { get; set; }
}

[MemoryPackable]
public partial class KvpPacket
{
    public KeyValuePair<int, int> IntPair { get; set; }
    public KeyValuePair<int, string> MixedPair { get; set; }
}

// ── Tuple / ValueTuple ─────────────────────────────────────────────────────────

[MemoryPackable]
public partial class TuplePacket
{
    public Tuple<int, string>? RefTuple { get; set; }
    public (int, int) UnmanagedValueTuple { get; set; }
    public (int, string) MixedValueTuple { get; set; }
    public (int, float, double) TripleUnmanaged { get; set; }
}

// ── .NET types ─────────────────────────────────────────────────────────────────

[MemoryPackable]
public partial class DotNetTypes
{
    public Guid GuidValue { get; set; }
    public DateTime DateTimeValue { get; set; }
    public TimeSpan TimeSpanValue { get; set; }
    public DateTimeOffset DateTimeOffsetValue { get; set; }
    public char CharValue { get; set; }
    public decimal DecimalValue { get; set; }
    public Half HalfValue { get; set; }
    public Int128 Int128Value { get; set; }
    public UInt128 UInt128Value { get; set; }
}

[MemoryPackable]
public partial class NumericsTypes
{
    public System.Numerics.Vector2 V2 { get; set; }
    public System.Numerics.Vector3 V3 { get; set; }
    public System.Numerics.Vector4 V4 { get; set; }
    public System.Numerics.Quaternion Quat { get; set; }
}

// ── Union ──────────────────────────────────────────────────────────────────────

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

// ── Version tolerant ───────────────────────────────────────────────────────────

[MemoryPackable(GenerateType.VersionTolerant)]
public partial class VersionTolerantPacket
{
    [MemoryPackOrder(0)] public int Id { get; set; }
    [MemoryPackOrder(1)] public string? Name { get; set; }
    [MemoryPackOrder(2)] public float Score { get; set; }
}

// ── Order / Ignore attributes ──────────────────────────────────────────────────

[MemoryPackable]
public partial class OrderedPacket
{
    [MemoryPackOrder(1)] public int Second { get; set; }
    [MemoryPackOrder(0)] public int First { get; set; }
    [MemoryPackIgnore] public int Ignored { get; set; }
}

// ── Strings ────────────────────────────────────────────────────────────────────

[MemoryPackable]
public partial class StringPacket
{
    public string? Ascii { get; set; }
    public string? Korean { get; set; }
    public string? Emoji { get; set; }
    public string? Empty { get; set; }
    public string? Null { get; set; }
}

// ── Fixed-size collection interop (mirrors the CppClient samples) ──────────────

[MemoryPackable]
public partial class ArrayPacket
{
    public List<short>? Shorts { get; set; }
    public List<int>? Ints { get; set; }
    public List<long>? Longs { get; set; }
    public List<byte>? Bytes { get; set; }
    public List<sbyte>? SBytes { get; set; }
    public List<float>? Floats { get; set; }
    public List<double>? Doubles { get; set; }
    public List<string>? Strings { get; set; }
}

// ── Special float values ───────────────────────────────────────────────────────

[MemoryPackable]
public partial class SpecialFloats
{
    public float NanF { get; set; }
    public float PosInfF { get; set; }
    public float NegInfF { get; set; }
    public double NanD { get; set; }
    public double PosInfD { get; set; }
    public double NegZeroD { get; set; }
}

// ── Additional probes (length encodings, edge cases) ──────────────────────────

[MemoryPackable(GenerateType.VersionTolerant)]
public partial class VersionTolerantLong
{
    [MemoryPackOrder(0)] public int Id { get; set; }
    [MemoryPackOrder(1)] public string? Big { get; set; }
    [MemoryPackOrder(2)] public int Tail { get; set; }
}

[MemoryPackable]
public partial class ListOfNullableObjects
{
    public List<Item?>? Items { get; set; }
}

[MemoryPackable]
public partial class ManyMembers
{
    public int M00 { get; set; }
    public int M01 { get; set; }
    public int M02 { get; set; }
    public int M03 { get; set; }
    public int M04 { get; set; }
    public int M05 { get; set; }
    public int M06 { get; set; }
    public int M07 { get; set; }
    public int M08 { get; set; }
    public int M09 { get; set; }
}

[MemoryPackable]
public partial struct ManagedNullableTarget
{
    public int A { get; set; }
    public string? B { get; set; }
}

[MemoryPackable]
public partial class NullableManagedHolder
{
    public ManagedNullableTarget? Value { get; set; }
}
