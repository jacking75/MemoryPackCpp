using Xunit;

namespace Cs2Cpp.Tests;

public sealed class ParserTests
{
    [Fact]
    public void FileScopedNamespaceIsDetected()
    {
        var schema = TestHelper.ParseCase("Plain.cs");
        Assert.Equal("Cases.Plain", schema.CsNamespace);
    }

    [Fact]
    public void BlockNestedNamespaceIsJoined()
    {
        var schema = TestHelper.ParseCase("NestedNamespace.cs");
        Assert.Equal("Outer.Inner", schema.CsNamespace);
        Assert.Single(schema.Types);
        Assert.Equal("Ping", schema.Types[0].Name);
    }

    [Fact]
    public void EnumValuesAutoIncrementAndAllowNegatives()
    {
        var schema = TestHelper.ParseCase("Enums.cs");

        var color = schema.Enums.Single(e => e.Name == "Color");
        Assert.Equal("uint8_t", color.CppBaseType);
        Assert.Equal([0L, 1L, 10L, 11L], color.Members.Select(m => m.Value));

        var signed = schema.Enums.Single(e => e.Name == "Signed");
        Assert.Equal("int16_t", signed.CppBaseType);
        Assert.Equal([-2L, 0L, 5L], signed.Members.Select(m => m.Value));

        var wide = schema.Enums.Single(e => e.Name == "Wide");
        Assert.Equal("uint32_t", wide.CppBaseType);
        Assert.Equal([1L, 0x10000L], wide.Members.Select(m => m.Value));
    }

    [Fact]
    public void MemoryPackOrderSortsAndIgnoreDrops()
    {
        var schema = TestHelper.ParseCase("OrderIgnore.cs");
        var t = schema.Types.Single();
        Assert.Equal(["First", "Second", "Trailing"], t.Members.Select(m => m.CsName));
    }

    [Fact]
    public void PublicFieldsAndRecordsAreSerialized()
    {
        var schema = TestHelper.ParseCase("Plain.cs");

        var fields = schema.Types.Single(t => t.Name == "FieldPacket");
        // internal 프로퍼티와 식 본문 프로퍼티는 제외된다.
        Assert.Equal(["Id", "Name", "Ticks"], fields.Members.Select(m => m.CsName));

        var record = schema.Types.Single(t => t.Name == "RecordPacket");
        Assert.Equal(["Id", "Name"], record.Members.Select(m => m.CsName));
    }

    [Fact]
    public void UnmanagedStructSizesFollowSequentialLayout()
    {
        var schema = TestHelper.ParseCase("Unmanaged.cs");

        Assert.Equal(12, schema.Types.Single(t => t.Name == "Vec3").UnmanagedSize);
        Assert.Equal(8, schema.Types.Single(t => t.Name == "PaddedStruct").UnmanagedSize);
        Assert.Equal(5, schema.Types.Single(t => t.Name == "PackedStruct").UnmanagedSize);

        // 문자열이 있는 struct 는 unmanaged 가 아니다.
        Assert.False(schema.Types.Single(t => t.Name == "ManagedStruct").IsUnmanaged);
    }

    [Fact]
    public void VersionTolerantAttributeIsDetected()
    {
        var schema = TestHelper.ParseCase("VersionTolerant.cs");
        Assert.True(schema.Types.Single().VersionTolerant);
    }

    [Fact]
    public void UnionAlternativesAreCollected()
    {
        var schema = TestHelper.ParseCase("Union.cs");
        var union = schema.Unions.Single();
        Assert.Equal("IShape", union.Name);
        Assert.Equal([(0, "CircleShape"), (1, "RectShape"), (300, "WideShape")], union.Alternatives);

        // 인터페이스 자체는 struct 로 만들지 않는다.
        Assert.DoesNotContain(schema.Types, t => t.Name == "IShape");
    }

    [Theory]
    [InlineData("int", "int32_t")]
    [InlineData("uint", "uint32_t")]
    [InlineData("byte", "uint8_t")]
    [InlineData("sbyte", "int8_t")]
    [InlineData("short", "int16_t")]
    [InlineData("ushort", "uint16_t")]
    [InlineData("long", "int64_t")]
    [InlineData("ulong", "uint64_t")]
    [InlineData("float", "float")]
    [InlineData("double", "double")]
    [InlineData("bool", "bool")]
    [InlineData("char", "char16_t")]
    [InlineData("string", "std::string")]
    [InlineData("string?", "std::string")]
    [InlineData("Guid", "memorypack::Guid")]
    [InlineData("DateTime", "memorypack::DateTime")]
    [InlineData("TimeSpan", "memorypack::TimeSpan")]
    [InlineData("DateTimeOffset", "memorypack::DateTimeOffset")]
    [InlineData("decimal", "memorypack::Decimal")]
    [InlineData("Half", "memorypack::Half")]
    [InlineData("Int128", "memorypack::Int128")]
    [InlineData("UInt128", "memorypack::UInt128")]
    [InlineData("List<int>?", "std::vector<int32_t>")]
    [InlineData("int[]", "std::vector<int32_t>")]
    [InlineData("List<string>?", "std::vector<std::string>")]
    [InlineData("Dictionary<int, string>?", "std::map<int32_t, std::string>")]
    [InlineData("HashSet<long>?", "std::set<int64_t>")]
    [InlineData("KeyValuePair<int, string>", "std::pair<int32_t, std::string>")]
    [InlineData("int?", "std::optional<int32_t>")]
    [InlineData("float?", "std::optional<float>")]
    [InlineData("Nullable<double>", "std::optional<double>")]
    public void TypeMappingTable(string csType, string cppType)
    {
        var source = $$"""
            using MemoryPack;
            [MemoryPackable]
            public partial class P
            {
                public {{csType}} Value { get; set; }
            }
            """;
        var schema = new CsParser().Parse(source);
        Assert.Equal(cppType, schema.Types.Single().Members.Single().CppType);
    }

    [Fact]
    public void NullableStringsOptionSwitchesToOptional()
    {
        const string source = """
            using MemoryPack;
            [MemoryPackable]
            public partial class P
            {
                public string? A { get; set; }
                public string B { get; set; }
            }
            """;
        var members = new CsParser(nullableStrings: true).Parse(source).Types.Single().Members;
        Assert.Equal("std::optional<std::string>", members[0].CppType);
        Assert.Equal("std::string", members[1].CppType);
    }

    [Fact]
    public void ReferenceMemberBecomesOptionalAndListStaysVector()
    {
        var schema = TestHelper.ParseCase("Nested.cs");
        var inv = schema.Types.Single(t => t.Name == "Inventory");
        Assert.Equal("std::optional<Item>", inv.Members[1].CppType);
        Assert.Equal("std::vector<Item>", inv.Members[2].CppType);
    }

    [Fact]
    public void FixedAndStdArrayAnnotationsAreParsed()
    {
        var fixedCase = TestHelper.ParseCase("FixedArray.cs");
        var skill = fixedCase.Types.Single(t => t.Name == "SkillSlotData");
        var fa = Assert.IsType<FixedArrayAnnotation>(skill.Members[1].Annotation);
        Assert.Equal(8, fa.Size);
        Assert.Equal("MAX_SKILLS", fa.ConstName);

        var stdCase = TestHelper.ParseCase("StdArray.cs");
        var transform = stdCase.Types.Single();
        var sa = Assert.IsType<StdArrayAnnotation>(transform.Members[1].Annotation);
        Assert.Equal(4, sa.Size);
        Assert.Equal("std::array<float, 4>", transform.Members[1].CppType);
    }

    [Fact]
    public void AnnotationIsFoundBetweenAttributeAndProperty()
    {
        const string source = """
            using MemoryPack;
            [MemoryPackable]
            public partial class P
            {
                [MemoryPackOrder(0)]
                // [cpp:fixed_array(4, MAX_N)]
                public List<int>? Values { get; set; }
            }
            """;
        var m = new CsParser().Parse(source).Types.Single().Members.Single();
        var fa = Assert.IsType<FixedArrayAnnotation>(m.Annotation);
        Assert.Equal(4, fa.Size);
        Assert.Equal("MAX_N", fa.ConstName);
    }

    [Theory]
    [InlineData("skillIds", "skillCount")]
    [InlineData("cooldowns", "cooldownCount")]
    [InlineData("tiles", "tileCount")]
    [InlineData("heights", "heightCount")]
    [InlineData("entries", "entryCount")]
    [InlineData("tag", "tagCount")]
    public void CountNameDerivation(string field, string expected) =>
        Assert.Equal(expected, CsParser.DeriveCountName(field));

    [Theory]
    [InlineData("PlayerId", "playerId")]
    [InlineData("ID", "id")]
    [InlineData("HTTPClient", "httpClient")]
    [InlineData("Name", "name")]
    public void PascalToCamelConversion(string input, string expected) =>
        Assert.Equal(expected, CsParser.PascalToCamel(input));
}
