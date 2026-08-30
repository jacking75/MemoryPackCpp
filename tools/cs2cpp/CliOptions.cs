namespace Cs2Cpp;

/// <summary>cs2cpp 명령줄 옵션.</summary>
public sealed class CliOptions
{
    public List<string> Inputs { get; } = [];
    public string? Output { get; set; }
    public string? Namespace { get; set; }
    public GeneratorStyle Style { get; set; } = GeneratorStyle.Macro;
    public bool NullableStrings { get; set; }
    public bool Check { get; set; }
    public bool Verbose { get; set; }
    /// <summary>null = PacketId enum 이 있으면 자동으로 켜짐.</summary>
    public bool? Dispatch { get; set; }
    public string? EmitSchemaHashCs { get; set; }
    public bool Help { get; set; }

    public GeneratorOptions ToGeneratorOptions() => new()
    {
        Style = Style,
        Namespace = Namespace,
        NullableStrings = NullableStrings,
        Dispatch = Dispatch,
    };

    public const string Usage = """
        cs2cpp — C# MemoryPack 패킷 정의를 C++ 헤더로 변환합니다.

        사용법:
          cs2cpp <input...> [options]

          <input>                 .cs 파일 / 디렉터리 / 글롭 패턴 (여러 개 지정 가능)

        옵션:
          -o, --output <path>     출력 .hpp 파일(입력이 하나일 때) 또는 출력 디렉터리
              --namespace <ns>    생성 코드를 감쌀 C++ 네임스페이스
              --style <s>         macro(기본) | explicit
              --nullable-strings  C#의 string? 을 std::optional<std::string> 으로 매핑
              --dispatch          디스패치 테이블/스키마 해시 강제 생성
              --no-dispatch       디스패치 테이블/스키마 해시 생성 안 함
              --emit-schema-hash-cs <path>
                                  PacketSchemaHash 상수를 담은 C# 파일 생성
              --check             생성 결과를 기존 파일과 비교만 하고 쓰지 않음
                                  (차이가 있으면 diff 출력 후 exit code 1)
              --verbose           상세 로그
          -h, --help              이 도움말

        예:
          cs2cpp samples/ChatServer/Packets.cs -o samples/ChatClient/packets.hpp
          cs2cpp samples/**/Packets.cs -o build/generated --style explicit
          cs2cpp samples/ChatServer/Packets.cs -o samples/ChatClient/packets.hpp --check
        """;

    /// <summary>인자를 파싱한다. 오류가 있으면 <paramref name="error"/>에 메시지를 담고 null 을 반환.</summary>
    public static CliOptions? Parse(string[] args, out string? error)
    {
        error = null;
        var o = new CliOptions();

        for (var i = 0; i < args.Length; i++)
        {
            var arg = args[i];

            if (arg is "-h" or "--help" or "/?")
            {
                o.Help = true;
                continue;
            }

            if (!arg.StartsWith('-'))
            {
                o.Inputs.Add(arg);
                continue;
            }

            // --name=value 형태를 --name value 로 정규화
            string name = arg;
            string? inlineValue = null;
            var eq = arg.IndexOf('=');
            if (eq > 0)
            {
                name = arg[..eq];
                inlineValue = arg[(eq + 1)..];
            }

            switch (name)
            {
                case "-o":
                case "--output":
                    if (!TryTakeValue(args, ref i, inlineValue, name, out var output, out error)) return null;
                    o.Output = output;
                    break;

                case "--namespace":
                    if (!TryTakeValue(args, ref i, inlineValue, name, out var nsValue, out error)) return null;
                    o.Namespace = nsValue;
                    break;

                case "--style":
                {
                    if (!TryTakeValue(args, ref i, inlineValue, name, out var style, out error)) return null;
                    switch (style.ToLowerInvariant())
                    {
                        case "macro": o.Style = GeneratorStyle.Macro; break;
                        case "explicit": o.Style = GeneratorStyle.Explicit; break;
                        default:
                            error = $"알 수 없는 --style 값: {style} (macro | explicit)";
                            return null;
                    }
                    break;
                }

                case "--nullable-strings":
                    o.NullableStrings = true;
                    break;

                case "--dispatch":
                    o.Dispatch = true;
                    break;

                case "--no-dispatch":
                    o.Dispatch = false;
                    break;

                case "--emit-schema-hash-cs":
                    if (!TryTakeValue(args, ref i, inlineValue, name, out var hashCs, out error)) return null;
                    o.EmitSchemaHashCs = hashCs;
                    break;

                case "--check":
                    o.Check = true;
                    break;

                case "--verbose":
                    o.Verbose = true;
                    break;

                default:
                    error = $"알 수 없는 옵션: {arg}";
                    return null;
            }
        }

        if (!o.Help && o.Inputs.Count == 0)
        {
            error = "입력 파일을 하나 이상 지정해야 합니다.";
            return null;
        }

        return o;
    }

    static bool TryTakeValue(
        string[] args, ref int i, string? inlineValue, string optionName,
        out string value, out string? error)
    {
        if (inlineValue is not null)
        {
            value = inlineValue;
            error = null;
            return true;
        }
        if (i + 1 < args.Length)
        {
            value = args[++i];
            error = null;
            return true;
        }
        value = "";
        error = $"옵션 {optionName} 에 값이 필요합니다.";
        return false;
    }
}
