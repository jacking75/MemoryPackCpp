using MemoryPack;

namespace Cases.Dispatch;

public enum PacketId : ushort
{
    LoginRequest = 1,
    LoginResponse = 2,
    Heartbeat = 3,
}

[MemoryPackable]
public partial class LoginRequest
{
    public string? Username { get; set; }
    public int Level { get; set; }
}

[MemoryPackable]
public partial class LoginResponse
{
    public bool Success { get; set; }
    public int PlayerId { get; set; }
}
