using MemoryPack;

namespace ChatServer;

public enum PacketId : ushort
{
    LoginRequest     = 101,
    LoginResponse    = 102,
    RoomJoinRequest  = 103,
    RoomJoinResponse = 104,
    RoomChat         = 105,
    PrivateChat      = 106,
    UserEntered      = 107,
    UserLeft         = 108,
}

// Member order MUST match C++ struct declaration order.

[MemoryPackable]
public partial class LoginRequest
{
    public string? Username { get; set; }
}

[MemoryPackable]
public partial class LoginResponse
{
    public bool Success { get; set; }
    public string? Message { get; set; }
}

[MemoryPackable]
public partial class RoomJoinRequest
{
    public string? RoomName { get; set; }
}

[MemoryPackable]
public partial class RoomJoinResponse
{
    public bool Success { get; set; }
    public List<string>? ExistingUsers { get; set; }
}

[MemoryPackable]
public partial class RoomChat
{
    public string? SenderName { get; set; }
    public string? Message { get; set; }
}

[MemoryPackable]
public partial class PrivateChat
{
    public string? SenderName { get; set; }
    public string? TargetName { get; set; }
    public string? Message { get; set; }
}

[MemoryPackable]
public partial class UserEntered
{
    public string? Username { get; set; }
}

[MemoryPackable]
public partial class UserLeft
{
    public string? Username { get; set; }
}
