using System.Text;
using System.Text.RegularExpressions;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Cs2Cpp;

/// <summary>
/// Roslyn 기반 C# 파서. `[MemoryPackable]` 타입, `[MemoryPackUnion]` 유니온,
/// enum 선언을 읽어 <see cref="Schema"/>로 변환한다.
/// </summary>
public sealed class CsParser(bool nullableStrings = false)
{
    // ── 어노테이션 주석 패턴 ──────────────────────────────────────────────────
    static readonly Regex FixedArrayPattern =
        new(@"\[cpp:fixed_array\(\s*(\d+)\s*,\s*(\w+)\s*\)\]", RegexOptions.Compiled);
    static readonly Regex StdArrayPattern =
        new(@"\[cpp:std_array\(\s*(\d+)\s*\)\]", RegexOptions.Compiled);

    // ── 파싱 중간 표현 ────────────────────────────────────────────────────────
    sealed class RawMember
    {
        public required string CsName { get; init; }
        public required TypeSyntax Type { get; init; }
        public int Order { get; init; } = -1;
        public CppAnnotation? Annotation { get; init; }
        public int DeclIndex { get; init; }
    }

    sealed class RawType
    {
        public required string Name { get; init; }
        public UserTypeKind Kind { get; init; }
        public bool VersionTolerant { get; init; }
        public bool Pack1 { get; init; }
        public List<RawMember> Members { get; } = [];
    }

    readonly Dictionary<string, EnumDef> enums = [];
    readonly Dictionary<string, RawType> rawTypes = [];
    readonly Dictionary<string, UnionDef> unions = [];
    readonly List<string> typeOrder = [];
    readonly List<string> warnings = [];

    /// <summary>C# 소스 텍스트 하나를 파싱한다.</summary>
    public Schema Parse(string source, string filePath = "<memory>")
    {
        enums.Clear();
        rawTypes.Clear();
        unions.Clear();
        typeOrder.Clear();
        warnings.Clear();

        var tree = CSharpSyntaxTree.ParseText(source, path: filePath);
        var root = tree.GetCompilationUnitRoot();

        var schema = new Schema { CsNamespace = FindNamespace(root) };

        foreach (var e in root.DescendantNodes().OfType<EnumDeclarationSyntax>())
            CollectEnum(e);

        foreach (var t in root.DescendantNodes().OfType<TypeDeclarationSyntax>())
            CollectType(t);

        // ── 해석 단계 ─────────────────────────────────────────────────────────
        var layout = new UnmanagedLayout(this);
        var packables = new Dictionary<string, PackableType>();

        foreach (var name in typeOrder)
        {
            var raw = rawTypes[name];
            var packable = new PackableType
            {
                Name = raw.Name,
                Kind = raw.Kind,
                VersionTolerant = raw.VersionTolerant,
                Pack1 = raw.Pack1,
            };
            if (raw.Kind == UserTypeKind.Struct && layout.TryGetSize(raw.Name, out var size))
            {
                packable.IsUnmanaged = true;
                packable.UnmanagedSize = size;
            }
            packables[name] = packable;
        }

        var mapper = new TypeMapper(enums, packables, unions, nullableStrings);

        foreach (var name in typeOrder)
        {
            var raw = rawTypes[name];
            var packable = packables[name];
            foreach (var member in OrderMembers(raw.Members))
            {
                var mapped = mapper.Map(member.Type, member.Annotation);
                if (mapped.Warning is not null)
                    warnings.Add($"{raw.Name}.{member.CsName}: {mapped.Warning}");

                var def = new MemberDef
                {
                    CsName = member.CsName,
                    CppName = CppNaming.Escape(PascalToCamel(member.CsName)),
                    CsType = NormalizeCsType(member.Type),
                    CppType = mapped.CppType,
                    ElementCppType = mapped.ElementCppType,
                    Annotation = member.Annotation,
                    Emit = mapped.Emit,
                    DefaultValue = mapped.DefaultValue,
                    Order = member.Order,
                    DeclIndex = member.DeclIndex,
                };
                def.ReferencedTypes.AddRange(mapped.ReferencedTypes);
                packable.Members.Add(def);
            }
            schema.Types.Add(packable);
        }

        schema.Enums.AddRange(enums.Values);
        schema.Unions.AddRange(unions.Values);
        schema.Warnings.AddRange(warnings);
        return schema;
    }

    // ── 네임스페이스 ──────────────────────────────────────────────────────────

    static string? FindNamespace(CompilationUnitSyntax root)
    {
        // 가장 안쪽 네임스페이스를 고른 뒤 바깥부터 이어 붙인다 (block form 중첩 지원).
        var ns = root.DescendantNodes()
            .OfType<BaseNamespaceDeclarationSyntax>()
            .OrderByDescending(n => n.Ancestors().OfType<BaseNamespaceDeclarationSyntax>().Count())
            .FirstOrDefault();
        if (ns is null) return null;

        var parts = new List<string> { ns.Name.ToString() };
        for (var parent = ns.Parent; parent is not null; parent = parent.Parent)
            if (parent is BaseNamespaceDeclarationSyntax outer)
                parts.Insert(0, outer.Name.ToString());
        return string.Join(".", parts);
    }

    // ── enum ──────────────────────────────────────────────────────────────────

    void CollectEnum(EnumDeclarationSyntax node)
    {
        var csBase = node.BaseList?.Types.FirstOrDefault()?.Type.ToString() ?? "int";
        var def = new EnumDef
        {
            Name = node.Identifier.ValueText,
            CsBaseType = csBase,
            CppBaseType = TypeMapper.EnumBaseToCpp(csBase),
        };

        long next = 0;
        var byName = new Dictionary<string, long>(StringComparer.Ordinal);
        foreach (var m in node.Members)
        {
            long value;
            if (m.EqualsValue is not null)
            {
                if (TryEvalConstant(m.EqualsValue.Value, byName, out value))
                {
                    next = value + 1;
                }
                else
                {
                    warnings.Add($"enum {def.Name}.{m.Identifier.ValueText}: 상수 값을 계산할 수 없어 {next}로 대체합니다.");
                    value = next++;
                }
            }
            else
            {
                value = next++;
            }
            byName[m.Identifier.ValueText] = value;
            def.Members.Add(new EnumMemberDef(CppNaming.Escape(m.Identifier.ValueText), value));
        }

        enums[def.Name] = def;
    }

    /// <summary>enum 값 계산기: 리터럴, 단항 +/-/~, 이전 멤버 참조, 기본 산술/비트 연산.</summary>
    static bool TryEvalConstant(ExpressionSyntax expr, Dictionary<string, long> byName, out long value)
    {
        switch (expr)
        {
            case LiteralExpressionSyntax lit when lit.Token.Value is not null:
                try
                {
                    value = Convert.ToInt64(lit.Token.Value);
                    return true;
                }
                catch (Exception e) when (e is OverflowException or InvalidCastException or FormatException)
                {
                    value = 0;
                    return false;
                }
            case PrefixUnaryExpressionSyntax prefix when TryEvalConstant(prefix.Operand, byName, out var inner):
                value = prefix.OperatorToken.Kind() switch
                {
                    SyntaxKind.MinusToken => -inner,
                    SyntaxKind.PlusToken => inner,
                    SyntaxKind.TildeToken => ~inner,
                    _ => 0,
                };
                return prefix.OperatorToken.IsKind(SyntaxKind.MinusToken)
                    || prefix.OperatorToken.IsKind(SyntaxKind.PlusToken)
                    || prefix.OperatorToken.IsKind(SyntaxKind.TildeToken);
            case IdentifierNameSyntax id when byName.TryGetValue(id.Identifier.ValueText, out var known):
                value = known;
                return true;
            case ParenthesizedExpressionSyntax paren:
                return TryEvalConstant(paren.Expression, byName, out value);
            case BinaryExpressionSyntax bin
                when TryEvalConstant(bin.Left, byName, out var l) && TryEvalConstant(bin.Right, byName, out var r):
                switch (bin.OperatorToken.Kind())
                {
                    case SyntaxKind.BarToken: value = l | r; return true;
                    case SyntaxKind.AmpersandToken: value = l & r; return true;
                    case SyntaxKind.LessThanLessThanToken: value = l << (int)r; return true;
                    case SyntaxKind.GreaterThanGreaterThanToken: value = l >> (int)r; return true;
                    case SyntaxKind.PlusToken: value = l + r; return true;
                    case SyntaxKind.MinusToken: value = l - r; return true;
                    case SyntaxKind.AsteriskToken: value = l * r; return true;
                    default: value = 0; return false;
                }
            default:
                value = 0;
                return false;
        }
    }

    // ── 타입 선언 ─────────────────────────────────────────────────────────────

    void CollectType(TypeDeclarationSyntax node)
    {
        var unionAttrs = FindAttributes(node.AttributeLists, "MemoryPackUnion").ToList();
        if (unionAttrs.Count > 0)
        {
            CollectUnion(node, unionAttrs);
            return;
        }

        var packable = FindAttributes(node.AttributeLists, "MemoryPackable").FirstOrDefault();
        if (packable is null) return;

        var kind = node switch
        {
            StructDeclarationSyntax => UserTypeKind.Struct,
            RecordDeclarationSyntax rec when rec.ClassOrStructKeyword.IsKind(SyntaxKind.StructKeyword)
                => UserTypeKind.Struct,
            InterfaceDeclarationSyntax => UserTypeKind.Interface,
            _ => UserTypeKind.Class,
        };
        if (kind == UserTypeKind.Interface) return;   // 유니온 없는 인터페이스는 무시

        var name = node.Identifier.ValueText;
        var raw = new RawType
        {
            Name = name,
            Kind = kind,
            VersionTolerant = AttributeMentions(packable, "VersionTolerant"),
            Pack1 = HasPack1(node),
        };

        var index = 0;

        // positional record: record Foo(int A, string B)
        if (node is RecordDeclarationSyntax { ParameterList: { } parameters })
        {
            foreach (var p in parameters.Parameters)
            {
                if (p.Type is null) continue;
                if (FindAttributes(p.AttributeLists, "MemoryPackIgnore").Any()) continue;
                raw.Members.Add(new RawMember
                {
                    CsName = p.Identifier.ValueText,
                    Type = p.Type,
                    Order = ReadOrder(p.AttributeLists),
                    Annotation = ReadAnnotation(p),
                    DeclIndex = index++,
                });
            }
        }

        foreach (var member in node.Members)
        {
            switch (member)
            {
                case PropertyDeclarationSyntax prop when IsSerializableProperty(prop):
                    if (FindAttributes(prop.AttributeLists, "MemoryPackIgnore").Any()) continue;
                    raw.Members.Add(new RawMember
                    {
                        CsName = prop.Identifier.ValueText,
                        Type = prop.Type,
                        Order = ReadOrder(prop.AttributeLists),
                        Annotation = ReadAnnotation(prop),
                        DeclIndex = index++,
                    });
                    break;

                case FieldDeclarationSyntax field when IsSerializableField(field):
                    if (FindAttributes(field.AttributeLists, "MemoryPackIgnore").Any()) continue;
                    var order = ReadOrder(field.AttributeLists);
                    var annotation = ReadAnnotation(field);
                    foreach (var v in field.Declaration.Variables)
                    {
                        raw.Members.Add(new RawMember
                        {
                            CsName = v.Identifier.ValueText,
                            Type = field.Declaration.Type,
                            Order = order,
                            Annotation = annotation,
                            DeclIndex = index++,
                        });
                    }
                    break;
            }
        }

        if (rawTypes.TryAdd(name, raw))
            typeOrder.Add(name);
        else
            warnings.Add($"타입 {name}이(가) 중복 선언되어 첫 번째 선언만 사용합니다.");
    }

    void CollectUnion(TypeDeclarationSyntax node, List<AttributeSyntax> unionAttrs)
    {
        var def = new UnionDef { Name = node.Identifier.ValueText };
        foreach (var attr in unionAttrs)
        {
            var args = attr.ArgumentList?.Arguments;
            if (args is null || args.Value.Count < 2) continue;

            if (!TryEvalConstant(args.Value[0].Expression, [], out var tag)) continue;

            var typeArg = args.Value[1].Expression;
            if (typeArg is not TypeOfExpressionSyntax typeOf) continue;

            def.Alternatives.Add(((int)tag, SimpleName(typeOf.Type)));
        }
        if (def.Alternatives.Count > 0)
            unions[def.Name] = def;
    }

    static bool IsSerializableProperty(PropertyDeclarationSyntax prop)
    {
        if (!prop.Modifiers.Any(SyntaxKind.PublicKeyword)) return false;
        if (prop.Modifiers.Any(SyntaxKind.StaticKeyword)) return false;
        if (prop.AccessorList is null) return false;        // expression-bodied

        var accessors = prop.AccessorList.Accessors;
        var getter = accessors.FirstOrDefault(a => a.IsKind(SyntaxKind.GetAccessorDeclaration));
        if (getter is null) return false;
        // 자동 프로퍼티만: 접근자에 본문이 없어야 한다.
        return accessors.All(a => a.Body is null && a.ExpressionBody is null);
    }

    static bool IsSerializableField(FieldDeclarationSyntax field)
    {
        if (!field.Modifiers.Any(SyntaxKind.PublicKeyword)) return false;
        if (field.Modifiers.Any(SyntaxKind.StaticKeyword)) return false;
        if (field.Modifiers.Any(SyntaxKind.ConstKeyword)) return false;
        return true;
    }

    static bool HasPack1(TypeDeclarationSyntax node)
    {
        foreach (var attr in FindAttributes(node.AttributeLists, "StructLayout"))
        {
            var args = attr.ArgumentList?.Arguments;
            if (args is null) continue;
            foreach (var a in args)
            {
                if (a.NameEquals?.Name.Identifier.ValueText != "Pack") continue;
                if (a.Expression is LiteralExpressionSyntax { Token.Value: int p } && p == 1) return true;
            }
        }
        return false;
    }

    static int ReadOrder(SyntaxList<AttributeListSyntax> lists)
    {
        var attr = FindAttributes(lists, "MemoryPackOrder").FirstOrDefault();
        var arg = attr?.ArgumentList?.Arguments.FirstOrDefault();
        if (arg is null) return -1;
        return TryEvalConstant(arg.Expression, [], out var v) ? (int)v : -1;
    }

    static IEnumerable<AttributeSyntax> FindAttributes(SyntaxList<AttributeListSyntax> lists, string name) =>
        lists.SelectMany(l => l.Attributes).Where(a => AttributeNameMatches(a, name));

    static bool AttributeNameMatches(AttributeSyntax attr, string name)
    {
        var text = SimpleName(attr.Name);
        if (text.EndsWith("Attribute", StringComparison.Ordinal))
            text = text[..^"Attribute".Length];
        return text == name;
    }

    /// <summary>정규화된 타입 이름에서 마지막 식별자만 뽑는다 (System.Guid → Guid).</summary>
    public static string SimpleName(TypeSyntax type) => type switch
    {
        QualifiedNameSyntax q => SimpleName(q.Right),
        AliasQualifiedNameSyntax a => SimpleName(a.Name),
        GenericNameSyntax g => g.Identifier.ValueText,
        SimpleNameSyntax s => s.Identifier.ValueText,
        _ => type.ToString(),
    };

    static bool AttributeMentions(AttributeSyntax attr, string token) =>
        attr.ArgumentList?.Arguments.Any(a => a.ToString().Contains(token, StringComparison.Ordinal)) ?? false;

    /// <summary>
    /// 멤버 선언에 붙은 주석에서 [cpp:...] 어노테이션을 찾는다.
    /// 선행 주석은 물론 특성([...])과 프로퍼티 사이, 줄 끝 주석도 인식한다.
    /// </summary>
    static CppAnnotation? ReadAnnotation(SyntaxNode member) =>
        ReadAnnotation(member.GetLeadingTrivia().Concat(member.DescendantTrivia()));

    static CppAnnotation? ReadAnnotation(IEnumerable<SyntaxTrivia> trivia)
    {
        foreach (var t in trivia)
        {
            if (!t.IsKind(SyntaxKind.SingleLineCommentTrivia) &&
                !t.IsKind(SyntaxKind.MultiLineCommentTrivia)) continue;

            var text = t.ToString();
            var fa = FixedArrayPattern.Match(text);
            if (fa.Success)
                return new FixedArrayAnnotation(int.Parse(fa.Groups[1].Value), fa.Groups[2].Value);

            var sa = StdArrayPattern.Match(text);
            if (sa.Success)
                return new StdArrayAnnotation(int.Parse(sa.Groups[1].Value));
        }
        return null;
    }

    /// <summary>
    /// [MemoryPackOrder]가 하나라도 있으면 그 값 오름차순으로 먼저 배치하고,
    /// 지정이 없는 멤버는 선언 순서대로 그 뒤에 붙인다.
    /// </summary>
    static IEnumerable<RawMember> OrderMembers(List<RawMember> members)
    {
        if (!members.Any(m => m.Order >= 0)) return members;
        return members
            .OrderBy(m => m.Order >= 0 ? 0 : 1)
            .ThenBy(m => m.Order >= 0 ? m.Order : m.DeclIndex)
            .ToList();
    }

    // ── unmanaged struct 레이아웃 계산 ────────────────────────────────────────

    /// <summary>
    /// .NET sequential layout(자연 정렬)으로 struct 크기를 계산한다.
    /// 참조 타입 필드가 하나라도 있으면 unmanaged가 아니다.
    /// </summary>
    sealed class UnmanagedLayout(CsParser parser)
    {
        readonly Dictionary<string, (int Size, int Align)?> cache = [];
        readonly HashSet<string> visiting = [];

        public bool TryGetSize(string typeName, out int size)
        {
            var r = Resolve(typeName);
            size = r?.Size ?? 0;
            return r is not null;
        }

        (int Size, int Align)? Resolve(string typeName)
        {
            if (cache.TryGetValue(typeName, out var cached)) return cached;
            if (!visiting.Add(typeName)) return null;     // 순환 참조

            (int, int)? result = null;
            if (parser.rawTypes.TryGetValue(typeName, out var raw) && raw.Kind == UserTypeKind.Struct)
                result = Compute(raw);

            visiting.Remove(typeName);
            cache[typeName] = result;
            return result;
        }

        (int Size, int Align)? Compute(RawType raw)
        {
            var pack = raw.Pack1 ? 1 : 8;
            var offset = 0;
            var maxAlign = 1;

            foreach (var m in raw.Members)
            {
                var f = FieldLayout(m.Type);
                if (f is null) return null;              // 관리 타입 필드 → unmanaged 아님
                var (size, align) = f.Value;
                align = Math.Min(align, pack);
                offset = RoundUp(offset, align);
                offset += size;
                maxAlign = Math.Max(maxAlign, align);
            }
            if (raw.Members.Count == 0) return (1, 1);   // C# 빈 struct는 1바이트
            return (RoundUp(offset, maxAlign), maxAlign);
        }

        (int Size, int Align)? FieldLayout(TypeSyntax type)
        {
            if (type is NullableTypeSyntax) return null;  // Nullable<T>는 크기가 다르다 — 지원 제외
            if (type is ArrayTypeSyntax) return null;
            if (type is GenericNameSyntax) return null;

            var name = SimpleName(type);
            var primitive = TypeMapper.PrimitiveLayout(name);
            if (primitive is not null) return primitive;

            if (parser.enums.TryGetValue(name, out var e))
                return TypeMapper.PrimitiveLayout(e.CsBaseType);

            return Resolve(name);
        }

        static int RoundUp(int value, int align) => (value + align - 1) / align * align;
    }

    // ── 이름 변환 유틸 ────────────────────────────────────────────────────────

    static string NormalizeCsType(TypeSyntax type)
    {
        var sb = new StringBuilder();
        foreach (var c in type.ToString())
            if (!char.IsWhiteSpace(c)) sb.Append(c);
        return sb.ToString();
    }

    /// <summary>PascalCase → camelCase. 연속 대문자(ID, HTTPClient)도 처리한다.</summary>
    public static string PascalToCamel(string name)
    {
        if (string.IsNullOrEmpty(name)) return name;

        if (name.Length >= 2 && char.IsUpper(name[0]) && char.IsUpper(name[1]))
        {
            var i = 0;
            while (i < name.Length && char.IsUpper(name[i])) i++;
            if (i == name.Length) return name.ToLowerInvariant();
            return name[..(i - 1)].ToLowerInvariant() + name[(i - 1)..];
        }

        return char.ToLowerInvariant(name[0]) + name[1..];
    }

    /// <summary>
    /// C++ 배열 필드명에서 count 변수명을 유도한다.
    /// skillIds → skillCount, cooldowns → cooldownCount, tiles → tileCount
    /// </summary>
    public static string DeriveCountName(string cppName)
    {
        string baseName;
        if (cppName.EndsWith("Ids", StringComparison.Ordinal))
            baseName = cppName[..^3];
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
