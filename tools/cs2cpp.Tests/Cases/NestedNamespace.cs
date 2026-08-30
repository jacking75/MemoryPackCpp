using MemoryPack;

namespace Outer
{
    namespace Inner
    {
        [MemoryPackable]
        public partial class Ping
        {
            public long Ticks { get; set; }
        }
    }
}
