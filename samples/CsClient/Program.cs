// ============================================================================
// samples/CsClient/Program.cs
//
// C# client for the C++ server in samples/CppServer.
//
// This is the reverse of the CSharpServer + CppClient pair: the bytes on the
// wire are produced by the C++ MemoryPackCpp writer and consumed by the real
// Cysharp MemoryPack library, and vice versa. Every assertion below therefore
// checks actual cross-language wire compatibility, not just a local round trip.
//
// Framing: [2B packetId][4B bodyLength][body...], all little-endian - the same
// header memorypack/packet.hpp writes on the C++ side.
//
// Run:
//     # terminal 1
//     ./build/samples/CppServer
//     # terminal 2
//     dotnet run --project samples/CsClient
//
// Exits 0 when every assertion passes and 1 otherwise, so it can be used as a
// CI interop check.
// ============================================================================

using System.Buffers.Binary;
using System.Net.Sockets;
using System.Numerics;
using CsClient;
using MemoryPack;

const int HeaderSize = 6;
const float Tolerance = 1e-6f;

var host = args.Length >= 1 ? args[0] : "127.0.0.1";
var port = args.Length >= 2 ? int.Parse(args[1]) : 25003;

int failures = 0;

Console.WriteLine($"Connecting to {host}:{port} ...");
using var client = new TcpClient();
try
{
    client.Connect(host, port);
}
catch (SocketException ex)
{
    Console.Error.WriteLine($"Failed to connect: {ex.Message}");
    Console.Error.WriteLine("Start the C++ server first: ./build/samples/CppServer");
    return 1;
}
using var stream = client.GetStream();
Console.WriteLine("Connected.");

RunEchoTest();
RunSumTest();
RunSpawnTest();
RunSpawnClampTest();

Console.WriteLine();
Console.WriteLine("==============================");
if (failures == 0)
{
    Console.WriteLine("All assertions passed.");
    return 0;
}
Console.WriteLine($"{failures} assertion(s) FAILED.");
return 1;

// -- Tests --------------------------------------------------------------------

// int + string in, upper-cased string + a .NET tick count back.
void RunEchoTest()
{
    Console.WriteLine();
    Console.WriteLine("==== EchoRequest -> EchoResponse ====");

    var request = new EchoRequest { Seq = 7, Text = "hello from the C# client" };
    Console.WriteLine($"[SEND] EchoRequest  Seq={request.Seq} Text=\"{request.Text}\"");
    SendPacket(PacketId.EchoRequest, MemoryPackSerializer.Serialize(request));

    var (id, body) = ReceivePacket();
    Check(id == PacketId.EchoResponse, $"packet id is EchoResponse (got {id})");

    var response = MemoryPackSerializer.Deserialize<EchoResponse>(body);
    Console.WriteLine($"[RECV] EchoResponse Seq={response?.Seq} Text=\"{response?.Text}\" " +
                      $"ServerTimeTicks={response?.ServerTimeTicks}");

    Check(response is not null, "response deserialized");
    if (response is null) return;

    Check(response.Seq == request.Seq, $"Seq round-tripped ({request.Seq})");
    var expectedText = request.Text!.ToUpperInvariant();
    Check(response.Text == expectedText, $"Text upper-cased by the server (\"{expectedText}\")");

    // The server sends DateTime.UtcNow.Ticks, so the value must decode to a
    // plausible date - which also proves the int64 arrived byte-correct.
    long lowerBound = new DateTime(2020, 1, 1, 0, 0, 0, DateTimeKind.Utc).Ticks;
    long upperBound = new DateTime(2100, 1, 1, 0, 0, 0, DateTimeKind.Utc).Ticks;
    bool plausible = response.ServerTimeTicks > lowerBound && response.ServerTimeTicks < upperBound;
    var rendered = plausible
        ? new DateTime(response.ServerTimeTicks, DateTimeKind.Utc).ToString("O")
        : "out of range";
    Check(plausible, $"ServerTimeTicks decodes to a plausible UTC time ({rendered})");
}

// List<int> in, a single long back. Exercises the collection header.
void RunSumTest()
{
    Console.WriteLine();
    Console.WriteLine("==== SumRequest -> SumResponse ====");

    var values = new List<int> { 1, 2, 3, 4, 5, -100, 2_000_000_000 };
    var request = new SumRequest { Values = values };
    long expected = values.Sum(v => (long)v);
    Console.WriteLine($"[SEND] SumRequest   Values=[{string.Join(", ", values)}]");
    SendPacket(PacketId.SumRequest, MemoryPackSerializer.Serialize(request));

    var (id, body) = ReceivePacket();
    Check(id == PacketId.SumResponse, $"packet id is SumResponse (got {id})");

    var response = MemoryPackSerializer.Deserialize<SumResponse>(body);
    Console.WriteLine($"[RECV] SumResponse  Total={response?.Total}");

    Check(response is not null, "response deserialized");
    if (response is null) return;

    // 2_000_000_000 + the rest overflows int, so a correct answer also proves
    // the server widened to int64 and that the int64 survived the wire.
    Check(response.Total == expected, $"Total == {expected}");
}

// int in, a list of nested objects back - each with an unmanaged Vector3 and a
// string. This is the shape that breaks first when member order drifts.
void RunSpawnTest()
{
    Console.WriteLine();
    Console.WriteLine("==== SpawnRequest -> SpawnResponse ====");

    const int count = 5;
    Console.WriteLine($"[SEND] SpawnRequest Count={count}");
    SendPacket(PacketId.SpawnRequest, MemoryPackSerializer.Serialize(new SpawnRequest { Count = count }));

    var (id, body) = ReceivePacket();
    Check(id == PacketId.SpawnResponse, $"packet id is SpawnResponse (got {id})");

    var response = MemoryPackSerializer.Deserialize<SpawnResponse>(body);
    Check(response?.Entities is not null, "response deserialized");
    if (response?.Entities is null) return;

    foreach (var entity in response.Entities)
    {
        Console.WriteLine($"[RECV] Entity Id={entity.Id} Position={entity.Position} Name=\"{entity.Name}\"");
    }

    Check(response.Entities.Count == count, $"entity count == {count}");
    for (int i = 0; i < response.Entities.Count; i++)
    {
        var entity = response.Entities[i];
        var expectedPosition = new Vector3(i * 1.5f, i * 2.5f, i * -0.5f);
        Check(entity.Id == 1000 + i, $"entity[{i}].Id == {1000 + i}");
        Check(Near(entity.Position, expectedPosition),
              $"entity[{i}].Position == {expectedPosition} (got {entity.Position})");
        Check(entity.Name == $"Entity_{i}", $"entity[{i}].Name == \"Entity_{i}\" (got \"{entity.Name}\")");
    }
}

// The server clamps the requested count to its own maximum. Asking for more
// than the protocol allows must produce a bounded response, not a huge one.
void RunSpawnClampTest()
{
    Console.WriteLine();
    Console.WriteLine("==== SpawnRequest (over the server maximum) ====");

    const int serverMaximum = 64;
    Console.WriteLine("[SEND] SpawnRequest Count=100000");
    SendPacket(PacketId.SpawnRequest, MemoryPackSerializer.Serialize(new SpawnRequest { Count = 100_000 }));

    var (id, body) = ReceivePacket();
    Check(id == PacketId.SpawnResponse, $"packet id is SpawnResponse (got {id})");

    var response = MemoryPackSerializer.Deserialize<SpawnResponse>(body);
    Console.WriteLine($"[RECV] SpawnResponse Entities={response?.Entities?.Count}");

    Check(response?.Entities is not null, "response deserialized");
    if (response?.Entities is null) return;

    Check(response.Entities.Count == serverMaximum,
          $"server clamped the spawn count to {serverMaximum}");
}

// -- Assertions ---------------------------------------------------------------

void Check(bool condition, string description)
{
    if (condition)
    {
        Console.WriteLine($"       [ OK ] {description}");
    }
    else
    {
        Console.WriteLine($"       [FAIL] {description}");
        failures++;
    }
}

bool Near(Vector3 a, Vector3 b) =>
    MathF.Abs(a.X - b.X) <= Tolerance &&
    MathF.Abs(a.Y - b.Y) <= Tolerance &&
    MathF.Abs(a.Z - b.Z) <= Tolerance;

// -- Framing ------------------------------------------------------------------

void SendPacket(PacketId id, byte[] body)
{
    var header = new byte[HeaderSize];
    BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(0, 2), (ushort)id);
    BinaryPrimitives.WriteInt32LittleEndian(header.AsSpan(2, 4), body.Length);
    stream.Write(header);
    stream.Write(body);
    stream.Flush();
}

(PacketId Id, byte[] Body) ReceivePacket()
{
    var header = new byte[HeaderSize];
    ReadExact(header);
    var id = (PacketId)BinaryPrimitives.ReadUInt16LittleEndian(header.AsSpan(0, 2));
    var bodyLength = BinaryPrimitives.ReadInt32LittleEndian(header.AsSpan(2, 4));
    if (bodyLength is < 0 or > 8 * 1024 * 1024)
    {
        throw new InvalidDataException($"Implausible body length {bodyLength}");
    }

    var body = new byte[bodyLength];
    if (bodyLength > 0) ReadExact(body);
    return (id, body);
}

// TCP is a byte stream: one Read() is not one packet, so always loop.
void ReadExact(byte[] buffer)
{
    int offset = 0;
    while (offset < buffer.Length)
    {
        int read = stream.Read(buffer, offset, buffer.Length - offset);
        if (read == 0) throw new EndOfStreamException("Server closed the connection");
        offset += read;
    }
}
