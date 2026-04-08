using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using ChatServer;
using MemoryPack;

const int Port = 9001;
const int HeaderSize = 6;

// ── Shared State ───────────────────────────────────────────────────────────────
var globalLock   = new object();
var loggedInUsers = new Dictionary<string, ClientSession>();   // username -> session
var rooms         = new Dictionary<string, HashSet<ClientSession>>(); // room -> sessions

var listener = new TcpListener(IPAddress.Any, Port);
listener.Start();
Console.WriteLine($"ChatServer listening on port {Port}...");

while (true)
{
    var tcp = listener.AcceptTcpClient();
    var session = new ClientSession(tcp);
    Console.WriteLine($"Client connected: {tcp.Client.RemoteEndPoint}");
    var thread = new Thread(() => HandleClient(session)) { IsBackground = true };
    thread.Start();
}

// ── Per-Client Handler ─────────────────────────────────────────────────────────
void HandleClient(ClientSession session)
{
    try
    {
        while (session.Client.Connected)
        {
            var header = new byte[HeaderSize];
            if (!ReadExact(session.Stream, header)) break;
            var packetId   = BinaryPrimitives.ReadUInt16LittleEndian(header.AsSpan(0, 2));
            var bodyLength = BinaryPrimitives.ReadInt32LittleEndian(header.AsSpan(2, 4));
            var body = new byte[bodyLength];
            if (bodyLength > 0 && !ReadExact(session.Stream, body)) break;

            switch ((PacketId)packetId)
            {
                case PacketId.LoginRequest:    HandleLogin(session, body);       break;
                case PacketId.RoomJoinRequest:  HandleRoomJoin(session, body);    break;
                case PacketId.RoomChat:         HandleRoomChat(session, body);    break;
                case PacketId.PrivateChat:      HandlePrivateChat(session, body); break;
            }
        }
    }
    catch (Exception) { /* disconnected */ }
    finally { HandleDisconnect(session); }
}

// ── Login ──────────────────────────────────────────────────────────────────────
void HandleLogin(ClientSession session, byte[] body)
{
    var req = MemoryPackSerializer.Deserialize<LoginRequest>(body);
    var username = req?.Username ?? "";

    bool success; string message;
    lock (globalLock)
    {
        if (string.IsNullOrWhiteSpace(username))
        {
            success = false; message = "Empty username";
        }
        else if (loggedInUsers.ContainsKey(username))
        {
            success = false; message = "Username already in use";
        }
        else
        {
            success = true; message = "OK";
            session.Username = username;
            loggedInUsers[username] = session;
        }
    }
    Console.WriteLine($"[Login] {username} -> {(success ? "OK" : message)}");
    var resp = new LoginResponse { Success = success, Message = message };
    SendPacket(session, PacketId.LoginResponse, MemoryPackSerializer.Serialize(resp));
}

// ── Room Join ──────────────────────────────────────────────────────────────────
void HandleRoomJoin(ClientSession session, byte[] body)
{
    var req = MemoryPackSerializer.Deserialize<RoomJoinRequest>(body);
    var roomName = req?.RoomName ?? "default";

    List<string> existingUsers;
    lock (globalLock)
    {
        // Leave old room
        LeaveCurrentRoom(session);

        // Join new room
        if (!rooms.TryGetValue(roomName, out var room))
        {
            room = new HashSet<ClientSession>();
            rooms[roomName] = room;
        }
        existingUsers = room.Where(s => s.Username != null).Select(s => s.Username!).ToList();
        room.Add(session);
        session.CurrentRoom = roomName;

        // Notify existing members
        var notify = MemoryPackSerializer.Serialize(new UserEntered { Username = session.Username ?? "" });
        foreach (var m in room)
            if (m != session) TrySendPacket(m, PacketId.UserEntered, notify);
    }

    Console.WriteLine($"[RoomJoin] {session.Username} -> {roomName} (existing: {existingUsers.Count})");
    var resp = new RoomJoinResponse { Success = true, ExistingUsers = existingUsers };
    SendPacket(session, PacketId.RoomJoinResponse, MemoryPackSerializer.Serialize(resp));
}

// ── Room Chat ──────────────────────────────────────────────────────────────────
void HandleRoomChat(ClientSession session, byte[] body)
{
    var req = MemoryPackSerializer.Deserialize<RoomChat>(body);
    if (session.CurrentRoom == null) return;

    var chat = new RoomChat { SenderName = session.Username ?? "", Message = req?.Message ?? "" };
    var chatBody = MemoryPackSerializer.Serialize(chat);

    Console.WriteLine($"[RoomChat] {session.Username}: {req?.Message}");
    lock (globalLock)
    {
        if (rooms.TryGetValue(session.CurrentRoom, out var room))
            foreach (var m in room)
                TrySendPacket(m, PacketId.RoomChat, chatBody);
    }
}

// ── Private Chat ───────────────────────────────────────────────────────────────
void HandlePrivateChat(ClientSession session, byte[] body)
{
    var req = MemoryPackSerializer.Deserialize<PrivateChat>(body);
    var targetName = req?.TargetName ?? "";

    var pm = new PrivateChat
    {
        SenderName = session.Username ?? "",
        TargetName = targetName,
        Message    = req?.Message ?? ""
    };
    var pmBody = MemoryPackSerializer.Serialize(pm);

    Console.WriteLine($"[PM] {session.Username} -> {targetName}: {req?.Message}");
    lock (globalLock)
    {
        // Send to target
        if (loggedInUsers.TryGetValue(targetName, out var target))
            TrySendPacket(target, PacketId.PrivateChat, pmBody);
        // Echo to sender
        TrySendPacket(session, PacketId.PrivateChat, pmBody);
    }
}

// ── Disconnect ─────────────────────────────────────────────────────────────────
void HandleDisconnect(ClientSession session)
{
    Console.WriteLine($"[Disconnect] {session.Username ?? "(unknown)"}");
    lock (globalLock)
    {
        if (session.Username != null)
            loggedInUsers.Remove(session.Username);
        LeaveCurrentRoom(session);
    }
    try { session.Client.Close(); } catch { }
}

void LeaveCurrentRoom(ClientSession session)
{
    // Must be called inside lock(globalLock)
    if (session.CurrentRoom == null) return;
    if (rooms.TryGetValue(session.CurrentRoom, out var room))
    {
        room.Remove(session);
        var notify = MemoryPackSerializer.Serialize(new UserLeft { Username = session.Username ?? "" });
        foreach (var m in room)
            TrySendPacket(m, PacketId.UserLeft, notify);
        if (room.Count == 0) rooms.Remove(session.CurrentRoom);
    }
    session.CurrentRoom = null;
}

// ── Network Helpers ────────────────────────────────────────────────────────────
void SendPacket(ClientSession session, PacketId id, byte[] body)
{
    var header = new byte[HeaderSize];
    BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(0, 2), (ushort)id);
    BinaryPrimitives.WriteInt32LittleEndian(header.AsSpan(2, 4), body.Length);
    lock (session.WriteLock)
    {
        session.Stream.Write(header);
        session.Stream.Write(body);
        session.Stream.Flush();
    }
}

void TrySendPacket(ClientSession session, PacketId id, byte[] body)
{
    try { SendPacket(session, id, body); } catch { }
}

static bool ReadExact(NetworkStream stream, byte[] buffer)
{
    int offset = 0;
    while (offset < buffer.Length)
    {
        int n = stream.Read(buffer, offset, buffer.Length - offset);
        if (n == 0) return false;
        offset += n;
    }
    return true;
}

// ── Client Session ─────────────────────────────────────────────────────────────
class ClientSession(TcpClient client)
{
    public TcpClient     Client      { get; } = client;
    public NetworkStream Stream      { get; } = client.GetStream();
    public object        WriteLock   { get; } = new();
    public string?       Username    { get; set; }
    public string?       CurrentRoom { get; set; }
}
