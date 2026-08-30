using System.Text;

namespace Cs2Cpp;

/// <summary>
/// 패킷 프로토콜 버전 비교용 스키마 해시.
///
/// 정규 문자열은 다음 형식이며, C#/C++ 양쪽이 접속 시 같은 값을 갖는지 확인할 수 있다.
///
///     TypeName(0:CsType;1:CsType;...)\n
///
/// - 타입은 선언 순서, 멤버는 직렬화 순서([MemoryPackOrder] 적용 후)
/// - CsType 은 공백을 모두 제거한 원본 C# 타입 문자열 (예: `List&lt;int&gt;?`)
/// - 해시는 이 문자열의 UTF-8 바이트에 대한 FNV-1a 64bit
/// </summary>
public static class SchemaHash
{
    const ulong FnvOffsetBasis = 0xcbf29ce484222325UL;
    const ulong FnvPrime = 0x100000001b3UL;

    public static string BuildCanonicalString(IEnumerable<PackableType> types)
    {
        var sb = new StringBuilder();
        foreach (var t in types)
        {
            sb.Append(t.Name).Append('(');
            for (var i = 0; i < t.Members.Count; i++)
            {
                sb.Append(i).Append(':').Append(t.Members[i].CsType).Append(';');
            }
            sb.Append(")\n");
        }
        return sb.ToString();
    }

    public static ulong Fnv1a64(string text)
    {
        var hash = FnvOffsetBasis;
        foreach (var b in Encoding.UTF8.GetBytes(text))
        {
            hash ^= b;
            hash *= FnvPrime;
        }
        return hash;
    }

    public static ulong Compute(IEnumerable<PackableType> types) =>
        Fnv1a64(BuildCanonicalString(types));
}
