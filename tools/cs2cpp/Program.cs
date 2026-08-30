using System.Text;

namespace Cs2Cpp;

/// <summary>C# MemoryPack 패킷 정의 → C++ 헤더 파일 변환 도구.</summary>
public static class Program
{
    public static int Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;

        var options = CliOptions.Parse(args, out var error);
        if (options is null)
        {
            Console.Error.WriteLine($"Error: {error}");
            Console.Error.WriteLine();
            Console.Error.WriteLine(CliOptions.Usage);
            return 1;
        }

        if (options.Help)
        {
            Console.WriteLine(CliOptions.Usage);
            return 0;
        }

        try
        {
            return new GeneratorRunner(options, Console.Out, Console.Error).Run();
        }
        catch (IOException e)
        {
            Console.Error.WriteLine($"Error: {e.Message}");
            return 1;
        }
        catch (UnauthorizedAccessException e)
        {
            Console.Error.WriteLine($"Error: {e.Message}");
            return 1;
        }
    }
}
