using System.Text;

namespace Cs2Cpp;

public sealed class GeneratorOptions
{
    public GeneratorStyle Style { get; init; } = GeneratorStyle.Macro;
    public string? Namespace { get; init; }
    public bool NullableStrings { get; init; }
    /// <summary>null = PacketId enum 이 있으면 자동으로 켜짐.</summary>
    public bool? Dispatch { get; init; }
}

/// <summary>파싱된 <see cref="Schema"/>를 C++ 헤더 텍스트로 변환한다.</summary>
public sealed class CppGenerator
{
    const int BannerWidth = 82;

    readonly Schema schema;
    readonly GeneratorOptions options;
    readonly StringBuilder sb = new();
    readonly Dictionary<string, PackableType> typeByName;
    readonly Dictionary<string, UnionDef> unionByName;
    readonly List<object> emitOrder = [];
    readonly List<string> warnings = [];

    public CppGenerator(Schema schema, GeneratorOptions? options = null)
    {
        this.schema = schema;
        this.options = options ?? new GeneratorOptions();
        typeByName = schema.Types.ToDictionary(t => t.Name, StringComparer.Ordinal);
        unionByName = schema.Unions.ToDictionary(u => u.Name, StringComparer.Ordinal);
        BuildEmitOrder();
        SchemaHashValue = SchemaHash.Compute(OrderedTypes);
    }

    /// <summary>선언 의존성 순으로 정렬된 [MemoryPackable] 타입들.</summary>
    public IReadOnlyList<PackableType> OrderedTypes => emitOrder.OfType<PackableType>().ToList();

    public ulong SchemaHashValue { get; }

    public IReadOnlyList<string> Warnings => warnings;

    /// <summary>PacketId enum 과 이름이 겹치는 패킷 타입이 있으면 디스패치 테이블을 만든다.</summary>
    public bool DispatchEnabled
    {
        get
        {
            if (options.Dispatch == false) return false;
            var e = schema.FindPacketIdEnum();
            if (e is null) return false;
            return e.Members.Any(m => typeByName.ContainsKey(m.Name));
        }
    }

    public string Generate()
    {
        sb.Clear();
        warnings.Clear();

        EmitIncludes();
        var ns = options.Namespace;
        if (!string.IsNullOrEmpty(ns)) { Line($"namespace {ns} {{"); Line(); }

        EmitEnums();
        EmitPacketHeaderConst();
        EmitDeclarations();

        if (!string.IsNullOrEmpty(ns)) { Line($"}} // namespace {ns}"); Line(); }

        EmitSerializers();
        EmitDispatch();

        return sb.ToString();
    }

    // ── 출력 헬퍼 ─────────────────────────────────────────────────────────────

    void Line(string text = "") => sb.Append(text).Append('\n');

    static string Banner(string title)
    {
        const string prefix = "// ── ";
        var pad = BannerWidth - prefix.Length - title.Length - 1;
        return prefix + title + " " + new string('─', Math.Max(pad, 3));
    }

    string Qualified(string name) =>
        string.IsNullOrEmpty(options.Namespace) ? name : $"{options.Namespace}::{name}";

    // ── 선언 순서 (의존성 위상 정렬) ──────────────────────────────────────────

    void BuildEmitOrder()
    {
        var emitted = new HashSet<string>(StringComparer.Ordinal);
        var visiting = new HashSet<string>(StringComparer.Ordinal);

        void Visit(string name)
        {
            if (emitted.Contains(name) || !visiting.Add(name)) return;

            if (typeByName.TryGetValue(name, out var t))
            {
                foreach (var dep in t.Members.SelectMany(m => m.ReferencedTypes).Distinct(StringComparer.Ordinal))
                    if (dep != name) Visit(dep);
                if (emitted.Add(name)) emitOrder.Add(t);
            }
            else if (unionByName.TryGetValue(name, out var u))
            {
                foreach (var alt in u.Alternatives)
                    Visit(alt.TypeName);
                if (emitted.Add(name)) emitOrder.Add(u);
            }

            visiting.Remove(name);
        }

        foreach (var t in schema.Types) Visit(t.Name);
        foreach (var u in schema.Unions) Visit(u.Name);
    }

    // ── #include ──────────────────────────────────────────────────────────────

    void EmitIncludes()
    {
        var includes = new SortedSet<string>(StringComparer.Ordinal) { "<cstdint>" };

        foreach (var t in schema.Types)
        {
            foreach (var m in t.Members)
            {
                if (m.Annotation is FixedArrayAnnotation) continue;   // C 배열은 헤더가 필요 없다
                AddIncludesFor(includes, m.CppType);
            }
        }
        if (schema.Unions.Count > 0) { includes.Add("<variant>"); includes.Add("<optional>"); }
        if (DispatchEnabled) includes.Add("<span>");

        Line("#pragma once");
        Line("#include \"memorypack/memorypack.hpp\"");
        foreach (var i in includes) Line($"#include {i}");
        Line();
    }

    static void AddIncludesFor(SortedSet<string> includes, string cppType)
    {
        if (cppType.Contains("std::string", StringComparison.Ordinal)) includes.Add("<string>");
        if (cppType.Contains("std::vector", StringComparison.Ordinal)) includes.Add("<vector>");
        if (cppType.Contains("std::array", StringComparison.Ordinal)) includes.Add("<array>");
        if (cppType.Contains("std::map", StringComparison.Ordinal)) includes.Add("<map>");
        if (cppType.Contains("std::set", StringComparison.Ordinal)) includes.Add("<set>");
        if (cppType.Contains("std::pair", StringComparison.Ordinal)) includes.Add("<utility>");
        if (cppType.Contains("std::optional", StringComparison.Ordinal)) includes.Add("<optional>");
        if (cppType.Contains("std::variant", StringComparison.Ordinal)) includes.Add("<variant>");
    }

    // ── enum ──────────────────────────────────────────────────────────────────

    void EmitEnums()
    {
        if (schema.Enums.Count == 0) return;

        Line(Banner(schema.FindPacketIdEnum() is null ? "Enums" : "Packet IDs"));
        foreach (var e in schema.Enums)
        {
            Line($"enum class {e.Name} : {e.CppBaseType} {{");
            if (e.Members.Count > 0)
            {
                var width = e.Members.Max(m => m.Name.Length);
                foreach (var m in e.Members)
                    Line($"    {m.Name.PadRight(width)} = {m.Value},");
            }
            Line("};");
        }
        Line();
    }

    void EmitPacketHeaderConst()
    {
        // 패킷 프로토콜 상수는 PacketId enum 이 있을 때만 의미가 있다.
        // (여러 생성 헤더를 한 TU 에 넣어도 중복 정의가 나지 않게 한다.)
        if (schema.FindPacketIdEnum() is null) return;

        Line(Banner("Packet Header: [2B packetId][4B bodyLength]"));
        Line("constexpr size_t PACKET_HEADER_SIZE = 6;");
        Line();
    }

    // ── struct / union alias ──────────────────────────────────────────────────

    void EmitDeclarations()
    {
        if (emitOrder.Count == 0) return;

        Line(Banner("Packet Structs"));
        Line("// Member order MUST match C# [MemoryPackable] declaration order.");
        Line();

        foreach (var node in emitOrder)
        {
            switch (node)
            {
                case PackableType t: EmitStruct(t); break;
                case UnionDef u: EmitUnionAlias(u); break;
            }
        }
    }

    void EmitStruct(PackableType t)
    {
        if (t.Pack1) Line("#pragma pack(push, 1)");
        Line($"struct {t.Name} {{");

        var consts = new HashSet<string>(StringComparer.Ordinal);
        foreach (var m in t.Members)
            if (m.Annotation is FixedArrayAnnotation fa && consts.Add(fa.ConstName))
                Line($"    static constexpr int32_t {fa.ConstName} = {fa.Size};");
        if (consts.Count > 0) Line();

        // C 고정 배열은 원소 타입과 count 변수의 int32_t 도 정렬 폭에 넣는다.
        var width = t.Members
            .Select(m => m.Annotation is FixedArrayAnnotation
                ? Math.Max(m.ElementCppType.Length, "int32_t".Length)
                : m.CppType.Length)
            .DefaultIfEmpty(0)
            .Max();

        foreach (var m in t.Members)
        {
            if (m.Annotation is FixedArrayAnnotation fa)
            {
                Line($"    {m.ElementCppType.PadRight(width)} {m.CppName}[{fa.ConstName}] = {{}};");
                Line($"    {"int32_t".PadRight(width)} {CsParser.DeriveCountName(m.CppName)} = 0;");
                continue;
            }
            var init = m.DefaultValue is null ? "" : $" = {m.DefaultValue}";
            Line($"    {m.CppType.PadRight(width)} {m.CppName}{init};");
        }

        Line("};");
        if (t.Pack1) Line("#pragma pack(pop)");
        Line();
    }

    void EmitUnionAlias(UnionDef u)
    {
        var alts = u.Alternatives.OrderBy(a => a.Tag).Select(a => a.TypeName);
        Line($"using {u.Name} = std::variant<{string.Join(", ", alts)}>;");
        Line();
    }

    // ── 직렬화 정의 ───────────────────────────────────────────────────────────

    void EmitSerializers()
    {
        var unmanaged = emitOrder.OfType<PackableType>().Where(t => t.IsUnmanaged).ToList();
        var unions = emitOrder.OfType<UnionDef>().ToList();
        var serializable = emitOrder.OfType<PackableType>().Where(t => !t.IsUnmanaged).ToList();

        var macroTypes = new List<PackableType>();
        var explicitTypes = new List<PackableType>();
        foreach (var t in serializable)
        {
            if (options.Style == GeneratorStyle.Explicit || t.RequiresExplicit) explicitTypes.Add(t);
            else macroTypes.Add(t);
        }

        // 매크로 한 줄도 없으면 배너를 생략하고 바로 특수화 블록으로 간다.
        if (unmanaged.Count > 0 || unions.Count > 0 || macroTypes.Count > 0)
            Line(Banner("Serializer definitions"));

        if (unmanaged.Count > 0)
        {
            // Pack=1 has no padding by construction, so EXACT's compile-time
            // proof always holds - use it for the zero-cost guarantee. Anything
            // else MIGHT have padding between members (natural alignment), so
            // generated code always reaches for SCRUBBED rather than trying to
            // compute whether padding actually exists: a small, structural
            // safety margin is worth more here than shaving a memcpy off types
            // that happen to pack tightly without a Pack=1 attribute.
            foreach (var t in unmanaged)
            {
                // An empty struct (C# size 1, no fields) has no padding question
                // to begin with, and the member-listing macros require at least
                // one member - fall back to the plain, unchecked macro rather
                // than emit a malformed empty argument list.
                if (t.Members.Count == 0)
                {
                    Line($"MEMORYPACK_UNMANAGED({Qualified(t.Name)}, {t.UnmanagedSize})");
                    continue;
                }
                var members = string.Join(", ", t.Members.Select(m => m.CppName));
                var macro = t.Pack1 ? "MEMORYPACK_UNMANAGED_EXACT" : "MEMORYPACK_UNMANAGED_SCRUBBED";
                Line($"{macro}({Qualified(t.Name)}, {t.UnmanagedSize}, {members})");
            }
            Line();
        }

        if (unions.Count > 0)
        {
            foreach (var u in unions)
                foreach (var (tag, typeName) in u.Alternatives.OrderBy(a => a.Tag))
                    Line($"MEMORYPACK_UNION_TAG({Qualified(typeName)}, {tag})");
            Line();
        }

        foreach (var t in macroTypes)
            EmitMacroDefine(t);
        if (macroTypes.Count > 0) Line();

        if (explicitTypes.Count > 0)
        {
            Line(Banner("IMemoryPackable Specializations"));
            Line("namespace memorypack {");
            Line();
            for (var i = 0; i < explicitTypes.Count; i++)
            {
                EmitExplicit(explicitTypes[i]);
                if (i < explicitTypes.Count - 1) Line();
            }
            Line();
            Line("} // namespace memorypack");
            Line();
        }
    }

    void EmitMacroDefine(PackableType t)
    {
        if (t.Members.Count == 0)
        {
            Line($"MEMORYPACK_DEFINE_EMPTY({Qualified(t.Name)})");
            return;
        }

        const string open = "MEMORYPACK_DEFINE(";
        var indent = new string(' ', open.Length);
        var parts = new List<string> { Qualified(t.Name) };
        parts.AddRange(t.Members.Select(m => m.CppName));

        var line = new StringBuilder(open);
        var lines = new List<string>();
        for (var i = 0; i < parts.Count; i++)
        {
            var piece = parts[i] + (i < parts.Count - 1 ? ", " : ")");
            if (line.Length > open.Length && line.Length + piece.Length > 100)
            {
                lines.Add(line.ToString().TrimEnd());
                line = new StringBuilder(indent);
            }
            line.Append(piece);
        }
        lines.Add(line.ToString());
        foreach (var l in lines) Line(l);
    }

    void EmitExplicit(PackableType t)
    {
        var name = Qualified(t.Name);
        Line($"// --- {t.Name} (memberCount={t.Members.Count}) ---");
        Line($"template<> struct IMemoryPackable<{name}> {{");

        if (t.VersionTolerant)
        {
            Line($"    static void Serialize(MemoryPackWriter& w, const {name}* v) {{");
            Line("        if (!v) { w.WriteNullObjectHeader(); return; }");
            Line("        VersionTolerantWriter vt(w);");
            foreach (var m in t.Members)
                Line($"        vt.WriteMember(v->{m.CppName});");
            Line("    }");
            Line($"    static void Deserialize(MemoryPackReader& r, {name}& v) {{");
            Line("        VersionTolerantReader vt(r);");
            Line("        if (vt.IsNull()) return;");
            foreach (var m in t.Members)
                Line($"        vt.ReadMember(v.{m.CppName});");
            Line("        vt.Finish();");
            Line("    }");
            Line("};");
            return;
        }

        Line($"    static void Serialize(MemoryPackWriter& w, const {name}* v) {{");
        Line("        if (!v) { w.WriteNullObjectHeader(); return; }");
        Line($"        w.WriteObjectHeader({t.Members.Count});");
        foreach (var m in t.Members)
            Line("        " + SerializeExpr(m));
        Line("    }");

        Line($"    static void Deserialize(MemoryPackReader& r, {name}& v) {{");
        if (t.Members.Count == 0)
        {
            Line("        (void)v;");
            Line("        r.ReadObjectHeader();");
        }
        else
        {
            Line("        auto [cnt, isNull] = r.ReadObjectHeader();");
            Line("        if (isNull) return;");
            EmitDeserializeLines(t);
        }
        Line("    }");
        Line("};");
    }

    static string SerializeExpr(MemberDef m)
    {
        var v = $"v->{m.CppName}";
        return m.Emit switch
        {
            EmitKind.FixedArray => $"w.WriteArray({v}, v->{CsParser.DeriveCountName(m.CppName)});",
            EmitKind.StdArray => $"w.WriteArray({v});",
            EmitKind.Bool => $"w.WriteBool({v});",
            EmitKind.Int8 => $"w.WriteInt8({v});",
            EmitKind.UInt8 => $"w.WriteUInt8({v});",
            EmitKind.Int16 => $"w.WriteInt16({v});",
            EmitKind.UInt16 => $"w.WriteUInt16({v});",
            EmitKind.Int32 => $"w.WriteInt32({v});",
            EmitKind.UInt32 => $"w.WriteUInt32({v});",
            EmitKind.Int64 => $"w.WriteInt64({v});",
            EmitKind.UInt64 => $"w.WriteUInt64({v});",
            EmitKind.Float => $"w.WriteFloat({v});",
            EmitKind.Double => $"w.WriteDouble({v});",
            EmitKind.String => $"w.WriteString({v});",
            EmitKind.OptionalString => $"w.WriteOptionalString({v});",
            EmitKind.VectorArith => $"w.WriteVector({v});",
            EmitKind.VectorString => $"w.WriteStringVector({v});",
            _ => $"w.Write({v});",
        };
    }

    /// <summary>역직렬화 한 줄: 대입식은 좌변 이름을 그룹별로 정렬해 준다.</summary>
    void EmitDeserializeLines(PackableType t)
    {
        var qualified = Qualified(t.Name);
        var plain = new List<(int Index, string Lhs, string Rhs)>();
        var strings = new List<(int Index, string Lhs)>();
        var statements = new List<(int Index, string Text)>();

        for (var i = 0; i < t.Members.Count; i++)
        {
            var m = t.Members[i];
            switch (m.Emit)
            {
                case EmitKind.FixedArray:
                {
                    var fa = (FixedArrayAnnotation)m.Annotation!;
                    plain.Add((i, CsParser.DeriveCountName(m.CppName),
                        $"r.ReadArray(v.{m.CppName}, {qualified}::{fa.ConstName});"));
                    break;
                }
                case EmitKind.StdArray:
                {
                    var sa = (StdArrayAnnotation)m.Annotation!;
                    plain.Add((i, m.CppName, $"r.ReadArray<{m.ElementCppType}, {sa.Size}>();"));
                    break;
                }
                case EmitKind.String:
                    strings.Add((i, m.CppName));
                    break;
                case EmitKind.OptionalString:
                    plain.Add((i, m.CppName, "r.ReadString();"));
                    break;
                case EmitKind.VectorArith:
                    plain.Add((i, m.CppName, $"r.ReadVector<{m.ElementCppType}>();"));
                    break;
                case EmitKind.VectorString:
                    plain.Add((i, m.CppName, "r.ReadStringVector();"));
                    break;
                case EmitKind.Generic:
                    statements.Add((i, $"r.Read(v.{m.CppName});"));
                    break;
                default:
                    plain.Add((i, m.CppName, $"r.{ReadMethod(m.Emit)}();"));
                    break;
            }
        }

        var plainWidth = plain.Count > 0 ? plain.Max(p => p.Lhs.Length) : 0;
        var stringWidth = strings.Count > 0 ? strings.Max(s => s.Lhs.Length) : 0;

        var rendered = new SortedDictionary<int, string>();
        foreach (var (i, lhs, rhs) in plain)
            rendered[i] = $"        if (cnt >= {i + 1}) v.{lhs.PadRight(plainWidth)} = {rhs}";
        foreach (var (i, lhs) in strings)
            rendered[i] = $"        if (cnt >= {i + 1}) {{ auto s = r.ReadString(); v.{lhs.PadRight(stringWidth)} = s.value_or(\"\"); }}";
        foreach (var (i, text) in statements)
            rendered[i] = $"        if (cnt >= {i + 1}) {text}";

        foreach (var line in rendered.Values) Line(line);
    }

    static string ReadMethod(EmitKind kind) => kind switch
    {
        EmitKind.Bool => "ReadBool",
        EmitKind.Int8 => "ReadInt8",
        EmitKind.UInt8 => "ReadUInt8",
        EmitKind.Int16 => "ReadInt16",
        EmitKind.UInt16 => "ReadUInt16",
        EmitKind.Int32 => "ReadInt32",
        EmitKind.UInt32 => "ReadUInt32",
        EmitKind.Int64 => "ReadInt64",
        EmitKind.UInt64 => "ReadUInt64",
        EmitKind.Float => "ReadFloat",
        EmitKind.Double => "ReadDouble",
        _ => "Read",
    };

    // ── 디스패치 테이블 + 스키마 해시 ─────────────────────────────────────────

    void EmitDispatch()
    {
        if (!DispatchEnabled) return;

        var e = schema.FindPacketIdEnum()!;
        var ns = options.Namespace;
        if (!string.IsNullOrEmpty(ns)) { Line($"namespace {ns} {{"); Line(); }

        Line(Banner("Packet dispatch table"));
        Line("template<typename Handler>");
        Line($"bool DispatchPacket({e.Name} id, std::span<const uint8_t> body, Handler&& handler) {{");
        Line("    switch (id) {");
        foreach (var m in e.Members)
        {
            if (!typeByName.TryGetValue(m.Name, out var t)) continue;
            if (t.IsUnmanaged) continue;
            Line($"    case {e.Name}::{m.Name}: {{ auto v = memorypack::Deserialize<{t.Name}>(body); handler(v); return true; }}");
        }
        Line("    default: return false;");
        Line("    }");
        Line("}");
        Line();

        Line(Banner("Schema hash"));
        Line("// FNV-1a 64bit over \"Type(0:CsType;1:CsType;...)\\n\" for every packet.");
        Line($"inline constexpr uint64_t PACKET_SCHEMA_HASH = 0x{SchemaHashValue:X16}ULL;");
        Line();

        if (!string.IsNullOrEmpty(ns)) Line($"}} // namespace {ns}");
    }

    /// <summary>--emit-schema-hash-cs 로 내보낼 C# 파일 내용.</summary>
    public string GenerateSchemaHashCs()
    {
        var cs = new StringBuilder();
        cs.Append("// <auto-generated> cs2cpp\n");
        cs.Append("// C++ 헤더의 PACKET_SCHEMA_HASH 와 같은 값입니다.\n");
        if (!string.IsNullOrEmpty(schema.CsNamespace))
            cs.Append($"namespace {schema.CsNamespace};\n\n");
        cs.Append("public static class PacketSchema\n");
        cs.Append("{\n");
        cs.Append($"    public const ulong PacketSchemaHash = 0x{SchemaHashValue:X16}UL;\n");
        cs.Append("}\n");
        return cs.ToString();
    }
}
