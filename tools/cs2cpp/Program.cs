using System.Text;
using System.Text.RegularExpressions;

// ── C# MemoryPack 패킷 정의 → C++ 헤더 파일 변환 도구 ──────────────────────────

if (args.Length < 1)
{
    Console.WriteLine("Usage: cs2cpp <input.cs> [output.hpp]");
    Console.WriteLine();
    Console.WriteLine("C# MemoryPack 패킷 정의를 C++ 헤더 파일로 변환합니다.");
    Console.WriteLine();
    Console.WriteLine("Arguments:");
    Console.WriteLine("  input.cs    C# 패킷 정의 파일");
    Console.WriteLine("  output.hpp  출력 C++ 헤더 파일 (기본값: 입력 파일과 같은 이름의 .hpp)");
    return 1;
}

var inputPath = args[0];
var outputPath = args.Length >= 2
    ? args[1]
    : Path.ChangeExtension(inputPath, ".hpp");

if (!File.Exists(inputPath))
{
    Console.Error.WriteLine($"Error: 입력 파일을 찾을 수 없습니다: {inputPath}");
    return 1;
}

var content = File.ReadAllText(inputPath, Encoding.UTF8);
var parser = new CsParser();
parser.Parse(content);

if (parser.Classes.Count == 0)
    Console.Error.WriteLine($"Warning: [MemoryPackable] 클래스를 찾지 못했습니다: {inputPath}");

var generator = new CppGenerator(parser.Enums, parser.Classes);
var cppContent = generator.Generate();

// 출력 디렉토리가 존재하는지 확인
var outputDir = Path.GetDirectoryName(outputPath);
if (!string.IsNullOrEmpty(outputDir) && !Directory.Exists(outputDir))
    Directory.CreateDirectory(outputDir);

File.WriteAllText(outputPath, cppContent, new UTF8Encoding(false));

Console.WriteLine($"Generated: {outputPath}");
Console.WriteLine($"  Enums:   {parser.Enums.Count}");
Console.WriteLine($"  Packets: {parser.Classes.Count}");
foreach (var cls in parser.Classes)
    Console.WriteLine($"    - {cls.Name} ({cls.Properties.Count} members)");

return 0;


// ── 데이터 모델 ───────────────────────────────────────────────────────────────

record EnumMember(string Name, int Value);

record EnumDef(string Name, string CppBaseType, List<EnumMember> Members);

// C++ 어노테이션 — 프로퍼티 위의 주석으로 지정
abstract record CppAnnotation;
record FixedArrayAnnotation(int Size, string ConstName) : CppAnnotation;
record StdArrayAnnotation(int Size) : CppAnnotation;

record PropertyDef(
    string CsName,              // PascalCase
    string CppName,             // camelCase
    string CsType,              // 원본 C# 타입
    string CppType,             // 변환된 C++ 타입
    bool IsNullable,
    bool IsList,
    string ListElementType,     // List<T>의 T에 대한 C++ 타입
    CppAnnotation? Annotation = null
);

record ClassDef(string Name, List<PropertyDef> Properties);


// ── C# → C++ 타입 매핑 ────────────────────────────────────────────────────────

static class TypeMap
{
    static readonly Dictionary<string, string> CsToCpp = new()
    {
        ["bool"]   = "bool",
        ["byte"]   = "uint8_t",
        ["sbyte"]  = "int8_t",
        ["short"]  = "int16_t",
        ["ushort"] = "uint16_t",
        ["int"]    = "int32_t",
        ["uint"]   = "uint32_t",
        ["long"]   = "int64_t",
        ["ulong"]  = "uint64_t",
        ["float"]  = "float",
        ["double"] = "double",
        ["string"] = "std::string",
    };

    static readonly Dictionary<string, string> WriteMethod = new()
    {
        ["bool"]        = "WriteBool",
        ["uint8_t"]     = "WriteUInt8",
        ["int8_t"]      = "WriteInt8",
        ["int16_t"]     = "WriteInt16",
        ["uint16_t"]    = "WriteUInt16",
        ["int32_t"]     = "WriteInt32",
        ["uint32_t"]    = "WriteUInt32",
        ["int64_t"]     = "WriteInt64",
        ["uint64_t"]    = "WriteUInt64",
        ["float"]       = "WriteFloat",
        ["double"]      = "WriteDouble",
        ["std::string"] = "WriteString",
    };

    static readonly Dictionary<string, string> ReadMethod = new()
    {
        ["bool"]        = "ReadBool",
        ["uint8_t"]     = "ReadUInt8",
        ["int8_t"]      = "ReadInt8",
        ["int16_t"]     = "ReadInt16",
        ["uint16_t"]    = "ReadUInt16",
        ["int32_t"]     = "ReadInt32",
        ["uint32_t"]    = "ReadUInt32",
        ["int64_t"]     = "ReadInt64",
        ["uint64_t"]    = "ReadUInt64",
        ["float"]       = "ReadFloat",
        ["double"]      = "ReadDouble",
        ["std::string"] = "ReadString",
    };

    static readonly Dictionary<string, string> DefaultValue = new()
    {
        ["bool"]     = "false",
        ["uint8_t"]  = "0",
        ["int8_t"]   = "0",
        ["int16_t"]  = "0",
        ["uint16_t"] = "0",
        ["int32_t"]  = "0",
        ["uint32_t"] = "0",
        ["int64_t"]  = "0",
        ["uint64_t"] = "0",
        ["float"]    = "0.f",
        ["double"]   = "0.0",
    };

    public static string ToCppType(string csType) =>
        CsToCpp.GetValueOrDefault(csType, csType);

    public static string GetWriteMethod(string cppType) =>
        WriteMethod.GetValueOrDefault(cppType, $"/* TODO: {cppType} */");

    public static string GetReadMethod(string cppType) =>
        ReadMethod.GetValueOrDefault(cppType, $"/* TODO: {cppType} */");

    public static string? GetDefaultValue(string cppType) =>
        DefaultValue.GetValueOrDefault(cppType);

    /// <summary>
    /// C# 타입 문자열을 파싱하여 C++ 정보로 변환.
    /// </summary>
    public static (string CppType, bool IsNullable, bool IsList, string ListElementType) ParseCsType(string csType)
    {
        var isNullable = csType.EndsWith('?');
        var baseType = csType.TrimEnd('?');

        // List<T> 확인
        var listMatch = Regex.Match(baseType, @"^List<(.+)>$");
        if (listMatch.Success)
        {
            var elementCs = listMatch.Groups[1].Value;
            var elementCpp = ToCppType(elementCs);
            var cppType = elementCs == "string"
                ? "std::vector<std::string>"
                : $"std::vector<{elementCpp}>";
            return (cppType, isNullable, true, elementCpp);
        }

        // T[] 배열 확인
        var arrayMatch = Regex.Match(baseType, @"^(.+)\[\]$");
        if (arrayMatch.Success)
        {
            var elementCs = arrayMatch.Groups[1].Value;
            var elementCpp = ToCppType(elementCs);
            var cppType = elementCs == "string"
                ? "std::vector<std::string>"
                : $"std::vector<{elementCpp}>";
            return (cppType, isNullable, true, elementCpp);
        }

        return (ToCppType(baseType), isNullable, false, "");
    }
}


// ── C# 파일 파서 ──────────────────────────────────────────────────────────────

class CsParser
{
    public List<EnumDef> Enums { get; } = [];
    public List<ClassDef> Classes { get; } = [];

    // 어노테이션 패턴
    static readonly Regex FixedArrayPattern = new(@"//\s*\[cpp:fixed_array\((\d+)\s*,\s*(\w+)\)\]");
    static readonly Regex StdArrayPattern = new(@"//\s*\[cpp:std_array\((\d+)\)\]");

    public void Parse(string content)
    {
        ParseEnums(content);
        ParseClasses(content);
    }

    void ParseEnums(string content)
    {
        var pattern = new Regex(@"public\s+enum\s+(\w+)\s*:\s*(\w+)\s*\{([^}]+)\}");
        foreach (Match match in pattern.Matches(content))
        {
            var name = match.Groups[1].Value;
            var cppBase = TypeMap.ToCppType(match.Groups[2].Value);
            var body = match.Groups[3].Value;

            var members = new List<EnumMember>();
            foreach (Match m in Regex.Matches(body, @"(\w+)\s*=\s*(\d+)"))
                members.Add(new EnumMember(m.Groups[1].Value, int.Parse(m.Groups[2].Value)));

            Enums.Add(new EnumDef(name, cppBase, members));
        }
    }

    void ParseClasses(string content)
    {
        // [MemoryPackable] class 를 찾고 중괄호 중첩을 올바르게 처리
        var headerPattern = new Regex(@"\[MemoryPackable\]\s*public\s+partial\s+class\s+(\w+)\s*\{");
        foreach (Match match in headerPattern.Matches(content))
        {
            var className = match.Groups[1].Value;
            var bracePos = match.Index + match.Length - 1; // '{' 위치
            var body = ExtractBraceBlock(content, bracePos);

            var props = new List<PropertyDef>();
            var propPattern = new Regex(@"public\s+([\w<>\[\]]+\??)\s+(\w+)\s*\{\s*get;\s*set;\s*\}");
            foreach (Match pm in propPattern.Matches(body))
            {
                var csType = pm.Groups[1].Value.Trim();
                var csName = pm.Groups[2].Value;
                var cppName = PascalToCamel(csName);
                var (cppType, isNullable, isList, listElem) = TypeMap.ParseCsType(csType);

                // 프로퍼티 앞의 주석에서 어노테이션 파싱
                var annotation = ParseAnnotation(body, pm.Index);

                props.Add(new PropertyDef(
                    csName, cppName, csType, cppType,
                    isNullable, isList, listElem, annotation));
            }

            Classes.Add(new ClassDef(className, props));
        }
    }

    /// <summary>
    /// 프로퍼티 앞의 줄에서 [cpp:...] 어노테이션을 찾아 반환.
    /// </summary>
    static CppAnnotation? ParseAnnotation(string body, int propertyIndex)
    {
        // propertyIndex 앞의 텍스트에서 직전 줄을 추출
        var before = body[..propertyIndex];
        var lines = before.Split('\n');

        // 뒤에서부터 빈 줄을 건너뛰고 주석 줄을 탐색
        for (var i = lines.Length - 1; i >= 0; i--)
        {
            var line = lines[i].Trim();
            if (string.IsNullOrEmpty(line)) continue;

            var fa = FixedArrayPattern.Match(line);
            if (fa.Success)
                return new FixedArrayAnnotation(int.Parse(fa.Groups[1].Value), fa.Groups[2].Value);

            var sa = StdArrayPattern.Match(line);
            if (sa.Success)
                return new StdArrayAnnotation(int.Parse(sa.Groups[1].Value));

            // 일반 주석(// 또는 ///)은 건너뛰고 계속 탐색
            if (line.StartsWith("//")) continue;

            break; // 코드 줄이면 중단
        }

        return null;
    }

    /// <summary>
    /// start 위치의 '{' 부터 매칭되는 '}' 까지의 내용을 반환 (중괄호 중첩 처리).
    /// </summary>
    static string ExtractBraceBlock(string text, int start)
    {
        var depth = 0;
        for (var i = start; i < text.Length; i++)
        {
            if (text[i] == '{') depth++;
            else if (text[i] == '}')
            {
                depth--;
                if (depth == 0)
                    return text[(start + 1)..i];
            }
        }
        return text[(start + 1)..];
    }

    static string PascalToCamel(string name)
    {
        if (string.IsNullOrEmpty(name)) return name;

        // 연속 대문자 처리 (예: "ID" → "id", "HTTPClient" → "httpClient")
        if (name.Length >= 2 && char.IsUpper(name[0]) && char.IsUpper(name[1]))
        {
            var i = 0;
            while (i < name.Length && char.IsUpper(name[i])) i++;
            if (i == name.Length)
                return name.ToLowerInvariant();
            return name[..(i - 1)].ToLowerInvariant() + name[(i - 1)..];
        }

        return char.ToLowerInvariant(name[0]) + name[1..];
    }

    /// <summary>
    /// C++ 배열 필드명에서 count 변수명을 유도.
    /// 예: skillIds → skillCount, cooldowns → cooldownCount, tiles → tileCount
    /// </summary>
    public static string DeriveCountName(string cppName)
    {
        string baseName;
        if (cppName.EndsWith("Ids", StringComparison.Ordinal))
            baseName = cppName[..^3];  // "skillIds" → "skill" + "Count"
        else if (cppName.EndsWith("ies", StringComparison.Ordinal))
            baseName = cppName[..^3] + "y";
        else if (cppName.EndsWith("ses", StringComparison.Ordinal) ||
                 cppName.EndsWith("xes", StringComparison.Ordinal) ||
                 cppName.EndsWith("zes", StringComparison.Ordinal))
            baseName = cppName[..^2];
        else if (cppName.EndsWith("s", StringComparison.Ordinal))
            baseName = cppName[..^1];
        else
            baseName = cppName;

        return baseName + "Count";
    }
}


// ── C++ 코드 생성기 ──────────────────────────────────────────────────────────

class CppGenerator(List<EnumDef> enums, List<ClassDef> classes)
{
    readonly StringBuilder sb = new();

    public string Generate()
    {
        sb.Clear();
        GenerateIncludes();
        GenerateEnums();
        GeneratePacketHeaderConst();
        GenerateStructs();
        GenerateMemoryPackables();
        return sb.ToString();
    }

    void Line(string text = "") => sb.AppendLine(text);

    void GenerateIncludes()
    {
        Line("#pragma once");
        Line("#include \"memorypack/memorypack.hpp\"");
        Line("#include <cstdint>");
        Line("#include <string>");

        var needsVector = classes.Any(c => c.Properties.Any(
            p => p.IsList && p.Annotation is null));
        if (needsVector)
            Line("#include <vector>");

        var needsArray = classes.Any(c => c.Properties.Any(
            p => p.Annotation is StdArrayAnnotation));
        if (needsArray)
            Line("#include <array>");

        Line();
    }

    void GenerateEnums()
    {
        if (enums.Count == 0) return;

        Line($"// ── Packet IDs ─────────────────────────────────────────────────────────────────");
        foreach (var e in enums)
        {
            Line($"enum class {e.Name} : {e.CppBaseType} {{");

            var maxNameLen = e.Members.Max(m => m.Name.Length);
            foreach (var m in e.Members)
                Line($"    {m.Name.PadRight(maxNameLen)} = {m.Value},");

            Line("};");
        }
        Line();
    }

    void GeneratePacketHeaderConst()
    {
        Line("// ── Packet Header: [2B packetId][4B bodyLength] ────────────────────────────────");
        Line("constexpr size_t PACKET_HEADER_SIZE = 6;");
        Line();
    }

    void GenerateStructs()
    {
        Line("// ── Packet Structs ─────────────────────────────────────────────────────────────");
        Line("// Member order MUST match C# [MemoryPackable] class declaration order.");
        Line();

        foreach (var cls in classes)
        {
            Line($"struct {cls.Name} {{");

            // static constexpr 상수 (중복 제거)
            var emittedConsts = new HashSet<string>();
            foreach (var prop in cls.Properties)
            {
                if (prop.Annotation is FixedArrayAnnotation fa && emittedConsts.Add(fa.ConstName))
                    Line($"    static constexpr int32_t {fa.ConstName} = {fa.Size};");
            }
            if (emittedConsts.Count > 0)
                Line();

            // 멤버 필드
            if (cls.Properties.Count > 0)
            {
                // 타입 정렬 폭 계산 (FixedArray는 독자적 포맷이므로 제외)
                var maxTypeLen = cls.Properties
                    .Where(p => p.Annotation is not FixedArrayAnnotation)
                    .Select(p => p.Annotation switch
                    {
                        StdArrayAnnotation sa => $"std::array<{p.ListElementType}, {sa.Size}>".Length,
                        _ => p.CppType.Length,
                    })
                    .DefaultIfEmpty(0)
                    .Max();

                foreach (var prop in cls.Properties)
                {
                    switch (prop.Annotation)
                    {
                        case FixedArrayAnnotation fa:
                        {
                            var countName = CsParser.DeriveCountName(prop.CppName);
                            Line($"    {prop.ListElementType} {prop.CppName}[{fa.ConstName}] = {{}};");
                            Line($"    int32_t {countName} = 0;");
                            break;
                        }
                        case StdArrayAnnotation sa:
                        {
                            var typeStr = $"std::array<{prop.ListElementType}, {sa.Size}>";
                            Line($"    {typeStr.PadRight(maxTypeLen)} {prop.CppName} = {{}};");
                            break;
                        }
                        default:
                        {
                            var defaultVal = TypeMap.GetDefaultValue(prop.CppType);
                            if (defaultVal is not null)
                                Line($"    {prop.CppType.PadRight(maxTypeLen)} {prop.CppName} = {defaultVal};");
                            else
                                Line($"    {prop.CppType.PadRight(maxTypeLen)} {prop.CppName};");
                            break;
                        }
                    }
                }
            }

            Line("};");
            Line();
        }
    }

    void GenerateMemoryPackables()
    {
        Line("// ── IMemoryPackable Specializations ────────────────────────────────────────────");
        Line("namespace memorypack {");
        Line();

        for (var i = 0; i < classes.Count; i++)
        {
            var cls = classes[i];
            var memberCount = cls.Properties.Count;

            Line($"// --- {cls.Name} (memberCount={memberCount}) ---");
            Line($"template<> struct IMemoryPackable<{cls.Name}> {{");

            // Serialize
            Line($"    static void Serialize(MemoryPackWriter& w, const {cls.Name}* v) {{");
            Line($"        if (!v) {{ w.WriteNullObjectHeader(); return; }}");
            Line($"        w.WriteObjectHeader({memberCount});");
            foreach (var prop in cls.Properties)
                Line(GenerateSerializeLine(prop));
            Line("    }");

            // Deserialize
            Line($"    static void Deserialize(MemoryPackReader& r, {cls.Name}& v) {{");
            Line("        auto [cnt, isNull] = r.ReadObjectHeader();");
            Line("        if (isNull) return;");
            for (var j = 0; j < cls.Properties.Count; j++)
                Line(GenerateDeserializeLine(cls.Properties[j], j, cls.Name));
            Line("    }");

            Line("};");

            if (i < classes.Count - 1)
                Line();
        }

        Line();
        Line("} // namespace memorypack");
    }

    static string GenerateSerializeLine(PropertyDef prop)
    {
        switch (prop.Annotation)
        {
            case FixedArrayAnnotation:
            {
                var countName = CsParser.DeriveCountName(prop.CppName);
                return $"        w.WriteArray(v->{prop.CppName}, v->{countName});";
            }
            case StdArrayAnnotation:
                return $"        w.WriteArray(v->{prop.CppName});";
        }

        if (prop.IsList)
        {
            return prop.ListElementType == "std::string"
                ? $"        w.WriteStringVector(v->{prop.CppName});"
                : $"        w.WriteVector(v->{prop.CppName});";
        }

        var method = TypeMap.GetWriteMethod(prop.CppType);
        return $"        w.{method}(v->{prop.CppName});";
    }

    static string GenerateDeserializeLine(PropertyDef prop, int index, string className)
    {
        var n = index + 1;

        switch (prop.Annotation)
        {
            case FixedArrayAnnotation fa:
            {
                var countName = CsParser.DeriveCountName(prop.CppName);
                return $"        if (cnt >= {n}) v.{countName} = r.ReadArray(v.{prop.CppName}, {className}::{fa.ConstName});";
            }
            case StdArrayAnnotation sa:
                return $"        if (cnt >= {n}) v.{prop.CppName} = r.ReadArray<{prop.ListElementType}, {sa.Size}>();";
        }

        if (prop.IsList)
        {
            return prop.ListElementType == "std::string"
                ? $"        if (cnt >= {n}) v.{prop.CppName} = r.ReadStringVector();"
                : $"        if (cnt >= {n}) v.{prop.CppName} = r.ReadVector<{prop.ListElementType}>();";
        }

        if (prop.CppType == "std::string")
            return $"        if (cnt >= {n}) {{ auto s = r.ReadString(); v.{prop.CppName} = s.value_or(\"\"); }}";

        var method = TypeMap.GetReadMethod(prop.CppType);
        return $"        if (cnt >= {n}) v.{prop.CppName} = r.{method}();";
    }
}
