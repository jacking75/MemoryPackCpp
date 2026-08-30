using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using FormatProbe;
using MemoryPack;

// ── FormatProbe ────────────────────────────────────────────────────────────────
//
// Emits golden wire-format fixtures produced by the real C# MemoryPack library so
// that the C++ implementation can be verified byte-for-byte against them.
//
//   dotnet run --project tools/FormatProbe -- generate <outputDir>
//       Writes <name>.bin for every case plus manifest.json and report.txt.
//
//   dotnet run --project tools/FormatProbe -- verify <fixtureDir>
//       Re-generates in memory and compares against the committed fixtures.
//       Exit code 1 on any mismatch (used by CI to detect MemoryPack changes).
//
//   dotnet run --project tools/FormatProbe -- check-cpp <cppOutputDir>
//       Deserializes bytes produced by the C++ test suite and validates the values.

var mode = args.Length > 0 ? args[0] : "generate";
var dir = args.Length > 1
    ? args[1]
    : Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "tests", "fixtures");
dir = Path.GetFullPath(dir);

var cases = FixtureCases.Build();

switch (mode)
{
    case "generate": return Generate(cases, dir);
    case "verify": return Verify(cases, dir);
    case "check-cpp": return CheckCpp(dir);
    default:
        Console.Error.WriteLine($"Unknown mode '{mode}'. Use generate | verify | check-cpp.");
        return 2;
}

static int Generate(List<FixtureCase> cases, string dir)
{
    Directory.CreateDirectory(dir);

    // Remove stale fixtures so a renamed case cannot linger.
    foreach (var stale in Directory.GetFiles(dir, "*.bin"))
        File.Delete(stale);

    var entries = new List<ManifestEntry>();
    var report = new StringBuilder();
    report.AppendLine("MemoryPack wire-format probe report");
    report.AppendLine($"MemoryPack package version: {FixtureCases.MemoryPackVersion}");
    report.AppendLine($"Runtime: {Environment.Version}");
    report.AppendLine(new string('=', 78));
    report.AppendLine();

    foreach (var c in cases)
    {
        var path = Path.Combine(dir, c.Name + ".bin");
        File.WriteAllBytes(path, c.Bytes);
        entries.Add(new ManifestEntry(c.Name, c.TypeName, c.Description, c.Bytes.Length, ToHex(c.Bytes)));

        report.AppendLine($"### {c.Name}   [{c.TypeName}]  {c.Bytes.Length} bytes");
        report.AppendLine($"    {c.Description}");
        report.AppendLine(HexDump(c.Bytes, "    "));
        report.AppendLine();
    }

    var manifest = new Manifest(FixtureCases.MemoryPackVersion, entries);
    File.WriteAllText(
        Path.Combine(dir, "manifest.json"),
        JsonSerializer.Serialize(manifest, ProbeJson.Default.Manifest),
        new UTF8Encoding(false));
    File.WriteAllText(Path.Combine(dir, "report.txt"), report.ToString(), new UTF8Encoding(false));

    Console.WriteLine($"Generated {cases.Count} fixtures in {dir}");
    return 0;
}

static int Verify(List<FixtureCase> cases, string dir)
{
    var failures = 0;
    foreach (var c in cases)
    {
        var path = Path.Combine(dir, c.Name + ".bin");
        if (!File.Exists(path))
        {
            Console.Error.WriteLine($"[MISSING] {c.Name}.bin");
            failures++;
            continue;
        }
        var committed = File.ReadAllBytes(path);
        if (!committed.AsSpan().SequenceEqual(c.Bytes))
        {
            Console.Error.WriteLine($"[MISMATCH] {c.Name}");
            Console.Error.WriteLine($"  committed: {ToHex(committed)}");
            Console.Error.WriteLine($"  current  : {ToHex(c.Bytes)}");
            failures++;
        }
    }

    if (failures > 0)
    {
        Console.Error.WriteLine($"{failures} fixture(s) differ from the installed MemoryPack output.");
        return 1;
    }
    Console.WriteLine($"All {cases.Count} fixtures match MemoryPack {FixtureCases.MemoryPackVersion}.");
    return 0;
}

// Validates the bytes the C++ test suite wrote: C# must be able to read them and
// see exactly the values the fixture describes.
static int CheckCpp(string dir)
{
    if (!Directory.Exists(dir))
    {
        Console.Error.WriteLine($"C++ output directory not found: {dir}");
        return 1;
    }

    var failures = 0;
    var checks = 0;

    void Check(string name, Action<byte[]> validate)
    {
        var path = Path.Combine(dir, name + ".bin");
        if (!File.Exists(path))
        {
            Console.Error.WriteLine($"[MISSING] {name}.bin (produced by the C++ tests)");
            failures++;
            return;
        }
        checks++;
        try
        {
            validate(File.ReadAllBytes(path));
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[FAIL] {name}: {ex.Message}");
            failures++;
        }
    }

    static void Expect(bool condition, string what)
    {
        if (!condition) throw new Exception($"expectation failed: {what}");
    }

    Check("simple_packet", b =>
    {
        var v = MemoryPackSerializer.Deserialize<SimplePacket>(b)!;
        Expect(v.Id == 42, "Id == 42");
        Expect(v.Name == "ABC", "Name == ABC");
    });

    Check("all_primitives", b =>
    {
        var v = MemoryPackSerializer.Deserialize<AllPrimitives>(b)!;
        var e = FixtureCases.SampleAllPrimitives();
        Expect(v.BoolValue == e.BoolValue, "bool");
        Expect(v.ByteValue == e.ByteValue, "byte");
        Expect(v.SByteValue == e.SByteValue, "sbyte");
        Expect(v.ShortValue == e.ShortValue, "short");
        Expect(v.UShortValue == e.UShortValue, "ushort");
        Expect(v.IntValue == e.IntValue, "int");
        Expect(v.UIntValue == e.UIntValue, "uint");
        Expect(v.LongValue == e.LongValue, "long");
        Expect(v.ULongValue == e.ULongValue, "ulong");
        Expect(v.FloatValue == e.FloatValue, "float");
        Expect(v.DoubleValue == e.DoubleValue, "double");
    });

    Check("string_packet", b =>
    {
        var v = MemoryPackSerializer.Deserialize<StringPacket>(b)!;
        Expect(v.Ascii == "Hello", "Ascii");
        Expect(v.Korean == "한글", "Korean");
        Expect(v.Emoji == "😀", "Emoji");
        Expect(v.Empty == "", "Empty");
        Expect(v.Null == null, "Null");
    });

    Check("inventory", b =>
    {
        var v = MemoryPackSerializer.Deserialize<Inventory>(b)!;
        Expect(v.OwnerId == 7, "OwnerId");
        Expect(v.Items is { Count: 3 }, "3 items");
        Expect(v.Items![0].ItemName == "Sword", "item 0 name");
        Expect(v.Items[2].Count == 30, "item 2 count");
    });

    Check("unmanaged_holder", b =>
    {
        var v = MemoryPackSerializer.Deserialize<UnmanagedHolder>(b)!;
        Expect(v.Position.X == 1.5f && v.Position.Y == 2.5f && v.Position.Z == 3.5f, "Vec3");
        Expect(v.Padded.Tag == 7 && v.Padded.Value == 1000, "PaddedStruct");
    });

    Check("unmanaged_list", b =>
    {
        var v = MemoryPackSerializer.Deserialize<UnmanagedList>(b)!;
        Expect(v.Points is { Count: 2 }, "2 points");
        Expect(v.Points![1].Z == 6f, "point 1 z");
    });

    Check("union_circle", b =>
    {
        var v = MemoryPackSerializer.Deserialize<UnionHolder>(b)!;
        Expect(v.Shape is CircleShape { Radius: 2.5f }, "circle radius 2.5");
    });

    Check("union_wide", b =>
    {
        var v = MemoryPackSerializer.Deserialize<UnionHolder>(b)!;
        Expect(v.Shape is WideShape { Sides: 12 }, "wide shape sides 12");
    });

    Check("nullable_packet", b =>
    {
        var v = MemoryPackSerializer.Deserialize<NullablePacket>(b)!;
        Expect(v.MaybeInt == 99, "MaybeInt");
        Expect(v.NullInt == null, "NullInt");
        Expect(v.MaybeFloat == 1.25f, "MaybeFloat");
        Expect(v.MaybeVec is { X: 1f }, "MaybeVec");
        Expect(v.NullVec == null, "NullVec");
    });

    Check("dict_packet", b =>
    {
        var v = MemoryPackSerializer.Deserialize<DictPacket>(b)!;
        Expect(v.IntMap is { Count: 3 } && v.IntMap[2] == 20, "IntMap");
        Expect(v.StringMap is { Count: 2 } && v.StringMap["b"] == 2, "StringMap");
        Expect(v.IntToString is { Count: 2 } && v.IntToString[1] == "one", "IntToString");
    });

    Check("set_packet", b =>
    {
        var v = MemoryPackSerializer.Deserialize<SetPacket>(b)!;
        Expect(v.IntSet is { Count: 3 } && v.IntSet.Contains(30), "IntSet");
        Expect(v.StringSet is { Count: 2 } && v.StringSet.Contains("beta"), "StringSet");
    });

    Check("bool_collection", b =>
    {
        var v = MemoryPackSerializer.Deserialize<BoolCollection>(b)!;
        Expect(v.Flags is { Count: 3 } && v.Flags[0] && !v.Flags[1] && v.Flags[2], "Flags");
        Expect(v.FlagArray is { Length: 2 }, "FlagArray");
    });

    Check("enum_packet", b =>
    {
        var v = MemoryPackSerializer.Deserialize<EnumPacket>(b)!;
        Expect(v.U16 == ColorU16.Green, "U16");
        Expect(v.I8 == ColorI8.Neg, "I8");
        Expect(v.I32 == ColorI32.B, "I32");
    });

    Check("dotnet_types", b =>
    {
        var v = MemoryPackSerializer.Deserialize<DotNetTypes>(b)!;
        var e = FixtureCases.SampleDotNetTypes();
        Expect(v.GuidValue == e.GuidValue, "Guid");
        Expect(v.DateTimeValue == e.DateTimeValue, "DateTime");
        Expect(v.TimeSpanValue == e.TimeSpanValue, "TimeSpan");
        Expect(v.CharValue == e.CharValue, "char");
    });

    Check("null_members", b =>
    {
        var v = MemoryPackSerializer.Deserialize<NullMembers>(b)!;
        Expect(v.NullString == null, "NullString");
        Expect(v.EmptyString == "", "EmptyString");
        Expect(v.NullList == null, "NullList");
        Expect(v.EmptyList is { Count: 0 }, "EmptyList");
        Expect(v.NullChild == null, "NullChild");
    });

    Check("nested_object", b =>
    {
        var v = MemoryPackSerializer.Deserialize<NestedObject>(b)!;
        Expect(v.Id == 5, "Id");
        Expect(v.Child is { ItemId: 9, ItemName: "Gem", Count: 3 }, "Child");
    });

    Check("array_packet", b =>
    {
        var v = MemoryPackSerializer.Deserialize<ArrayPacket>(b)!;
        Expect(v.Shorts is { Count: 3 }, "Shorts");
        Expect(v.Strings is { Count: 3 } && v.Strings[1] == "bb", "Strings");
        Expect(v.Doubles is { Count: 2 }, "Doubles");
    });

    Check("special_floats", b =>
    {
        var v = MemoryPackSerializer.Deserialize<SpecialFloats>(b)!;
        Expect(float.IsNaN(v.NanF), "NanF");
        Expect(float.IsPositiveInfinity(v.PosInfF), "PosInfF");
        Expect(float.IsNegativeInfinity(v.NegInfF), "NegInfF");
        Expect(double.IsNaN(v.NanD), "NanD");
        Expect(double.IsNegative(v.NegZeroD) && v.NegZeroD == 0.0, "NegZeroD");
    });

    Check("nested_list", b =>
    {
        var v = MemoryPackSerializer.Deserialize<NestedList>(b)!;
        Expect(v.Rows is { Count: 3 }, "3 rows");
        Expect(v.Rows![0] is { Count: 2 }, "row 0");
        Expect(v.Rows[2] is { Count: 0 }, "row 2 empty");
    });

    Check("value_tuple", b =>
    {
        var v = MemoryPackSerializer.Deserialize<TuplePacket>(b)!;
        Expect(v.UnmanagedValueTuple == (11, 22), "UnmanagedValueTuple");
        Expect(v.MixedValueTuple == (33, "tuple"), "MixedValueTuple");
    });

    Check("kvp_packet", b =>
    {
        var v = MemoryPackSerializer.Deserialize<KvpPacket>(b)!;
        Expect(v.IntPair.Key == 1 && v.IntPair.Value == 2, "IntPair");
        Expect(v.MixedPair.Key == 3 && v.MixedPair.Value == "kv", "MixedPair");
    });

    Check("version_tolerant", b =>
    {
        var v = MemoryPackSerializer.Deserialize<VersionTolerantPacket>(b)!;
        Expect(v.Id == 77, "Id");
        Expect(v.Name == "vt", "Name");
        Expect(v.Score == 0.5f, "Score");
    });

    if (failures > 0)
    {
        Console.Error.WriteLine($"{failures} C++-produced fixture(s) failed C# validation ({checks} checked).");
        return 1;
    }
    Console.WriteLine($"All {checks} C++-produced fixtures validated by C# MemoryPack.");
    return 0;
}

static string ToHex(byte[] bytes) => Convert.ToHexString(bytes);

static string HexDump(byte[] bytes, string indent)
{
    var sb = new StringBuilder();
    for (var i = 0; i < bytes.Length; i += 16)
    {
        sb.Append(indent).Append(i.ToString("X4")).Append("  ");
        for (var j = 0; j < 16; j++)
        {
            if (i + j < bytes.Length) sb.Append(bytes[i + j].ToString("X2")).Append(' ');
            else sb.Append("   ");
            if (j == 7) sb.Append(' ');
        }
        sb.Append(" |");
        for (var j = 0; j < 16 && i + j < bytes.Length; j++)
        {
            var c = bytes[i + j];
            sb.Append(c is >= 0x20 and < 0x7F ? (char)c : '.');
        }
        sb.Append('|');
        if (i + 16 < bytes.Length) sb.AppendLine();
    }
    return sb.ToString();
}

// ── Manifest model ─────────────────────────────────────────────────────────────

record ManifestEntry(string Name, string Type, string Description, int Length, string Hex);
record Manifest(string MemoryPackVersion, List<ManifestEntry> Fixtures);

[JsonSourceGenerationOptions(WriteIndented = true)]
[JsonSerializable(typeof(Manifest))]
partial class ProbeJson : JsonSerializerContext;
