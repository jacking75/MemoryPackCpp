using MemoryPack;

namespace Cases.Enums;

// 명시적 값이 없는 멤버는 자동 증가한다.
public enum Color : byte
{
    Red,
    Green,
    Blue = 10,
    Cyan,
}

// 음수 값
public enum Signed : short
{
    Neg = -2,
    Zero = 0,
    Pos = 5,
}

public enum Wide : uint
{
    Small = 1,
    Big = 0x10000,
}

[MemoryPackable]
public partial class EnumPacket
{
    public Color Color { get; set; }
    public Signed Signed { get; set; }
    public Wide Wide { get; set; }
    public List<Color>? Palette { get; set; }
}
