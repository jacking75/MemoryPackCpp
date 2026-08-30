using MemoryPack;

namespace Cases.Nested;

// Item 보다 먼저 선언되어 있어도 C++ 쪽은 의존성 순서로 재정렬된다.
[MemoryPackable]
public partial class Inventory
{
    public int OwnerId { get; set; }
    public Item? Equipped { get; set; }
    public List<Item>? Items { get; set; }
}

[MemoryPackable]
public partial class Item
{
    public int ItemId { get; set; }
    public string? ItemName { get; set; }
    public int Count { get; set; }
}
