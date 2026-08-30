using MemoryPack;

namespace Cases.StdArray;

[MemoryPackable]
public partial class TransformData
{
    public int Id { get; set; }

    // [cpp:std_array(4)]
    public List<float>? Quaternion { get; set; }

    // [cpp:std_array(16)]
    public float[]? Matrix { get; set; }
}
