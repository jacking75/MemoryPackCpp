using System.Text;

namespace Cs2Cpp.Tests;

public static class TestHelper
{
    /// <summary>cs2cpp.Tests 프로젝트 디렉터리 (bin 출력 위치에서 거슬러 올라가 찾는다).</summary>
    public static string ProjectDir { get; } = FindProjectDir();

    static string FindProjectDir()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "cs2cpp.Tests.csproj"))) return dir.FullName;
            dir = dir.Parent;
        }
        throw new DirectoryNotFoundException("cs2cpp.Tests.csproj 를 찾을 수 없습니다.");
    }

    public static string CasePath(string fileName) => Path.Combine(ProjectDir, "Cases", fileName);

    public static string ReadCase(string fileName) => File.ReadAllText(CasePath(fileName), Encoding.UTF8);

    public static Schema ParseCase(string fileName, bool nullableStrings = false) =>
        new CsParser(nullableStrings).Parse(ReadCase(fileName), CasePath(fileName));

    public static string Generate(string fileName, GeneratorOptions options)
    {
        var schema = ParseCase(fileName, options.NullableStrings);
        return new CppGenerator(schema, options).Generate();
    }

    public static string Normalize(string text) => text.Replace("\r\n", "\n").Replace('\r', '\n');
}
