using MemoryPack;

namespace FormatProbe;

public record FixtureCase(string Name, string TypeName, string Description, byte[] Bytes);

public static class FixtureCases
{
    // Keep in sync with the PackageReference in FormatProbe.csproj.
    public const string MemoryPackVersion = "1.21.4";

    static FixtureCase Case<T>(string name, string description, T value) =>
        new(name, typeof(T).Name, description, MemoryPackSerializer.Serialize(value));

    public static AllPrimitives SampleAllPrimitives() => new()
    {
        BoolValue = true,
        ByteValue = 200,
        SByteValue = -5,
        ShortValue = -1234,
        UShortValue = 54321,
        IntValue = -123456,
        UIntValue = 3000000000u,
        LongValue = -1234567890123L,
        ULongValue = 12345678901234567890UL,
        FloatValue = 3.14159f,
        DoubleValue = 2.718281828459045,
    };

    public static DotNetTypes SampleDotNetTypes() => new()
    {
        GuidValue = new Guid("01020304-0506-0708-090a-0b0c0d0e0f10"),
        DateTimeValue = new DateTime(2026, 8, 30, 12, 34, 56, DateTimeKind.Utc),
        TimeSpanValue = TimeSpan.FromSeconds(90),
        DateTimeOffsetValue = new DateTimeOffset(2026, 8, 30, 12, 34, 56, TimeSpan.FromHours(9)),
        CharValue = 'A',
        DecimalValue = 123.456m,
        HalfValue = (Half)1.5f,
        Int128Value = Int128.MinValue + 7,
        UInt128Value = (UInt128)123456789,
    };

    public static List<FixtureCase> Build()
    {
        var list = new List<FixtureCase>
        {
            // ── Object basics ──────────────────────────────────────────────────
            Case("simple_packet", "Object header + int32 + UTF-8 string",
                new SimplePacket { Id = 42, Name = "ABC" }),

            Case("all_primitives", "Every primitive type, little-endian fixed size",
                SampleAllPrimitives()),

            Case("empty_packet", "Object with zero members", new EmptyPacket()),

            Case("null_object", "Top-level null object (0xFF)", (SimplePacket?)null),

            // ── Strings ────────────────────────────────────────────────────────
            Case("string_packet", "ASCII / Korean / emoji / empty / null strings",
                new StringPacket
                {
                    Ascii = "Hello",
                    Korean = "한글",
                    Emoji = "😀",
                    Empty = "",
                    Null = null,
                }),

            Case("string_top_level", "Top-level string value", "Hello"),
            Case("string_top_level_empty", "Top-level empty string", ""),
            Case("string_top_level_null", "Top-level null string", (string?)null),

            // ── Collections ────────────────────────────────────────────────────
            Case("array_packet", "Collections of every arithmetic element type + strings",
                new ArrayPacket
                {
                    Shorts = [1, -2, 3],
                    Ints = [10, 20, 30],
                    Longs = [-1L, 2L],
                    Bytes = [0, 128, 255],
                    SBytes = [-128, 0, 127],
                    Floats = [1.5f, -2.5f],
                    Doubles = [1.25, -2.5],
                    Strings = ["a", "bb", "ccc"],
                }),

            Case("int_list_top_level", "Top-level List<int>", new List<int> { 10, 20, 30 }),
            Case("int_list_empty", "Top-level empty List<int>", new List<int>()),
            Case("int_list_null", "Top-level null List<int>", (List<int>?)null),

            Case("bool_collection", "List<bool> and bool[] element encoding",
                new BoolCollection { Flags = [true, false, true], FlagArray = [false, true] }),

            Case("nested_list", "List<List<int>> including an empty inner list",
                new NestedList { Rows = [[1, 2], [3], []] }),

            // ── Objects in collections ─────────────────────────────────────────
            Case("inventory", "List<T> where T is a MemoryPackable class",
                new Inventory
                {
                    OwnerId = 7,
                    Items =
                    [
                        new Item { ItemId = 1, ItemName = "Sword", Count = 10 },
                        new Item { ItemId = 2, ItemName = "Shield", Count = 20 },
                        new Item { ItemId = 3, ItemName = "Potion", Count = 30 },
                    ],
                }),

            Case("nested_object", "Object member that is itself a MemoryPackable class",
                new NestedObject { Id = 5, Child = new Item { ItemId = 9, ItemName = "Gem", Count = 3 } }),

            Case("null_members", "null vs empty string, null vs empty list, null child object",
                new NullMembers
                {
                    NullString = null,
                    EmptyString = "",
                    NullList = null,
                    EmptyList = [],
                    NullChild = null,
                }),

            // ── Enums ──────────────────────────────────────────────────────────
            Case("enum_packet", "Enums with ushort / sbyte / int underlying types",
                new EnumPacket { U16 = ColorU16.Green, I8 = ColorI8.Neg, I32 = ColorI32.B }),

            // ── Unmanaged structs ──────────────────────────────────────────────
            Case("vec3_top_level", "Top-level unmanaged struct (no object header)",
                new Vec3 { X = 1.5f, Y = 2.5f, Z = 3.5f }),

            Case("padded_struct_top_level", "Unmanaged struct with natural-alignment padding",
                new PaddedStruct { Tag = 7, Value = 1000 }),

            Case("packed_struct_top_level", "Unmanaged struct declared with Pack = 1",
                new PackedStruct { Tag = 7, Value = 1000 }),

            Case("unmanaged_holder", "Class holding unmanaged struct members",
                new UnmanagedHolder
                {
                    Position = new Vec3 { X = 1.5f, Y = 2.5f, Z = 3.5f },
                    Padded = new PaddedStruct { Tag = 7, Value = 1000 },
                }),

            Case("unmanaged_list", "List<UnmanagedStruct> bulk layout",
                new UnmanagedList
                {
                    Points =
                    [
                        new Vec3 { X = 1f, Y = 2f, Z = 3f },
                        new Vec3 { X = 4f, Y = 5f, Z = 6f },
                    ],
                }),

            Case("managed_struct_holder", "struct containing a reference type keeps an object header",
                new ManagedStructHolder { Value = new ManagedStruct { Id = 4, Label = "lbl" } }),

            Case("numerics_types", "System.Numerics vector/quaternion types",
                new NumericsTypes
                {
                    V2 = new System.Numerics.Vector2(1, 2),
                    V3 = new System.Numerics.Vector3(3, 4, 5),
                    V4 = new System.Numerics.Vector4(6, 7, 8, 9),
                    Quat = new System.Numerics.Quaternion(0.1f, 0.2f, 0.3f, 0.4f),
                }),

            // ── Nullable<T> ────────────────────────────────────────────────────
            Case("nullable_packet", "Nullable<int> / Nullable<float> / Nullable<unmanaged struct>",
                new NullablePacket
                {
                    MaybeInt = 99,
                    NullInt = null,
                    MaybeFloat = 1.25f,
                    MaybeVec = new Vec3 { X = 1f, Y = 2f, Z = 3f },
                    NullVec = null,
                }),

            Case("nullable_int_top_level", "Top-level Nullable<int> with a value", (int?)42),
            Case("nullable_int_top_level_null", "Top-level Nullable<int> that is null", (int?)null),

            // ── Dictionary / set / kvp ─────────────────────────────────────────
            Case("dict_packet", "Dictionary<int,int>, Dictionary<string,int>, Dictionary<int,string>",
                new DictPacket
                {
                    IntMap = new Dictionary<int, int> { [1] = 10, [2] = 20, [3] = 30 },
                    StringMap = new Dictionary<string, int> { ["a"] = 1, ["b"] = 2 },
                    IntToString = new Dictionary<int, string> { [1] = "one", [2] = "two" },
                }),

            Case("dict_object_packet", "Dictionary<int, MemoryPackable class>",
                new DictObjectPacket
                {
                    Items = new Dictionary<int, Item>
                    {
                        [1] = new Item { ItemId = 1, ItemName = "A", Count = 1 },
                    },
                }),

            Case("set_packet", "HashSet<int> and HashSet<string>",
                new SetPacket
                {
                    IntSet = [10, 20, 30],
                    StringSet = ["alpha", "beta"],
                }),

            Case("kvp_packet", "KeyValuePair with unmanaged and managed value types",
                new KvpPacket
                {
                    IntPair = new KeyValuePair<int, int>(1, 2),
                    MixedPair = new KeyValuePair<int, string>(3, "kv"),
                }),

            // ── Tuples ─────────────────────────────────────────────────────────
            Case("value_tuple", "Tuple<> class vs ValueTuple, unmanaged and mixed",
                new TuplePacket
                {
                    RefTuple = Tuple.Create(7, "ref"),
                    UnmanagedValueTuple = (11, 22),
                    MixedValueTuple = (33, "tuple"),
                    TripleUnmanaged = (44, 1.5f, 2.5),
                }),

            Case("value_tuple_top_level", "Top-level unmanaged ValueTuple", (11, 22)),

            // ── .NET types ─────────────────────────────────────────────────────
            Case("dotnet_types", "Guid, DateTime, TimeSpan, DateTimeOffset, char, decimal, Half, Int128",
                SampleDotNetTypes()),

            Case("guid_top_level", "Top-level Guid",
                new Guid("01020304-0506-0708-090a-0b0c0d0e0f10")),

            Case("datetime_top_level", "Top-level DateTime (UTC)",
                new DateTime(2026, 8, 30, 12, 34, 56, DateTimeKind.Utc)),

            // ── Unions ─────────────────────────────────────────────────────────
            Case("union_circle", "Union with a small tag (0)",
                new UnionHolder { Shape = new CircleShape { Radius = 2.5f } }),

            Case("union_rect", "Union with a small tag (1)",
                new UnionHolder { Shape = new RectShape { Width = 3f, Height = 4f } }),

            Case("union_wide", "Union with a wide tag (300 >= 250)",
                new UnionHolder { Shape = new WideShape { Sides = 12 } }),

            Case("union_null", "Union member that is null",
                new UnionHolder { Shape = null }),

            Case("union_top_level", "Top-level union value",
                (IShape)new CircleShape { Radius = 2.5f }),

            // ── Attributes / version tolerance ─────────────────────────────────
            Case("ordered_packet", "MemoryPackOrder reorders members, MemoryPackIgnore drops one",
                new OrderedPacket { First = 1, Second = 2, Ignored = 999 }),

            Case("version_tolerant", "GenerateType.VersionTolerant layout",
                new VersionTolerantPacket { Id = 77, Name = "vt", Score = 0.5f }),

            // ── Special values ─────────────────────────────────────────────────
            Case("special_floats", "NaN / +Inf / -Inf / -0.0 bit patterns",
                new SpecialFloats
                {
                    NanF = float.NaN,
                    PosInfF = float.PositiveInfinity,
                    NegInfF = float.NegativeInfinity,
                    NanD = double.NaN,
                    PosInfD = double.PositiveInfinity,
                    NegZeroD = -0.0,
                }),

            // ── Additional probes ──────────────────────────────────────────────
            Case("version_tolerant_long", "VersionTolerant member length encoding above 127 bytes",
                new VersionTolerantLong { Id = 1, Big = new string('x', 300), Tail = 9 }),

            Case("list_of_nullable_objects", "List<T?> where an element is null",
                new ListOfNullableObjects
                {
                    Items = [new Item { ItemId = 1, ItemName = "A", Count = 1 }, null],
                }),

            Case("many_members", "Object with 10 members", new ManyMembers
            {
                M00 = 0, M01 = 1, M02 = 2, M03 = 3, M04 = 4,
                M05 = 5, M06 = 6, M07 = 7, M08 = 8, M09 = 9,
            }),

            Case("nullable_managed_holder", "Nullable<T> where T is a managed struct",
                new NullableManagedHolder
                {
                    Value = new ManagedNullableTarget { A = 3, B = "n" },
                }),

            Case("nullable_managed_holder_null", "Nullable<managed struct> that is null",
                new NullableManagedHolder { Value = null }),

            Case("vt_len_120", "VersionTolerant member length 120 (single byte)",
                new VersionTolerantLong { Id = 1, Big = new string('y', 112), Tail = 9 }),
            Case("vt_len_130", "VersionTolerant member length 130 (just above 127)",
                new VersionTolerantLong { Id = 1, Big = new string('y', 122), Tail = 9 }),
            Case("vt_len_70000", "VersionTolerant member length above 65535",
                new VersionTolerantLong { Id = 1, Big = new string('y', 69992), Tail = 9 }),
        };

        return list;
    }
}
