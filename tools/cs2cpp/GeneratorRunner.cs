using System.Text;

namespace Cs2Cpp;

/// <summary>입력 파일 탐색 → 생성 → 쓰기/검사 까지의 전체 흐름.</summary>
public sealed class GeneratorRunner(CliOptions options, TextWriter stdout, TextWriter stderr)
{
    static readonly UTF8Encoding Utf8NoBom = new(false);

    public int Run()
    {
        List<string> inputs;
        try
        {
            inputs = ResolveInputs(options.Inputs);
        }
        catch (DirectoryNotFoundException e)
        {
            stderr.WriteLine($"Error: {e.Message}");
            return 1;
        }

        if (inputs.Count == 0)
        {
            stderr.WriteLine("Error: 입력과 일치하는 .cs 파일이 없습니다.");
            return 1;
        }

        var failed = false;
        ulong lastHash = 0;
        Schema? lastSchema = null;
        CppGenerator? lastGenerator = null;

        foreach (var input in inputs)
        {
            var source = File.ReadAllText(input, Encoding.UTF8);
            var parser = new CsParser(options.NullableStrings);
            var schema = parser.Parse(source, input);

            foreach (var w in schema.Warnings)
                stderr.WriteLine($"Warning: {input}: {w}");

            if (schema.Types.Count == 0 && schema.Unions.Count == 0 && schema.Enums.Count == 0)
                stderr.WriteLine($"Warning: [MemoryPackable] 타입을 찾지 못했습니다: {input}");

            var generator = new CppGenerator(schema, options.ToGeneratorOptions());
            var text = generator.Generate();
            var outputPath = ResolveOutputPath(input, inputs.Count);

            lastHash = generator.SchemaHashValue;
            lastSchema = schema;
            lastGenerator = generator;

            if (options.Check)
            {
                if (!CheckFile(outputPath, text)) failed = true;
            }
            else
            {
                WriteFile(outputPath, text);
                stdout.WriteLine($"Generated: {outputPath}");
            }

            if (options.Verbose)
            {
                stdout.WriteLine($"  Enums:   {schema.Enums.Count}");
                stdout.WriteLine($"  Unions:  {schema.Unions.Count}");
                stdout.WriteLine($"  Packets: {schema.Types.Count}");
                foreach (var t in schema.Types)
                {
                    var kind = t.IsUnmanaged ? "unmanaged struct"
                             : t.VersionTolerant ? "version tolerant"
                             : t.Kind.ToString().ToLowerInvariant();
                    stdout.WriteLine($"    - {t.Name} ({t.Members.Count} members, {kind})");
                }
                stdout.WriteLine($"  Style:   {options.Style.ToString().ToLowerInvariant()}");
                stdout.WriteLine($"  Dispatch:{(generator.DispatchEnabled ? " on" : " off")}");
            }

            if (generator.DispatchEnabled)
                stdout.WriteLine($"PACKET_SCHEMA_HASH = 0x{generator.SchemaHashValue:X16}ULL   ({Path.GetFileName(input)})");
        }

        if (options.EmitSchemaHashCs is { } csPath && lastSchema is not null && lastGenerator is not null)
        {
            var csText = lastGenerator.GenerateSchemaHashCs();
            if (options.Check)
            {
                if (!CheckFile(csPath, csText)) failed = true;
            }
            else
            {
                WriteFile(csPath, csText);
                stdout.WriteLine($"Generated: {csPath} (0x{lastHash:X16})");
            }
        }

        return failed ? 1 : 0;
    }

    // ── 입력 탐색 ─────────────────────────────────────────────────────────────

    public static List<string> ResolveInputs(IEnumerable<string> patterns)
    {
        var result = new List<string>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        void Add(string path)
        {
            var full = Path.GetFullPath(path);
            if (seen.Add(full)) result.Add(full);
        }

        foreach (var raw in patterns)
        {
            var pattern = raw.Replace('\\', '/');

            if (Directory.Exists(pattern))
            {
                foreach (var f in EnumerateCsFiles(pattern, "*.cs", SearchOption.AllDirectories))
                    Add(f);
                continue;
            }

            if (File.Exists(pattern))
            {
                Add(pattern);
                continue;
            }

            if (!pattern.Contains('*') && !pattern.Contains('?'))
                throw new DirectoryNotFoundException($"입력을 찾을 수 없습니다: {raw}");

            var starIndex = pattern.IndexOf("**", StringComparison.Ordinal);
            string baseDir;
            string filePattern;
            SearchOption search;

            if (starIndex >= 0)
            {
                baseDir = pattern[..starIndex].TrimEnd('/');
                filePattern = Path.GetFileName(pattern);
                search = SearchOption.AllDirectories;
            }
            else
            {
                baseDir = Path.GetDirectoryName(pattern) ?? "";
                filePattern = Path.GetFileName(pattern);
                search = SearchOption.TopDirectoryOnly;
            }
            if (string.IsNullOrEmpty(baseDir)) baseDir = ".";
            if (string.IsNullOrEmpty(filePattern)) filePattern = "*.cs";
            if (!Directory.Exists(baseDir))
                throw new DirectoryNotFoundException($"디렉터리를 찾을 수 없습니다: {baseDir}");

            foreach (var f in EnumerateCsFiles(baseDir, filePattern, search))
                Add(f);
        }

        result.Sort(StringComparer.OrdinalIgnoreCase);
        return result;
    }

    static IEnumerable<string> EnumerateCsFiles(string dir, string pattern, SearchOption search) =>
        Directory.EnumerateFiles(dir, pattern, search)
            .Where(f => f.EndsWith(".cs", StringComparison.OrdinalIgnoreCase))
            .Where(f =>
            {
                var normalized = f.Replace('\\', '/');
                return !normalized.Contains("/obj/", StringComparison.OrdinalIgnoreCase)
                    && !normalized.Contains("/bin/", StringComparison.OrdinalIgnoreCase);
            });

    // ── 출력 경로 ─────────────────────────────────────────────────────────────

    string ResolveOutputPath(string input, int inputCount)
    {
        var output = options.Output;
        if (string.IsNullOrEmpty(output))
            return Path.ChangeExtension(input, ".hpp");

        var looksLikeDir = inputCount > 1
            || Directory.Exists(output)
            || output.EndsWith('/') || output.EndsWith('\\')
            || string.IsNullOrEmpty(Path.GetExtension(output));

        if (looksLikeDir)
            return Path.Combine(output, Path.GetFileNameWithoutExtension(input) + ".hpp");

        return output;
    }

    static void WriteFile(string path, string text)
    {
        var dir = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
            Directory.CreateDirectory(dir);
        File.WriteAllText(path, text, Utf8NoBom);
    }

    // ── --check ───────────────────────────────────────────────────────────────

    bool CheckFile(string path, string expected)
    {
        if (!File.Exists(path))
        {
            stderr.WriteLine($"--check FAILED: 파일이 없습니다: {path}");
            return false;
        }

        var actual = File.ReadAllText(path, Encoding.UTF8);
        if (Normalize(actual) == Normalize(expected))
        {
            if (options.Verbose) stdout.WriteLine($"--check OK: {path}");
            return true;
        }

        stderr.WriteLine($"--check FAILED: {path}");
        foreach (var line in TextDiff.Unified(Normalize(actual), Normalize(expected), path))
            stderr.WriteLine(line);
        return false;
    }

    static string Normalize(string text) => text.Replace("\r\n", "\n").Replace('\r', '\n');
}

/// <summary>--check 가 출력하는 간단한 unified diff.</summary>
public static class TextDiff
{
    const int MaxContext = 3;
    const int MaxHunkLines = 200;

    public static IEnumerable<string> Unified(string oldText, string newText, string label)
    {
        var a = oldText.Split('\n');
        var b = newText.Split('\n');
        var ops = Diff(a, b);

        var output = new List<string>
        {
            $"--- {label} (committed)",
            $"+++ {label} (generated)",
        };

        var pending = new List<string>();
        var contextAfter = 0;
        var emitted = 0;

        foreach (var (kind, text) in ops)
        {
            if (kind == ' ')
            {
                if (contextAfter > 0)
                {
                    output.Add("  " + text);
                    contextAfter--;
                    emitted++;
                }
                else
                {
                    pending.Add("  " + text);
                    if (pending.Count > MaxContext) pending.RemoveAt(0);
                }
                continue;
            }

            if (pending.Count > 0)
            {
                output.AddRange(pending);
                emitted += pending.Count;
                pending.Clear();
            }
            output.Add((kind == '-' ? "- " : "+ ") + text);
            emitted++;
            contextAfter = MaxContext;

            if (emitted >= MaxHunkLines)
            {
                output.Add("  ... (diff truncated)");
                break;
            }
        }

        return output;
    }

    static List<(char Kind, string Text)> Diff(string[] a, string[] b)
    {
        // 앞뒤 공통 부분을 잘라내 DP 크기를 줄인다.
        var start = 0;
        while (start < a.Length && start < b.Length && a[start] == b[start]) start++;
        var endA = a.Length - 1;
        var endB = b.Length - 1;
        while (endA >= start && endB >= start && a[endA] == b[endB]) { endA--; endB--; }

        var result = new List<(char, string)>();
        for (var i = 0; i < start; i++) result.Add((' ', a[i]));

        var n = endA - start + 1;
        var m = endB - start + 1;

        if ((long)n * m > 4_000_000L)
        {
            // 너무 커서 LCS 를 돌릴 수 없으면 통짜 치환으로 표시한다.
            for (var i = 0; i < n; i++) result.Add(('-', a[start + i]));
            for (var j = 0; j < m; j++) result.Add(('+', b[start + j]));
        }
        else
        {
            var lcs = new int[n + 1, m + 1];
            for (var i = n - 1; i >= 0; i--)
                for (var j = m - 1; j >= 0; j--)
                    lcs[i, j] = a[start + i] == b[start + j]
                        ? lcs[i + 1, j + 1] + 1
                        : Math.Max(lcs[i + 1, j], lcs[i, j + 1]);

            var x = 0;
            var y = 0;
            while (x < n && y < m)
            {
                if (a[start + x] == b[start + y]) { result.Add((' ', a[start + x])); x++; y++; }
                else if (lcs[x + 1, y] >= lcs[x, y + 1]) { result.Add(('-', a[start + x])); x++; }
                else { result.Add(('+', b[start + y])); y++; }
            }
            while (x < n) { result.Add(('-', a[start + x])); x++; }
            while (y < m) { result.Add(('+', b[start + y])); y++; }
        }

        for (var i = endA + 1; i < a.Length; i++) result.Add((' ', a[i]));
        return result;
    }
}
