using System.Text;
using Xunit;

namespace Cs2Cpp.Tests;

/// <summary>
/// Cases/*.cs 를 생성기에 통과시킨 결과가 Expected/*.hpp 와 정확히 같은지 검사한다.
/// 기대 파일을 다시 만들려면 환경변수 CS2CPP_UPDATE_SNAPSHOTS=1 로 테스트를 실행한다.
/// </summary>
public sealed class SnapshotTests
{
    public static readonly Dictionary<string, (string File, GeneratorOptions Options)> Cases = new()
    {
        ["Plain"] = ("Plain.cs", new GeneratorOptions()),
        ["PlainExplicit"] = ("Plain.cs", new GeneratorOptions { Style = GeneratorStyle.Explicit }),
        ["PlainNullableStrings"] = ("Plain.cs", new GeneratorOptions { NullableStrings = true }),
        ["Enums"] = ("Enums.cs", new GeneratorOptions()),
        ["FixedArray"] = ("FixedArray.cs", new GeneratorOptions()),
        ["FixedArrayExplicit"] = ("FixedArray.cs", new GeneratorOptions { Style = GeneratorStyle.Explicit }),
        ["StdArray"] = ("StdArray.cs", new GeneratorOptions()),
        ["Nested"] = ("Nested.cs", new GeneratorOptions()),
        ["NestedNamespaced"] = ("Nested.cs", new GeneratorOptions { Namespace = "game" }),
        ["Collections"] = ("Collections.cs", new GeneratorOptions()),
        ["Unmanaged"] = ("Unmanaged.cs", new GeneratorOptions()),
        ["Union"] = ("Union.cs", new GeneratorOptions()),
        ["VersionTolerant"] = ("VersionTolerant.cs", new GeneratorOptions()),
        ["OrderIgnore"] = ("OrderIgnore.cs", new GeneratorOptions()),
        ["Dispatch"] = ("Dispatch.cs", new GeneratorOptions()),
        ["DispatchExplicitOff"] = ("Dispatch.cs", new GeneratorOptions
        {
            Style = GeneratorStyle.Explicit,
            Dispatch = false,
        }),
    };

    public static TheoryData<string> CaseNames
    {
        get
        {
            var data = new TheoryData<string>();
            foreach (var name in Cases.Keys.OrderBy(k => k, StringComparer.Ordinal)) data.Add(name);
            return data;
        }
    }

    [Theory]
    [MemberData(nameof(CaseNames))]
    public void GeneratedHeaderMatchesSnapshot(string caseName)
    {
        var (file, options) = Cases[caseName];
        var actual = TestHelper.Generate(file, options);
        var expectedPath = Path.Combine(TestHelper.ProjectDir, "Expected", caseName + ".hpp");

        if (Environment.GetEnvironmentVariable("CS2CPP_UPDATE_SNAPSHOTS") == "1")
        {
            Directory.CreateDirectory(Path.GetDirectoryName(expectedPath)!);
            File.WriteAllText(expectedPath, actual, new UTF8Encoding(false));
            return;
        }

        Assert.True(File.Exists(expectedPath), $"기대 파일이 없습니다: {expectedPath}");
        var expected = TestHelper.Normalize(File.ReadAllText(expectedPath, Encoding.UTF8));
        Assert.Equal(expected, TestHelper.Normalize(actual));
    }

    [Fact]
    public void EverySnapshotCaseHasAnExpectedFile()
    {
        foreach (var name in Cases.Keys)
        {
            var path = Path.Combine(TestHelper.ProjectDir, "Expected", name + ".hpp");
            Assert.True(File.Exists(path), $"기대 파일이 없습니다: {path}");
        }
    }
}
