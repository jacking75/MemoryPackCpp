using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Cs2Cpp;

/// <summary>한 멤버 타입을 C++로 옮긴 결과.</summary>
public sealed record MappedType
{
    public required string CppType { get; init; }
    public string ElementCppType { get; init; } = "";
    public EmitKind Emit { get; init; } = EmitKind.Generic;
    public string? DefaultValue { get; init; }
    public string? Warning { get; init; }
    public List<string> ReferencedTypes { get; } = [];
    /// <summary>&lt;string&gt;, &lt;vector&gt; 처럼 이 타입이 요구하는 표준 헤더.</summary>
    public HashSet<string> Includes { get; } = [];
}

/// <summary>C# 타입 → C++ 타입 매핑.</summary>
public sealed class TypeMapper(
    IReadOnlyDictionary<string, EnumDef> enums,
    IReadOnlyDictionary<string, PackableType> types,
    IReadOnlyDictionary<string, UnionDef> unions,
    bool nullableStrings)
{
    // ── 기본 타입 표 ──────────────────────────────────────────────────────────
    static readonly Dictionary<string, string> Primitives = new(StringComparer.Ordinal)
    {
        ["bool"] = "bool",
        ["Boolean"] = "bool",
        ["byte"] = "uint8_t",
        ["Byte"] = "uint8_t",
        ["sbyte"] = "int8_t",
        ["SByte"] = "int8_t",
        ["short"] = "int16_t",
        ["Int16"] = "int16_t",
        ["ushort"] = "uint16_t",
        ["UInt16"] = "uint16_t",
        ["int"] = "int32_t",
        ["Int32"] = "int32_t",
        ["uint"] = "uint32_t",
        ["UInt32"] = "uint32_t",
        ["long"] = "int64_t",
        ["Int64"] = "int64_t",
        ["ulong"] = "uint64_t",
        ["UInt64"] = "uint64_t",
        ["float"] = "float",
        ["Single"] = "float",
        ["double"] = "double",
        ["Double"] = "double",
        ["char"] = "char16_t",
        ["Char"] = "char16_t",
    };

    /// <summary>.NET 전용 타입 → memorypack:: 매핑 (모두 값 타입).</summary>
    static readonly Dictionary<string, string> DotNetTypes = new(StringComparer.Ordinal)
    {
        ["Guid"] = "memorypack::Guid",
        ["DateTime"] = "memorypack::DateTime",
        ["TimeSpan"] = "memorypack::TimeSpan",
        ["DateTimeOffset"] = "memorypack::DateTimeOffset",
        ["decimal"] = "memorypack::Decimal",
        ["Decimal"] = "memorypack::Decimal",
        ["Half"] = "memorypack::Half",
        ["Int128"] = "memorypack::Int128",
        ["UInt128"] = "memorypack::UInt128",
        ["Vector2"] = "memorypack::Vector2",
        ["Vector3"] = "memorypack::Vector3",
        ["Vector4"] = "memorypack::Vector4",
        ["Quaternion"] = "memorypack::Quaternion",
    };

    static readonly Dictionary<string, (int Size, int Align)> PrimitiveSizes = new(StringComparer.Ordinal)
    {
        ["bool"] = (1, 1),
        ["Boolean"] = (1, 1),
        ["byte"] = (1, 1),
        ["Byte"] = (1, 1),
        ["sbyte"] = (1, 1),
        ["SByte"] = (1, 1),
        ["short"] = (2, 2),
        ["Int16"] = (2, 2),
        ["ushort"] = (2, 2),
        ["UInt16"] = (2, 2),
        ["char"] = (2, 2),
        ["Char"] = (2, 2),
        ["int"] = (4, 4),
        ["Int32"] = (4, 4),
        ["uint"] = (4, 4),
        ["UInt32"] = (4, 4),
        ["float"] = (4, 4),
        ["Single"] = (4, 4),
        ["long"] = (8, 8),
        ["Int64"] = (8, 8),
        ["ulong"] = (8, 8),
        ["UInt64"] = (8, 8),
        ["double"] = (8, 8),
        ["Double"] = (8, 8),
    };

    static readonly Dictionary<string, EmitKind> ArithmeticEmit = new(StringComparer.Ordinal)
    {
        ["bool"] = EmitKind.Bool,
        ["uint8_t"] = EmitKind.UInt8,
        ["int8_t"] = EmitKind.Int8,
        ["int16_t"] = EmitKind.Int16,
        ["uint16_t"] = EmitKind.UInt16,
        ["int32_t"] = EmitKind.Int32,
        ["uint32_t"] = EmitKind.UInt32,
        ["int64_t"] = EmitKind.Int64,
        ["uint64_t"] = EmitKind.UInt64,
        ["float"] = EmitKind.Float,
        ["double"] = EmitKind.Double,
    };

    static readonly Dictionary<string, string> ArithmeticDefaults = new(StringComparer.Ordinal)
    {
        ["bool"] = "false",
        ["uint8_t"] = "0",
        ["int8_t"] = "0",
        ["int16_t"] = "0",
        ["uint16_t"] = "0",
        ["int32_t"] = "0",
        ["uint32_t"] = "0",
        ["int64_t"] = "0",
        ["uint64_t"] = "0",
        ["float"] = "0.f",
        ["double"] = "0.0",
        ["char16_t"] = "0",
    };

    static readonly HashSet<string> ListLike = new(StringComparer.Ordinal)
    {
        "List", "IList", "ICollection", "IEnumerable", "IReadOnlyList",
        "IReadOnlyCollection", "Collection", "ObservableCollection",
    };

    static readonly HashSet<string> DictLike = new(StringComparer.Ordinal)
    {
        "Dictionary", "IDictionary", "IReadOnlyDictionary", "SortedDictionary",
    };

    static readonly HashSet<string> SetLike = new(StringComparer.Ordinal)
    {
        "HashSet", "ISet", "IReadOnlySet", "SortedSet",
    };

    public static string EnumBaseToCpp(string csBase) =>
        Primitives.GetValueOrDefault(csBase.Trim(), "int32_t");

    public static (int Size, int Align)? PrimitiveLayout(string csName) =>
        PrimitiveSizes.TryGetValue(csName, out var v) ? v : null;

    /// <summary>C++ arithmetic 타입인지 (vector 벌크 복사 경로 판정용).</summary>
    static bool IsCppArithmetic(string cppType) => ArithmeticEmit.ContainsKey(cppType);

    public MappedType Map(TypeSyntax type, CppAnnotation? annotation)
    {
        var result = MapCore(type, topLevel: true);

        if (annotation is FixedArrayAnnotation)
        {
            if (!IsCppArithmetic(result.ElementCppType))
                return result with { Warning = "[cpp:fixed_array]는 산술 타입 컬렉션에만 쓸 수 있습니다." };
            var fixedResult = new MappedType
            {
                CppType = result.CppType,
                ElementCppType = result.ElementCppType,
                Emit = EmitKind.FixedArray,
            };
            return fixedResult;
        }

        if (annotation is StdArrayAnnotation sa)
        {
            if (!IsCppArithmetic(result.ElementCppType))
                return result with { Warning = "[cpp:std_array]는 산술 타입 컬렉션에만 쓸 수 있습니다." };
            var stdResult = new MappedType
            {
                CppType = $"std::array<{result.ElementCppType}, {sa.Size}>",
                ElementCppType = result.ElementCppType,
                Emit = EmitKind.StdArray,
                DefaultValue = "{}",
            };
            stdResult.Includes.Add("<array>");
            return stdResult;
        }

        return result;
    }

    MappedType MapCore(TypeSyntax type, bool topLevel)
    {
        switch (type)
        {
            case NullableTypeSyntax nullable:
                return MapNullable(nullable.ElementType, topLevel);

            case ArrayTypeSyntax array:
                return MapSequence(array.ElementType);

            case GenericNameSyntax generic:
                return MapGeneric(generic, topLevel);

            case QualifiedNameSyntax qualified:
                return MapCore(qualified.Right, topLevel);

            default:
                return MapNamed(CsParser.SimpleName(type), topLevel);
        }
    }

    MappedType MapNullable(TypeSyntax element, bool topLevel)
    {
        // string? / List<T>? / T[]? 는 C#의 nullable reference 표기일 뿐이다.
        if (element is ArrayTypeSyntax or GenericNameSyntax)
        {
            var seq = MapCore(element, topLevel);
            return seq;
        }

        var name = CsParser.SimpleName(element);
        if (name is "string" or "String")
            return MapString();

        var inner = MapCore(element, topLevel: false);
        if (inner.CppType.StartsWith("std::optional<", StringComparison.Ordinal))
            return inner;   // 유니온처럼 이미 optional 인 매핑

        var wrapped = new MappedType
        {
            CppType = $"std::optional<{inner.CppType}>",
            Emit = EmitKind.Generic,
        };
        wrapped.Includes.Add("<optional>");
        foreach (var i in inner.Includes) wrapped.Includes.Add(i);
        wrapped.ReferencedTypes.AddRange(inner.ReferencedTypes);
        return wrapped;
    }

    MappedType MapGeneric(GenericNameSyntax generic, bool topLevel)
    {
        var name = generic.Identifier.ValueText;
        var args = generic.TypeArgumentList.Arguments;

        if (name == "Nullable" && args.Count == 1)
            return MapNullable(args[0], topLevel);

        if (ListLike.Contains(name) && args.Count == 1)
            return MapSequence(args[0]);

        if (DictLike.Contains(name) && args.Count == 2)
        {
            var k = MapCore(args[0], topLevel: false);
            var v = MapCore(args[1], topLevel: false);
            var map = new MappedType { CppType = $"std::map<{k.CppType}, {v.CppType}>" };
            map.Includes.Add("<map>");
            Merge(map, k, v);
            return map;
        }

        if (SetLike.Contains(name) && args.Count == 1)
        {
            var e = MapCore(args[0], topLevel: false);
            var set = new MappedType { CppType = $"std::set<{e.CppType}>" };
            set.Includes.Add("<set>");
            Merge(set, e);
            return set;
        }

        if (name == "KeyValuePair" && args.Count == 2)
        {
            var k = MapCore(args[0], topLevel: false);
            var v = MapCore(args[1], topLevel: false);
            var pair = new MappedType { CppType = $"std::pair<{k.CppType}, {v.CppType}>", DefaultValue = "{}" };
            pair.Includes.Add("<utility>");
            Merge(pair, k, v);
            return pair;
        }

        return new MappedType
        {
            CppType = name,
            Warning = $"제네릭 타입 {generic}을(를) 매핑할 수 없어 이름을 그대로 사용합니다.",
        };
    }

    MappedType MapSequence(TypeSyntax element)
    {
        var e = MapCore(element, topLevel: false);
        var vec = new MappedType
        {
            CppType = $"std::vector<{e.CppType}>",
            ElementCppType = e.CppType,
            Emit = e.CppType == "std::string" ? EmitKind.VectorString
                 : IsCppArithmetic(e.CppType) ? EmitKind.VectorArith
                 : EmitKind.Generic,
        };
        vec.Includes.Add("<vector>");
        Merge(vec, e);
        return vec;
    }

    MappedType MapString()
    {
        if (!nullableStrings)
        {
            var plain = new MappedType { CppType = "std::string", Emit = EmitKind.String };
            plain.Includes.Add("<string>");
            return plain;
        }
        var opt = new MappedType { CppType = "std::optional<std::string>", Emit = EmitKind.OptionalString };
        opt.Includes.Add("<string>");
        opt.Includes.Add("<optional>");
        return opt;
    }

    MappedType MapNamed(string name, bool topLevel)
    {
        if (name is "string" or "String")
        {
            // 명시적으로 non-nullable 인 string 은 항상 std::string.
            var s = new MappedType { CppType = "std::string", Emit = EmitKind.String };
            s.Includes.Add("<string>");
            return s;
        }

        if (Primitives.TryGetValue(name, out var prim))
        {
            return new MappedType
            {
                CppType = prim,
                Emit = ArithmeticEmit.GetValueOrDefault(prim, EmitKind.Generic),
                DefaultValue = ArithmeticDefaults.GetValueOrDefault(prim),
            };
        }

        if (DotNetTypes.TryGetValue(name, out var dotnet))
            return new MappedType { CppType = dotnet };

        if (enums.ContainsKey(name))
        {
            var e = new MappedType { CppType = name, DefaultValue = "{}" };
            e.ReferencedTypes.Add(name);
            return e;
        }

        if (unions.ContainsKey(name))
        {
            // 유니온은 참조 타입 계층 → nullable union.
            var u = new MappedType { CppType = $"std::optional<{name}>" };
            u.Includes.Add("<optional>");
            u.Includes.Add("<variant>");
            u.ReferencedTypes.Add(name);
            return u;
        }

        if (types.TryGetValue(name, out var user))
        {
            if (user.Kind == UserTypeKind.Struct)
            {
                var s = new MappedType { CppType = name };
                s.ReferencedTypes.Add(name);
                return s;
            }
            // 참조 타입 멤버는 null 이 될 수 있으므로 std::optional.
            var c = new MappedType { CppType = topLevel ? $"std::optional<{name}>" : name };
            if (topLevel) c.Includes.Add("<optional>");
            c.ReferencedTypes.Add(name);
            return c;
        }

        return new MappedType
        {
            CppType = name,
            Warning = $"알 수 없는 타입 {name} — C++ 이름을 그대로 사용합니다.",
        };
    }

    static void Merge(MappedType target, params MappedType[] sources)
    {
        foreach (var s in sources)
        {
            foreach (var i in s.Includes) target.Includes.Add(i);
            target.ReferencedTypes.AddRange(s.ReferencedTypes);
        }
    }
}
