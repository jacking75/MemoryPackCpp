namespace Cs2Cpp;

/// <summary>IMemoryPackable 코드를 어떤 형태로 뽑을지.</summary>
public enum GeneratorStyle
{
    /// <summary>MEMORYPACK_DEFINE(Type, a, b, ...) 매크로 (기본값).</summary>
    Macro,
    /// <summary>template&lt;&gt; struct IMemoryPackable&lt;T&gt; 전체 특수화 (레거시).</summary>
    Explicit,
}

public enum UserTypeKind
{
    Class,
    Struct,
    Interface,
}

/// <summary>멤버 하나를 C++로 어떻게 읽고 쓸지 (explicit 스타일에서만 사용).</summary>
public enum EmitKind
{
    /// <summary>w.Write(v-&gt;x) / r.Read(v.x) — 제네릭 디스패치.</summary>
    Generic,
    Bool,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float,
    Double,
    /// <summary>std::string.</summary>
    String,
    /// <summary>std::optional&lt;std::string&gt;.</summary>
    OptionalString,
    /// <summary>std::vector&lt;arithmetic&gt;.</summary>
    VectorArith,
    /// <summary>std::vector&lt;std::string&gt;.</summary>
    VectorString,
    /// <summary>// [cpp:fixed_array(N, NAME)]</summary>
    FixedArray,
    /// <summary>// [cpp:std_array(N)]</summary>
    StdArray,
}

public sealed record EnumMemberDef(string Name, long Value);

public sealed class EnumDef
{
    public required string Name { get; init; }
    public required string CsBaseType { get; init; }
    public required string CppBaseType { get; init; }
    public List<EnumMemberDef> Members { get; } = [];
}

public abstract record CppAnnotation;

/// <summary>C 스타일 고정 배열 + count 추적 변수.</summary>
public sealed record FixedArrayAnnotation(int Size, string ConstName) : CppAnnotation;

/// <summary>std::array&lt;T, N&gt;.</summary>
public sealed record StdArrayAnnotation(int Size) : CppAnnotation;

public sealed class MemberDef
{
    public required string CsName { get; init; }
    public required string CppName { get; init; }
    /// <summary>공백을 제거한 원본 C# 타입 (스키마 해시에 사용).</summary>
    public required string CsType { get; init; }
    public required string CppType { get; init; }
    /// <summary>vector / 배열의 원소 C++ 타입.</summary>
    public string ElementCppType { get; init; } = "";
    public CppAnnotation? Annotation { get; init; }
    public EmitKind Emit { get; init; } = EmitKind.Generic;
    /// <summary>구조체 필드 초기화 값 (없으면 null).</summary>
    public string? DefaultValue { get; init; }
    /// <summary>[MemoryPackOrder(n)] 값, 없으면 -1.</summary>
    public int Order { get; init; } = -1;
    public int DeclIndex { get; init; }
    /// <summary>이 멤버가 참조하는 사용자 정의 타입 이름들 (선언 순서 정렬용).</summary>
    public List<string> ReferencedTypes { get; } = [];
}

public sealed class PackableType
{
    public required string Name { get; init; }
    public UserTypeKind Kind { get; init; }
    public bool VersionTolerant { get; init; }
    /// <summary>참조 타입 필드가 없는 C# struct — 오브젝트 헤더 없이 통째로 복사된다.</summary>
    public bool IsUnmanaged { get; set; }
    /// <summary>.NET sequential layout 기준으로 계산한 바이트 크기.</summary>
    public int UnmanagedSize { get; set; }
    /// <summary>[StructLayout(LayoutKind.Sequential, Pack = 1)]</summary>
    public bool Pack1 { get; set; }
    public List<MemberDef> Members { get; } = [];

    /// <summary>매크로(MEMORYPACK_DEFINE)로는 표현할 수 없어 항상 explicit 코드가 필요한지.</summary>
    public bool RequiresExplicit =>
        VersionTolerant || Members.Any(m => m.Annotation is not null);
}

public sealed class UnionDef
{
    public required string Name { get; init; }
    public List<(int Tag, string TypeName)> Alternatives { get; } = [];
}

/// <summary>입력 .cs 파일 하나를 파싱한 결과.</summary>
public sealed class Schema
{
    public string? CsNamespace { get; set; }
    public List<EnumDef> Enums { get; } = [];
    public List<PackableType> Types { get; } = [];
    public List<UnionDef> Unions { get; } = [];
    public List<string> Warnings { get; } = [];

    public EnumDef? FindPacketIdEnum() =>
        Enums.FirstOrDefault(e =>
            e.Name == "PacketId" || e.Name.EndsWith("PacketId", StringComparison.Ordinal));
}
