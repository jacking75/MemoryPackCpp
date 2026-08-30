using System.Text;
using Xunit;

namespace Cs2Cpp.Tests;

public sealed class CliTests
{
    [Fact]
    public void ParsesInputsAndOptions()
    {
        var o = CliOptions.Parse(
            ["a.cs", "b.cs", "-o", "out", "--namespace", "game", "--style", "explicit",
             "--nullable-strings", "--verbose", "--no-dispatch"],
            out var error);

        Assert.Null(error);
        Assert.NotNull(o);
        Assert.Equal(["a.cs", "b.cs"], o.Inputs);
        Assert.Equal("out", o.Output);
        Assert.Equal("game", o.Namespace);
        Assert.Equal(GeneratorStyle.Explicit, o.Style);
        Assert.True(o.NullableStrings);
        Assert.True(o.Verbose);
        Assert.False(o.Dispatch);
    }

    [Fact]
    public void SupportsEqualsForm()
    {
        var o = CliOptions.Parse(["a.cs", "--style=macro", "--output=x.hpp"], out var error);
        Assert.Null(error);
        Assert.NotNull(o);
        Assert.Equal(GeneratorStyle.Macro, o.Style);
        Assert.Equal("x.hpp", o.Output);
    }

    [Fact]
    public void RejectsUnknownOptionAndMissingInput()
    {
        Assert.Null(CliOptions.Parse(["a.cs", "--nope"], out var e1));
        Assert.NotNull(e1);

        Assert.Null(CliOptions.Parse([], out var e2));
        Assert.NotNull(e2);

        Assert.Null(CliOptions.Parse(["a.cs", "--style", "weird"], out var e3));
        Assert.NotNull(e3);
    }

    [Fact]
    public void HelpNeedsNoInput()
    {
        var o = CliOptions.Parse(["--help"], out var error);
        Assert.Null(error);
        Assert.NotNull(o);
        Assert.True(o.Help);
    }

    [Fact]
    public void RunWritesHeaderAndCheckAgrees()
    {
        var temp = Directory.CreateTempSubdirectory("cs2cpp-test");
        try
        {
            var input = TestHelper.CasePath("Dispatch.cs");
            var output = Path.Combine(temp.FullName, "packets.hpp");

            var write = CliOptions.Parse([input, "-o", output], out var error);
            Assert.Null(error);
            Assert.NotNull(write);
            Assert.Equal(0, new GeneratorRunner(write, TextWriter.Null, TextWriter.Null).Run());
            Assert.True(File.Exists(output));

            // 방금 쓴 파일이므로 --check 는 통과해야 한다.
            var check = CliOptions.Parse([input, "-o", output, "--check"], out error)!;
            Assert.Equal(0, new GeneratorRunner(check, TextWriter.Null, TextWriter.Null).Run());

            // 내용을 바꾸면 --check 는 실패하고 diff 를 낸다.
            File.WriteAllText(output, "#pragma once\n// tampered\n", new UTF8Encoding(false));
            var stderr = new StringWriter();
            Assert.Equal(1, new GeneratorRunner(check, TextWriter.Null, stderr).Run());
            var report = stderr.ToString();
            Assert.Contains("--check FAILED", report);
            Assert.Contains("+ ", report);
        }
        finally
        {
            temp.Delete(recursive: true);
        }
    }

    [Fact]
    public void EmitsCSharpSchemaHashFile()
    {
        var temp = Directory.CreateTempSubdirectory("cs2cpp-test");
        try
        {
            var input = TestHelper.CasePath("Dispatch.cs");
            var hpp = Path.Combine(temp.FullName, "packets.hpp");
            var cs = Path.Combine(temp.FullName, "PacketSchema.g.cs");

            var options = CliOptions.Parse([input, "-o", hpp, "--emit-schema-hash-cs", cs], out var error)!;
            Assert.Null(error);
            Assert.Equal(0, new GeneratorRunner(options, TextWriter.Null, TextWriter.Null).Run());

            var text = File.ReadAllText(cs);
            Assert.Contains("public const ulong PacketSchemaHash = 0x", text);
            Assert.Contains("namespace Cases.Dispatch;", text);

            // C# 상수와 C++ 상수는 같은 값이어야 한다.
            var hash = SchemaHash.Compute(TestHelper.ParseCase("Dispatch.cs").Types);
            Assert.Contains($"0x{hash:X16}UL", text);
            Assert.Contains($"0x{hash:X16}ULL", File.ReadAllText(hpp));
        }
        finally
        {
            temp.Delete(recursive: true);
        }
    }

    [Fact]
    public void ResolvesDirectoryAndGlobInputs()
    {
        var casesDir = Path.Combine(TestHelper.ProjectDir, "Cases");

        var fromDir = GeneratorRunner.ResolveInputs([casesDir]);
        Assert.Contains(fromDir, f => f.EndsWith("Plain.cs", StringComparison.Ordinal));

        var fromGlob = GeneratorRunner.ResolveInputs([Path.Combine(casesDir, "Un*.cs")]);
        Assert.Equal(2, fromGlob.Count);   // Union.cs, Unmanaged.cs
    }
}

public sealed class SchemaHashTests
{
    [Fact]
    public void HashIsDeterministicAndOrderSensitive()
    {
        var a = SchemaHash.Compute(TestHelper.ParseCase("Dispatch.cs").Types);
        var b = SchemaHash.Compute(TestHelper.ParseCase("Dispatch.cs").Types);
        Assert.Equal(a, b);

        const string changed = """
            using MemoryPack;
            namespace Cases.Dispatch;
            public enum PacketId : ushort { LoginRequest = 1, LoginResponse = 2, Heartbeat = 3 }
            [MemoryPackable]
            public partial class LoginRequest
            {
                public int Level { get; set; }
                public string? Username { get; set; }
            }
            [MemoryPackable]
            public partial class LoginResponse
            {
                public bool Success { get; set; }
                public int PlayerId { get; set; }
            }
            """;
        var c = SchemaHash.Compute(new CsParser().Parse(changed).Types);
        Assert.NotEqual(a, c);
    }

    [Fact]
    public void KnownFnvVector()
    {
        // FNV-1a 64bit 참조 값
        Assert.Equal(0xcbf29ce484222325UL, SchemaHash.Fnv1a64(""));
        Assert.Equal(0xaf63dc4c8601ec8cUL, SchemaHash.Fnv1a64("a"));
    }

    [Fact]
    public void CanonicalStringShape()
    {
        var text = SchemaHash.BuildCanonicalString(TestHelper.ParseCase("Dispatch.cs").Types);
        Assert.Equal("LoginRequest(0:string?;1:int;)\nLoginResponse(0:bool;1:int;)\n", text);
    }
}
