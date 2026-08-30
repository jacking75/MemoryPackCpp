// ============================================================================
// samples/ChatClientConsole/main.cpp
//
// Cross-platform console chat client for samples/ChatServer (TCP 25002).
//
// samples/ChatClient is a Win32 GUI application, so on Linux and macOS there is
// no way to exercise the chat protocol at all. This client speaks exactly the
// same protocol - same packet ids 101..108, same packet shapes - from a plain
// console, so it builds and runs everywhere the library does.
//
// Structure:
//   - a background thread owns recv() and a memorypack::PacketFrameParser,
//     turning the TCP byte stream back into whole packets;
//   - the main thread owns stdin and does the sending.
//
// Commands:
//   <text>                 send a message to everyone in the current room
//   /w <user> <message>    whisper to one user
//   /join <room>           leave the current room and join another
//   /quit                  disconnect and exit
//
// Run:
//     # terminal 1
//     cd samples/ChatServer && dotnet run
//     # terminal 2..N
//     ./build/samples/ChatClientConsole alice lobby 127.0.0.1 25002
// ============================================================================

#include "packets.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <span>
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
  #define CLOSE_SOCKET   closesocket
  #define SHUTDOWN_BOTH  SD_BOTH
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  using socket_t = int;
  constexpr socket_t INVALID_SOCKET = -1;
  #define CLOSE_SOCKET   close
  #define SHUTDOWN_BOTH  SHUT_RDWR
#endif

namespace {

constexpr const char* DEFAULT_HOST = "127.0.0.1";
constexpr uint16_t    DEFAULT_PORT = 25002;
constexpr const char* DEFAULT_ROOM = "lobby";

/// Largest chat packet body this protocol can legitimately produce.
constexpr size_t MAX_BODY_LENGTH = 64u * 1024u;

/// Caps applied inside a packet body. Even a client should bound what it
/// decodes: the server it is talking to may not be the one it expects.
memorypack::ReaderOptions MakeReaderLimits() {
    memorypack::ReaderOptions limits;
    limits.maxCollectionLength = 1024;   // largest plausible room roster
    limits.maxStringLength     = 4096;   // largest plausible message, in bytes
    limits.maxDepth            = 8;
    return limits;
}

// The receive thread and the main thread both write to the console.
std::mutex g_consoleMutex;

void Print(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_consoleMutex);
    std::cout << line << '\n';
    std::cout.flush();
}

/// Trims ASCII spaces and tabs from both ends.
std::string Trim(std::string_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) --end;
    return std::string(text.substr(begin, end - begin));
}

bool StartsWith(const std::string& text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

// -- Chat client ---------------------------------------------------------------

class ChatClient {
public:
    ~ChatClient() { Stop(); }

    bool Connect(const std::string& host, uint16_t port) {
        sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_ == INVALID_SOCKET) {
            Print("[error] failed to create a socket");
            return false;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            Print("[error] '" + host + "' is not a valid IPv4 address");
            CLOSE_SOCKET(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }

        if (connect(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            Print("[error] could not connect to " + host + ":" + std::to_string(port));
            CLOSE_SOCKET(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }

        running_ = true;
        receiveThread_ = std::thread([this] { ReceiveLoop(); });
        return true;
    }

    [[nodiscard]] bool IsRunning() const noexcept { return running_; }

    /// Serializes `body` behind a packet header and sends the whole frame.
    template<typename T>
    bool Send(PacketId id, const T& body) {
        std::lock_guard<std::mutex> lock(sendMutex_);
        sendBuffer_.clear();
        // WritePacket reserves the header, serializes the body straight behind
        // it and patches the length - one pass into a buffer we keep reusing.
        memorypack::WritePacket(sendBuffer_, static_cast<uint16_t>(id), body);
        return SendAll(sendBuffer_.data(), sendBuffer_.size());
    }

    /// Closes the connection and joins the receive thread. Safe to call twice.
    void Stop() {
        const bool wasRunning = running_.exchange(false);
        if (sock_ != INVALID_SOCKET) {
            // shutdown() unblocks the recv() the receive thread is sitting in;
            // closing the socket first would leave it reading a dead handle.
            shutdown(sock_, SHUTDOWN_BOTH);
        }
        if (receiveThread_.joinable()) receiveThread_.join();
        if (sock_ != INVALID_SOCKET) {
            CLOSE_SOCKET(sock_);
            sock_ = INVALID_SOCKET;
        }
        if (wasRunning) Print("[system] disconnected");
    }

private:
    void ReceiveLoop() {
        // The parser is constructed with this protocol's real maximum body
        // length; Feed() returns false when a frame declares more than that.
        memorypack::PacketFrameParser parser(MAX_BODY_LENGTH);
        std::vector<uint8_t> buffer(4096);

        while (running_) {
            const auto received = recv(sock_,
                                       reinterpret_cast<char*>(buffer.data()),
                                       static_cast<int>(buffer.size()), 0);
            if (received <= 0) break;   // 0 = server closed, < 0 = error or shutdown()

            const std::span<const uint8_t> chunk(buffer.data(), static_cast<size_t>(received));
            const bool ok = parser.Feed(chunk, [this](uint16_t id, std::span<const uint8_t> body) {
                HandlePacket(id, body);
            });
            if (!ok) {
                Print("[error] the server sent a frame that violates the protocol limits");
                break;
            }
        }

        if (running_.exchange(false)) {
            Print("[system] the connection was closed. Press Enter to exit.");
        }
    }

    /// Deserializes one packet body under this client's limits.
    template<typename T>
    bool DecodeBody(std::span<const uint8_t> body, T& out) const {
        memorypack::MemoryPackReader reader(body, limits_);
        // try/catch when exceptions are enabled, a pass-through when they are not.
        MEMORYPACK_TRY {
            reader.Read(out);
        } MEMORYPACK_CATCH_ALL {
            Print("[error] malformed packet body");
            return false;
        }
        if (reader.Failed()) {
            Print(std::string("[error] decode failed: ") + memorypack::ToString(reader.Error()));
            return false;
        }
        return true;
    }

    void HandlePacket(uint16_t rawId, std::span<const uint8_t> body) {
        switch (static_cast<PacketId>(rawId)) {
            case PacketId::LoginResponse: {
                LoginResponse packet;
                if (!DecodeBody(body, packet)) return;
                Print(packet.success ? "[system] logged in"
                                     : "[system] login rejected: " + packet.message);
                if (!packet.success) running_ = false;
                break;
            }
            case PacketId::RoomJoinResponse: {
                RoomJoinResponse packet;
                if (!DecodeBody(body, packet)) return;
                std::string line = "[system] joined the room. Already here: ";
                if (packet.existingUsers.empty()) {
                    line += "(nobody)";
                } else {
                    for (size_t i = 0; i < packet.existingUsers.size(); ++i) {
                        if (i > 0) line += ", ";
                        line += packet.existingUsers[i];
                    }
                }
                Print(line);
                break;
            }
            case PacketId::RoomChat: {
                RoomChat packet;
                if (!DecodeBody(body, packet)) return;
                Print("<" + packet.senderName + "> " + packet.message);
                break;
            }
            case PacketId::PrivateChat: {
                PrivateChat packet;
                if (!DecodeBody(body, packet)) return;
                Print("*" + packet.senderName + " -> " + packet.targetName + "* " + packet.message);
                break;
            }
            case PacketId::UserEntered: {
                UserEntered packet;
                if (!DecodeBody(body, packet)) return;
                Print("[system] " + packet.username + " entered the room");
                break;
            }
            case PacketId::UserLeft: {
                UserLeft packet;
                if (!DecodeBody(body, packet)) return;
                Print("[system] " + packet.username + " left the room");
                break;
            }

            // Request ids travel client -> server only.
            case PacketId::LoginRequest:
            case PacketId::RoomJoinRequest:
            default:
                Print("[warn] ignoring unexpected packet id " + std::to_string(rawId));
                break;
        }
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

    socket_t                  sock_ = INVALID_SOCKET;
    std::atomic<bool>         running_{ false };
    std::thread               receiveThread_;
    std::mutex                sendMutex_;
    std::vector<uint8_t>      sendBuffer_;
    memorypack::ReaderOptions limits_ = MakeReaderLimits();
};

/// Reads a line from stdin, showing `prompt` first. Returns the trimmed value.
std::string Ask(const char* prompt) {
    std::cout << prompt;
    std::cout.flush();
    std::string line;
    if (!std::getline(std::cin, line)) return {};
    return Trim(line);
}

} // namespace

// -- Entry point ---------------------------------------------------------------

int main(int argc, char** argv) {
    // argv: [username] [room] [host] [port] - all optional.
    std::string username = argc >= 2 ? Trim(argv[1]) : std::string{};
    std::string room     = argc >= 3 ? Trim(argv[2]) : std::string{};
    std::string host     = argc >= 4 ? Trim(argv[3]) : std::string{ DEFAULT_HOST };
    uint16_t    port     = DEFAULT_PORT;
    if (argc >= 5) {
        const long parsed = std::strtol(argv[4], nullptr, 10);
        if (parsed <= 0 || parsed > 65535) {
            std::cerr << "usage: ChatClientConsole [username] [room] [host] [port]\n";
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

    if (username.empty()) username = Ask("Username: ");
    if (username.empty()) {
        std::cerr << "A username is required.\n";
        return 1;
    }
    if (room.empty()) room = Ask("Room [lobby]: ");
    if (room.empty()) room = DEFAULT_ROOM;

    ChatClient client;
    if (!client.Connect(host, port)) {
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    Print("[system] connected to " + host + ":" + std::to_string(port));

    client.Send(PacketId::LoginRequest, LoginRequest{ username });
    client.Send(PacketId::RoomJoinRequest, RoomJoinRequest{ room });

    Print("[system] commands: /w <user> <message> | /join <room> | /quit");

    // The main thread owns stdin; the receive thread owns recv().
    std::string line;
    while (client.IsRunning() && std::getline(std::cin, line)) {
        line = Trim(line);
        if (line.empty()) continue;

        if (line == "/quit") {
            break;
        }
        if (line == "/join" || StartsWith(line, "/join ")) {
            const std::string target = Trim(std::string_view(line).substr(5));
            if (target.empty()) {
                Print("[system] usage: /join <room>");
                continue;
            }
            room = target;
            client.Send(PacketId::RoomJoinRequest, RoomJoinRequest{ room });
            continue;
        }
        if (StartsWith(line, "/w ")) {
            const std::string rest = Trim(std::string_view(line).substr(3));
            const size_t space = rest.find(' ');
            if (space == std::string::npos) {
                Print("[system] usage: /w <user> <message>");
                continue;
            }
            PrivateChat whisper;
            whisper.senderName = username;                    // the server overwrites this
            whisper.targetName = rest.substr(0, space);
            whisper.message    = Trim(std::string_view(rest).substr(space + 1));
            if (whisper.message.empty()) {
                Print("[system] usage: /w <user> <message>");
                continue;
            }
            client.Send(PacketId::PrivateChat, whisper);
            continue;
        }

        // Anything else is a room message. The server substitutes the session's
        // own username for senderName, so a client cannot spoof another user.
        client.Send(PacketId::RoomChat, RoomChat{ username, line });
    }

    client.Stop();
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
