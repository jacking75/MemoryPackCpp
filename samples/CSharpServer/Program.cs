using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using CSharpServer;
using MemoryPack;

const int Port = 9000;
const int PacketHeaderSize = 6; // [2B packetId][4B bodyLength]

var listener = new TcpListener(IPAddress.Loopback, Port);
listener.Start();
Console.WriteLine($"Server listening on 127.0.0.1:{Port} ...");
Console.WriteLine("Waiting for a client...");

using var client = await listener.AcceptTcpClientAsync();
Console.WriteLine($"Client connected: {client.Client.RemoteEndPoint}");

var stream = client.GetStream();

try
{
    while (client.Connected)
    {
        // ── Read packet header ──
        var header = new byte[PacketHeaderSize];
        if (!await ReadExactAsync(stream, header))
            break;

        var packetId   = BinaryPrimitives.ReadUInt16LittleEndian(header.AsSpan(0, 2));
        var bodyLength = BinaryPrimitives.ReadInt32LittleEndian(header.AsSpan(2, 4));

        // ── Read packet body ──
        var body = new byte[bodyLength];
        if (bodyLength > 0 && !await ReadExactAsync(stream, body))
            break;

        // ── Process & Respond ──
        byte[]? responseBody = null;
        ushort responseId = 0;

        switch ((PacketId)packetId)
        {
            case PacketId.LoginRequest:
            {
                var req = MemoryPackSerializer.Deserialize<LoginRequest>(body);
                Console.WriteLine($"[LoginRequest] Username={req?.Username}, Level={req?.Level}");

                var resp = new LoginResponse
                {
                    Success  = true,
                    PlayerId = 1001,
                    Message  = $"Welcome {req?.Username}!"
                };
                responseBody = MemoryPackSerializer.Serialize(resp);
                responseId   = (ushort)PacketId.LoginResponse;
                Console.WriteLine($"  -> LoginResponse: Success={resp.Success}, PlayerId={resp.PlayerId}");
                break;
            }

            case PacketId.PlayerState:
            {
                var req = MemoryPackSerializer.Deserialize<PlayerState>(body);
                Console.WriteLine($"[PlayerState] Id={req?.PlayerId}, Pos=({req?.PosX}, {req?.PosY}, {req?.PosZ}), Name={req?.Name}");

                // Echo back with slightly modified position
                var resp = new PlayerState
                {
                    PlayerId = req!.PlayerId,
                    PosX     = req.PosX + 10.0f,
                    PosY     = req.PosY + 10.0f,
                    PosZ     = req.PosZ + 10.0f,
                    Name     = req.Name
                };
                responseBody = MemoryPackSerializer.Serialize(resp);
                responseId   = (ushort)PacketId.PlayerState;
                Console.WriteLine($"  -> PlayerState: Pos=({resp.PosX}, {resp.PosY}, {resp.PosZ})");
                break;
            }

            case PacketId.ChatMessage:
            {
                var req = MemoryPackSerializer.Deserialize<ChatMessage>(body);
                Console.WriteLine($"[ChatMessage] SenderId={req?.SenderId}, Message=\"{req?.Message}\", Timestamp={req?.Timestamp}");

                var resp = new ChatMessage
                {
                    SenderId  = 0, // server
                    Message   = $"Echo: {req?.Message}",
                    Timestamp = req?.Timestamp ?? 0
                };
                responseBody = MemoryPackSerializer.Serialize(resp);
                responseId   = (ushort)PacketId.ChatMessage;
                Console.WriteLine($"  -> ChatMessage: \"{resp.Message}\"");
                break;
            }

            case PacketId.ScoreUpdate:
            {
                var req = MemoryPackSerializer.Deserialize<ScoreUpdate>(body);
                var scoresStr = req?.Scores != null ? string.Join(", ", req.Scores) : "null";
                Console.WriteLine($"[ScoreUpdate] PlayerId={req?.PlayerId}, Scores=[{scoresStr}], Total={req?.TotalScore}");

                // Double all scores
                var doubledScores = req?.Scores?.Select(s => s * 2).ToList();
                var resp = new ScoreUpdate
                {
                    PlayerId   = req!.PlayerId,
                    Scores     = doubledScores,
                    TotalScore = req.TotalScore * 2
                };
                responseBody = MemoryPackSerializer.Serialize(resp);
                responseId   = (ushort)PacketId.ScoreUpdate;
                var respScoresStr = resp.Scores != null ? string.Join(", ", resp.Scores) : "null";
                Console.WriteLine($"  -> ScoreUpdate: Scores=[{respScoresStr}], Total={resp.TotalScore}");
                break;
            }

            case PacketId.InventoryData:
            {
                var req = MemoryPackSerializer.Deserialize<InventoryData>(body);
                Console.WriteLine($"[InventoryData] PlayerId={req?.PlayerId}");
                if (req?.ItemNames != null)
                {
                    for (int i = 0; i < req.ItemNames.Count; i++)
                    {
                        var count = (req.ItemCounts != null && i < req.ItemCounts.Count) ? req.ItemCounts[i] : 0;
                        Console.WriteLine($"  {req.ItemNames[i]} x{count}");
                    }
                }

                // Echo back with an extra item added
                var names  = req?.ItemNames != null ? new List<string>(req.ItemNames) : new List<string>();
                var counts = req?.ItemCounts != null ? new List<int>(req.ItemCounts) : new List<int>();
                names.Add("Gift");
                counts.Add(99);

                var resp = new InventoryData
                {
                    PlayerId   = req!.PlayerId,
                    ItemNames  = names,
                    ItemCounts = counts
                };
                responseBody = MemoryPackSerializer.Serialize(resp);
                responseId   = (ushort)PacketId.InventoryData;
                Console.WriteLine($"  -> InventoryData: added Gift x99");
                break;
            }

            case PacketId.BufferData:
            {
                var req = MemoryPackSerializer.Deserialize<BufferData>(body);
                var rawStr = req?.RawData != null ? string.Join(",", req.RawData.Select(b => $"0x{b:X2}")) : "null";
                var charStr = req?.CharCodes != null ? string.Join(",", req.CharCodes) : "null";
                Console.WriteLine($"[BufferData] Tag=0x{req?.Tag:X2}, Grade={req?.Grade}, RawData=[{rawStr}], CharCodes=[{charStr}]");

                // Reverse both arrays
                var reversedRaw = req?.RawData != null ? new List<byte>(req.RawData) : new List<byte>();
                reversedRaw.Reverse();
                var reversedChar = req?.CharCodes != null ? new List<sbyte>(req.CharCodes) : new List<sbyte>();
                reversedChar.Reverse();

                var resp = new BufferData
                {
                    Tag       = req!.Tag,
                    Grade     = req.Grade,
                    RawData   = reversedRaw,
                    CharCodes = reversedChar
                };
                responseBody = MemoryPackSerializer.Serialize(resp);
                responseId   = (ushort)PacketId.BufferData;
                Console.WriteLine($"  -> BufferData: reversed arrays");
                break;
            }

            case PacketId.IntArrayPacket:
            {
                var req = MemoryPackSerializer.Deserialize<IntArrayPacket>(body);
                var shortStr = req?.ShortArray != null ? string.Join(",", req.ShortArray) : "null";
                var intStr   = req?.IntArray   != null ? string.Join(",", req.IntArray)   : "null";
                var longStr  = req?.LongArray  != null ? string.Join(",", req.LongArray)  : "null";
                Console.WriteLine($"[IntArrayPacket] Id={req?.Id}, Short=[{shortStr}], Int=[{intStr}], Long=[{longStr}]");

                // Sort all arrays ascending
                var sortedShort = req?.ShortArray != null ? new List<short>(req.ShortArray) : new List<short>();
                sortedShort.Sort();
                var sortedInt = req?.IntArray != null ? new List<int>(req.IntArray) : new List<int>();
                sortedInt.Sort();
                var sortedLong = req?.LongArray != null ? new List<long>(req.LongArray) : new List<long>();
                sortedLong.Sort();

                var resp = new IntArrayPacket
                {
                    Id         = req!.Id,
                    ShortArray = sortedShort,
                    IntArray   = sortedInt,
                    LongArray  = sortedLong
                };
                responseBody = MemoryPackSerializer.Serialize(resp);
                responseId   = (ushort)PacketId.IntArrayPacket;
                Console.WriteLine($"  -> IntArrayPacket: sorted arrays");
                break;
            }

            case PacketId.SkillSlotData:
            {
                var req = MemoryPackSerializer.Deserialize<SkillSlotData>(body);
                var skillStr = req?.SkillIds != null ? string.Join(",", req.SkillIds) : "null";
                var cdStr = req?.Cooldowns != null ? string.Join(",", req.Cooldowns.Select(c => $"{c:F1}")) : "null";
                Console.WriteLine($"[SkillSlotData] PlayerId={req?.PlayerId}, Skills=[{skillStr}], Cooldowns=[{cdStr}]");

                // +100 to each skill ID, +1.0 to each cooldown
                var resp = new SkillSlotData
                {
                    PlayerId  = req!.PlayerId,
                    SkillIds  = req.SkillIds?.Select(s => s + 100).ToList(),
                    Cooldowns = req.Cooldowns?.Select(c => c + 1.0f).ToList()
                };
                responseBody = MemoryPackSerializer.Serialize(resp);
                responseId   = (ushort)PacketId.SkillSlotData;
                Console.WriteLine($"  -> SkillSlotData: +100 skill IDs, +1.0 cooldowns");
                break;
            }

            case PacketId.MapTileRow:
            {
                var req = MemoryPackSerializer.Deserialize<MapTileRow>(body);
                var tileStr = req?.Tiles != null ? string.Join(",", req.Tiles) : "null";
                var hStr = req?.Heights != null ? string.Join(",", req.Heights) : "null";
                Console.WriteLine($"[MapTileRow] Row={req?.RowIndex}, Tiles=[{tileStr}], Heights=[{hStr}]");

                // XOR tiles with 0xFF, negate heights
                var resp = new MapTileRow
                {
                    RowIndex = req!.RowIndex,
                    Tiles    = req.Tiles?.Select(t => (byte)(t ^ 0xFF)).ToList(),
                    Heights  = req.Heights?.Select(h => (short)-h).ToList()
                };
                responseBody = MemoryPackSerializer.Serialize(resp);
                responseId   = (ushort)PacketId.MapTileRow;
                Console.WriteLine($"  -> MapTileRow: XOR tiles, negate heights");
                break;
            }

            case PacketId.MixedFormatPacket:
            {
                var req = MemoryPackSerializer.Deserialize<MixedFormatPacket>(body);
                var dynStr = req?.DynamicScores != null ? string.Join(",", req.DynamicScores) : "null";
                var fixStr = req?.FixedBonuses != null ? string.Join(",", req.FixedBonuses) : "null";
                var tagStr = req?.TagBytes != null
                    ? new string(req.TagBytes.Select(b => (char)b).ToArray()) : "null";
                Console.WriteLine($"[MixedFormatPacket] Id={req?.Id}, Dynamic=[{dynStr}], Fixed=[{fixStr}], Tag=\"{tagStr}\", Mult={req?.Multiplier}");

                // Double scores, triple bonuses, uppercase tag, square multiplier
                var resp = new MixedFormatPacket
                {
                    Id            = req!.Id,
                    DynamicScores = req.DynamicScores?.Select(s => s * 2).ToList(),
                    FixedBonuses  = req.FixedBonuses?.Select(b => b * 3).ToList(),
                    TagBytes      = req.TagBytes?.Select(b =>
                        (b >= 'a' && b <= 'z') ? (sbyte)(b - 32) : b).ToList(),
                    Multiplier    = req.Multiplier * req.Multiplier
                };
                responseBody = MemoryPackSerializer.Serialize(resp);
                responseId   = (ushort)PacketId.MixedFormatPacket;
                Console.WriteLine($"  -> MixedFormatPacket: x2 scores, x3 bonuses, uppercase tag, squared mult");
                break;
            }

            default:
                Console.WriteLine($"Unknown packet ID: {packetId}");
                continue;
        }

        // ── Send response ──
        if (responseBody != null)
        {
            var respHeader = new byte[PacketHeaderSize];
            BinaryPrimitives.WriteUInt16LittleEndian(respHeader.AsSpan(0, 2), responseId);
            BinaryPrimitives.WriteInt32LittleEndian(respHeader.AsSpan(2, 4), responseBody.Length);
            await stream.WriteAsync(respHeader);
            await stream.WriteAsync(responseBody);
            await stream.FlushAsync();
        }
    }
}
catch (IOException)
{
    // Client disconnected
}

Console.WriteLine("Client disconnected. Server shutting down.");
listener.Stop();

// ── Helper: read exact number of bytes from stream ──
static async Task<bool> ReadExactAsync(NetworkStream stream, byte[] buffer)
{
    int offset = 0;
    while (offset < buffer.Length)
    {
        int read = await stream.ReadAsync(buffer.AsMemory(offset, buffer.Length - offset));
        if (read == 0) return false; // connection closed
        offset += read;
    }
    return true;
}
