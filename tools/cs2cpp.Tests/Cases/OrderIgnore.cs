using MemoryPack;

namespace Cases.OrderIgnore;

[MemoryPackable]
public partial class OrderedPacket
{
    [MemoryPackOrder(1)]
    public int Second { get; set; }

    [MemoryPackOrder(0)]
    public int First { get; set; }

    [MemoryPackIgnore]
    public int Ignored { get; set; }

    // 순서 지정이 없는 멤버는 지정된 멤버들 뒤에 선언 순서대로 붙는다.
    public string? Trailing { get; set; }
}
