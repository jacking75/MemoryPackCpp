// ============================================================================
// samples/CppServer/main.cpp
//
// A C++ MemoryPack game server, talking to the C# client in samples/CsClient.
//
// This is the reverse of the CSharpServer + CppClient pair: the authoritative
// side is native C++ and the client is managed C#, which is what a Unity (or
// MAUI, or WPF tool) front end plus a native backend actually looks like.
//
// Framing is the samples' usual `[2B packetId][4B bodyLength][body...]`, all
// little-endian, provided by memorypack/packet.hpp.
//
// The pieces worth copying into a real server:
//
//   1. memorypack::PacketFrameParser for reassembly. TCP is a byte stream: a
//      100-byte send can arrive as 100 one-byte reads, and three sends can
//      arrive glued together. The parser is constructed with this protocol's
//      real maximum body length, and Feed() returns false the moment a frame
//      declares something impossible - at which point the stream is untrusted
//      and the connection must be dropped.
//   2. A tightened memorypack::ReaderOptions on every read of client data, so
//      an implausible length is rejected before anything is allocated.
//      See docs/security.md.
//   3. One MemoryPackWriter per connection, rewound with Clear() between
//      responses, so a busy connection allocates nothing per packet once its
//      send buffer has grown to its working size.
//      See docs/performance.md.
//
// Threading model: one std::thread per client, detached. That is the simplest
// thing that demonstrates the serialization, not a scalability recommendation -
// a real server would use IOCP/epoll and a fixed worker pool.
//
// Run:
//     ./build/samples/CppServer            # listens on 25003
//     ./build/samples/CppServer 25003      # or name the port
//     dotnet run --project samples/CsClient
// ============================================================================

#include "packets.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// -- Platform socket abstraction ----------------------------------------------
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <WinSock2.h>
  #include <WS2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  using socklen_t_compat = int;          // Winsock's accept() takes int*
  #define CLOSE_SOCKET closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  using socket_t = int;
  using socklen_t_compat = socklen_t;
  constexpr socket_t INVALID_SOCKET = -1;
  #define CLOSE_SOCKET close
#endif

namespace {

// -- Protocol limits ----------------------------------------------------------
// Everything a hostile client can influence gets an explicit bound. These are
// the numbers this protocol actually needs, not the library defaults.

constexpr uint16_t DEFAULT_PORT = 25003;

/// Largest packet body this protocol can legitimately produce. Anything bigger
/// aborts the connection instead of being buffered.
constexpr size_t MAX_BODY_LENGTH = 64u * 1024u;

/// Largest SumRequest the server will add up.
constexpr size_t MAX_SUM_VALUES = 4096;

/// Largest number of entities a single SpawnRequest may ask for.
constexpr int32_t MAX_SPAWN_COUNT = 64;

/// Caps applied inside a packet body, on top of the framing limit above.
memorypack::ReaderOptions MakeReaderLimits() {
    memorypack::ReaderOptions limits;
    limits.maxCollectionLength = 4096;        // largest legitimate list
    limits.maxStringLength     = 4096;        // largest legitimate string, in bytes
    limits.maxDepth            = 8;           // deepest legitimate nesting
    return limits;
}

// -- Console ------------------------------------------------------------------
// Several client threads print at once, so serialize the output.

std::mutex g_consoleMutex;

void Log(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    std::cout << line << '\n';
    std::cout.flush();
}

// -- Small helpers ------------------------------------------------------------

/// .NET DateTime.UtcNow.Ticks: 100ns intervals since 0001-01-01 UTC. Sending
/// ticks rather than a Unix timestamp lets the C# side build a DateTime from
/// the value directly.
int64_t UtcNowTicks() {
    using namespace std::chrono;
    constexpr int64_t TICKS_AT_UNIX_EPOCH = 621355968000000000LL;
    const auto micros = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
    return TICKS_AT_UNIX_EPOCH + micros * 10;
}

/// ASCII upper-casing. Deliberately byte-wise: it must not touch the multi-byte
/// UTF-8 sequences MemoryPack strings may carry.
std::string ToUpperAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        out.push_back((c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c);
    }
    return out;
}

std::string ToString(const memorypack::Vector3& v) {
    std::ostringstream os;
    os << '(' << v.x << ", " << v.y << ", " << v.z << ')';
    return os.str();
}

// -- Connection ---------------------------------------------------------------
// Everything one client needs: its socket, its reassembly parser, its reader
// limits, and its reusable send buffer + writer.

class Connection {
public:
    Connection(socket_t sock, int id)
        : sock_(sock),
          id_(id),
          limits_(MakeReaderLimits()),
          parser_(MAX_BODY_LENGTH),      // the real maximum, not the 8MB default
          writer_(sendBuffer_) {         // writer appends to sendBuffer_
        writer_.Reserve(4096);           // grow once, then reuse forever
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    ~Connection() { CLOSE_SOCKET(sock_); }

    /// Receive loop. Returns when the peer disconnects or the stream is refused.
    void Run() {
        Log(Prefix() + "connected");

        std::vector<uint8_t> recvBuffer(4096);
        while (alive_) {
            const auto received = recv(sock_,
                                       reinterpret_cast<char*>(recvBuffer.data()),
                                       static_cast<int>(recvBuffer.size()), 0);
            if (received <= 0) break;   // 0 = orderly shutdown, < 0 = error

            const std::span<const uint8_t> chunk(recvBuffer.data(), static_cast<size_t>(received));

            // Feed() calls back once per COMPLETE frame and returns false when a
            // frame violates the limits handed to the constructor. A false here
            // means the stream can no longer be trusted - close, do not resync.
            const bool ok = parser_.Feed(chunk, [this](uint16_t id, std::span<const uint8_t> body) {
                if (alive_ && !HandlePacket(id, body)) alive_ = false;
            });
            if (!ok) {
                Log(Prefix() + "frame limit violated - closing");
                break;
            }
        }

        Log(Prefix() + "disconnected");
    }

private:
    std::string Prefix() const { return "[conn " + std::to_string(id_) + "] "; }

    // -- Receive side ---------------------------------------------------------

    /// Deserializes one packet body under this connection's limits.
    /// Returns false for malformed input; the caller then drops the connection.
    template<typename T>
    bool DecodeBody(std::span<const uint8_t> body, T& out) {
        memorypack::MemoryPackReader reader(body, limits_);
        // MEMORYPACK_TRY/MEMORYPACK_CATCH_ALL are try/catch when exceptions are
        // enabled and a plain pass-through when they are not, so this same code
        // works in a -fno-exceptions build.
        MEMORYPACK_TRY {
            reader.Read(out);
        } MEMORYPACK_CATCH_ALL {
            Log(Prefix() + "malformed body - closing");
            return false;
        }
        if (reader.Failed()) {
            Log(Prefix() + "decode failed: " + memorypack::ToString(reader.Error()));
            return false;
        }
        return true;
    }

    /// Returns false to close the connection.
    bool HandlePacket(uint16_t rawId, std::span<const uint8_t> body) {
        switch (static_cast<PacketId>(rawId)) {
            case PacketId::EchoRequest:  return HandleEcho(body);
            case PacketId::SumRequest:   return HandleSum(body);
            case PacketId::SpawnRequest: return HandleSpawn(body);

            // Response ids are server -> client only; a client sending one is
            // either broken or probing. Either way, stop talking to it.
            case PacketId::EchoResponse:
            case PacketId::SumResponse:
            case PacketId::SpawnResponse:
            default:
                Log(Prefix() + "unexpected packet id " + std::to_string(rawId) + " - closing");
                return false;
        }
    }

    bool HandleEcho(std::span<const uint8_t> body) {
        EchoRequest request;
        if (!DecodeBody(body, request)) return false;
        Log(Prefix() + "recv EchoRequest  seq=" + std::to_string(request.seq)
            + " text=\"" + request.text + "\"");

        EchoResponse response;
        response.seq             = request.seq;
        response.text            = ToUpperAscii(request.text);
        response.serverTimeTicks = UtcNowTicks();

        Log(Prefix() + "send EchoResponse seq=" + std::to_string(response.seq)
            + " text=\"" + response.text + "\""
            + " ticks=" + std::to_string(response.serverTimeTicks));
        return SendPacket(PacketId::EchoResponse, response);
    }

    bool HandleSum(std::span<const uint8_t> body) {
        SumRequest request;
        if (!DecodeBody(body, request)) return false;

        // A well-formed packet can still be unreasonable. Application-level
        // validation is always the server's job - the reader only guarantees
        // that the bytes decoded safely.
        if (request.values.size() > MAX_SUM_VALUES) {
            Log(Prefix() + "SumRequest with " + std::to_string(request.values.size())
                + " values exceeds the protocol maximum - closing");
            return false;
        }

        int64_t total = 0;
        for (int32_t value : request.values) total += value;

        Log(Prefix() + "recv SumRequest   count=" + std::to_string(request.values.size()));
        Log(Prefix() + "send SumResponse  total=" + std::to_string(total));
        return SendPacket(PacketId::SumResponse, SumResponse{ total });
    }

    bool HandleSpawn(std::span<const uint8_t> body) {
        SpawnRequest request;
        if (!DecodeBody(body, request)) return false;

        int32_t count = request.count;
        if (count < 0) count = 0;
        if (count > MAX_SPAWN_COUNT) count = MAX_SPAWN_COUNT;

        // Deterministic contents so the C# client can assert on them.
        SpawnResponse response;
        response.entities.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) {
            const auto f = static_cast<float>(i);
            Entity entity;
            entity.id       = 1000 + i;
            entity.position = memorypack::Vector3{ f * 1.5f, f * 2.5f, f * -0.5f };
            entity.name     = "Entity_" + std::to_string(i);
            response.entities.push_back(std::move(entity));
        }

        Log(Prefix() + "recv SpawnRequest count=" + std::to_string(request.count)
            + (count != request.count ? " (clamped to " + std::to_string(count) + ")" : ""));
        for (const auto& entity : response.entities) {
            Log(Prefix() + "     entity id=" + std::to_string(entity.id)
                + " pos=" + ToString(entity.position)
                + " name=\"" + entity.name + "\"");
        }
        Log(Prefix() + "send SpawnResponse entities=" + std::to_string(response.entities.size()));
        return SendPacket(PacketId::SpawnResponse, response);
    }

    // -- Send side ------------------------------------------------------------

    /// Serializes `body` behind a packet header into this connection's reusable
    /// buffer, then sends it.
    ///
    /// The header space is reserved FIRST and the body serialized straight
    /// behind it, so the length can be patched in afterwards - one pass, no
    /// temporary buffer, no copy. This is what memorypack::WritePacket() does
    /// internally; it is spelled out here because the writer is a long-lived
    /// per-connection object rather than a fresh one per packet.
    template<typename T>
    bool SendPacket(PacketId id, const T& body) {
        writer_.Clear();                                       // rewinds, keeps capacity
        sendBuffer_.resize(memorypack::PACKET_HEADER_SIZE);    // header placeholder
        writer_.Write(body);                                   // appended behind it
        if (writer_.Failed()) {
            Log(Prefix() + "serialization failed: " + memorypack::ToString(writer_.Error()));
            return false;
        }

        const auto bodyLength =
            static_cast<int32_t>(sendBuffer_.size() - memorypack::PACKET_HEADER_SIZE);
        memorypack::DefaultPacketHeaderPolicy::Write(
            sendBuffer_.data(), static_cast<uint16_t>(id), bodyLength);

        return SendAll(sendBuffer_.data(), sendBuffer_.size());
    }

    bool SendAll(const uint8_t* data, size_t length) {
        size_t sent = 0;
        while (sent < length) {
            const auto n = send(sock_,
                                reinterpret_cast<const char*>(data + sent),
                                static_cast<int>(length - sent), 0);
            if (n <= 0) return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    // Declaration order matters: sendBuffer_ must outlive/precede writer_,
    // which holds a reference to it.
    socket_t                     sock_;
    int                          id_;
    memorypack::ReaderOptions    limits_;
    memorypack::PacketFrameParser parser_;
    std::vector<uint8_t>         sendBuffer_;
    memorypack::MemoryPackWriter writer_;
    bool                         alive_ = true;
};

std::atomic<int> g_nextConnectionId{ 0 };

} // namespace

// -- Entry point ---------------------------------------------------------------

int main(int argc, char** argv) {
    uint16_t port = DEFAULT_PORT;
    if (argc >= 2) {
        const long parsed = std::strtol(argv[1], nullptr, 10);
        if (parsed <= 0 || parsed > 65535) {
            std::cerr << "usage: CppServer [port]\n";
            return 1;
        }
        port = static_cast<uint16_t>(parsed);
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    socket_t listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        std::cerr << "Failed to create listening socket\n";
        return 1;
    }

    // Without SO_REUSEADDR a restart within TIME_WAIT fails to bind.
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "Failed to bind port " << port << "\n";
        CLOSE_SOCKET(listener);
        return 1;
    }
    if (listen(listener, SOMAXCONN) != 0) {
        std::cerr << "Failed to listen on port " << port << "\n";
        CLOSE_SOCKET(listener);
        return 1;
    }

    Log("CppServer listening on port " + std::to_string(port) + " (Ctrl-C to stop)");

    for (;;) {
        sockaddr_in peer{};
        socklen_t_compat peerLength = static_cast<socklen_t_compat>(sizeof(peer));
        socket_t client = accept(listener, reinterpret_cast<sockaddr*>(&peer), &peerLength);
        if (client == INVALID_SOCKET) {
            Log("accept failed - shutting down");
            break;
        }

        char peerIp[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &peer.sin_addr, peerIp, sizeof(peerIp));
        const int id = ++g_nextConnectionId;
        Log("[conn " + std::to_string(id) + "] accepted from "
            + peerIp + ":" + std::to_string(ntohs(peer.sin_port)));

        // One detached thread per client. A production server would keep the
        // handles and join them on shutdown; this sample runs until Ctrl-C.
        std::thread([client, id] {
            Connection connection(client, id);
            connection.Run();
        }).detach();
    }

    CLOSE_SOCKET(listener);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
