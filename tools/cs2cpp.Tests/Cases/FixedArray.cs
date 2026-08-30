using MemoryPack;

namespace Cases.FixedArray;

[MemoryPackable]
public partial class SkillSlotData
{
    public int PlayerId { get; set; }

    // [cpp:fixed_array(8, MAX_SKILLS)]
    public List<int>? SkillIds { get; set; }

    // [cpp:fixed_array(8, MAX_SKILLS)]
    public List<float>? Cooldowns { get; set; }
}

[MemoryPackable]
public partial class MixedFormatPacket
{
    public int Id { get; set; }

    public List<int>? DynamicScores { get; set; }

    // [cpp:fixed_array(4, MAX_BONUSES)]
    public List<int>? FixedBonuses { get; set; }

    // [cpp:fixed_array(16, MAX_TAG_LEN)]
    public sbyte[]? Tag { get; set; }

    public double Multiplier { get; set; }
}
