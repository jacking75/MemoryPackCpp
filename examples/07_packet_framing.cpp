// examples/07_packet_framing.cpp
// ============================================================================
// memorypack/packet.hpp: turning a TCP byte stream back into messages.
//
// MemoryPack defines how a VALUE is encoded. It says nothing about where one
// message ends and the next begins - that is the transport's job. TCP is a byte
// stream, not a message stream: a single send() of 100 bytes can arrive as
// 100 one-byte reads, or three sends can arrive glued together in one read.
// Anything that treats "one recv() == one message" is broken; it just has not
// noticed yet.
//
// packet.hpp provides the framing the samples use:
//
//     [2B uint16 packetId][4B int32 bodyLength][body ...]      all little-endian
//
//   MakePacket(id, body)          -> a fresh std::vector holding one framed packet
//   WritePacket(out, id, body)    -> appends one framed packet to a caller's buffer
//   PeekPacketHeader(bytes)       -> decodes a header if enough bytes are present
//   PacketFrameParser             -> reassembles whole packets from stream chunks
//
// WHY WritePacket is not "serialize, then prepend a header": it reserves the
// header space first, serializes the body directly behind it, and patches the
// length afterwards. One pass, no temporary buffer, no copy - the pattern shown
// by hand in 06_fixed_buffer.cpp.
//
// The header layout is a policy template parameter, so a project with a
// different framing (a 4-byte length only, a magic number, a checksum) supplies
// its own Policy and keeps the parser.
// ============================================================================

#include "memorypack/memorypack.hpp"
#include "memorypack/packet.hpp"

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

// -- The protocol -----------------------------------------------------------
// Packet ids are the dispatch key. Keep them in one enum shared with the C#
// side so a renumbering is a compile error rather than a runtime mystery.
enum PacketId : uint16_t {
    PID_LOGIN     = 1,
    PID_CHAT      = 2,
    PID_HEARTBEAT = 3,
};

struct LoginRequest {
    std::string userName;
    int32_t     level = 0;
};
MEMORYPACK_DEFINE(LoginRequest, userName, level)

struct ChatMessage {
    std::string from;
    std::string text;
};
MEMORYPACK_DEFINE(ChatMessage, from, text)

struct Heartbeat {
    int64_t timestamp = 0;
};
MEMORYPACK_DEFINE(Heartbeat, timestamp)

namespace {

void Dump(const char* label, std::span<const uint8_t> bytes) {
    std::printf("%s (%zu bytes)\n", label, bytes.size());
    for (size_t i = 0; i < bytes.size(); i += 16) {
        std::printf("    %04zx  ", i);
        size_t j = 0;
        for (; j < 16 && i + j < bytes.size(); ++j) std::printf("%02X ", bytes[i + j]);
        for (; j < 16; ++j) std::printf("   ");
        std::printf(" |");
        for (j = 0; j < 16 && i + j < bytes.size(); ++j) {
            const uint8_t c = bytes[i + j];
            std::printf("%c", (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.');
        }
        std::printf("|\n");
    }
}

bool g_ok = true;
int  g_delivered = 0;

void Check(const char* what, bool condition) {
    if (!condition) {
        g_ok = false;
        std::printf("  FAILED: %s\n", what);
    }
}

// The receive-side dispatcher: one packet id -> one concrete body type.
void Dispatch(uint16_t id, std::span<const uint8_t> body) {
    ++g_delivered;
    switch (id) {
        case PID_LOGIN: {
            const auto value = memorypack::Deserialize<LoginRequest>(body);
            std::printf("    [%zu B] LOGIN     user=\"%s\" level=%d\n",
                        body.size(), value.userName.c_str(), value.level);
            Check("login body", value.userName == "alice" && value.level == 12);
            break;
        }
        case PID_CHAT: {
            const auto value = memorypack::Deserialize<ChatMessage>(body);
            std::printf("    [%zu B] CHAT      %s: \"%s\"\n",
                        body.size(), value.from.c_str(), value.text.c_str());
            Check("chat body", value.from == "alice");
            break;
        }
        case PID_HEARTBEAT: {
            const auto value = memorypack::Deserialize<Heartbeat>(body);
            std::printf("    [%zu B] HEARTBEAT ts=%lld\n",
                        body.size(), static_cast<long long>(value.timestamp));
            Check("heartbeat body", value.timestamp == 1700000000LL);
            break;
        }
        default:
            // An unknown id from a newer peer. Skipping it is safe precisely
            // because the frame length told us exactly how much to skip.
            std::printf("    [%zu B] (unknown packet id %u - skipped)\n",
                        body.size(), static_cast<unsigned>(id));
            break;
    }
}

} // namespace

int main() {
    std::printf("== 07 packet framing over a TCP-like stream ==\n\n");

    // =======================================================================
    // 1. MakePacket / PeekPacketHeader
    // =======================================================================
    std::printf("--- 1. one packet, header decoded ---\n\n");
    {
        const std::vector<uint8_t> packet =
            memorypack::MakePacket(PID_LOGIN, LoginRequest{"alice", 12});

        Dump("MakePacket(PID_LOGIN, LoginRequest{\"alice\", 12})", packet);

        const auto header = memorypack::PeekPacketHeader(packet);
        Check("header decodes", header.has_value());
        if (header) {
            std::printf("    -> id = %u, bodyLength = %d, header size = %zu\n",
                        static_cast<unsigned>(header->id), header->bodyLength,
                        memorypack::PACKET_HEADER_SIZE);
            Check("header id", header->id == PID_LOGIN);
            Check("header length",
                  static_cast<size_t>(header->bodyLength)
                      == packet.size() - memorypack::PACKET_HEADER_SIZE);
        }

        // PeekPacketHeader on a short read returns nullopt rather than reading
        // uninitialised memory - the "we do not have the header yet" case.
        const auto partial = memorypack::PeekPacketHeader(
            std::span<const uint8_t>(packet.data(), 3));
        Check("short header returns nullopt", !partial.has_value());
        std::printf("    -> PeekPacketHeader on 3 bytes: %s\n\n",
                    partial ? "decoded (wrong!)" : "nullopt, need more bytes");
    }

    // =======================================================================
    // 2. WritePacket: several packets appended to one send buffer.
    //    This is how a real writer batches: one buffer, N packets, one send().
    // =======================================================================
    std::printf("--- 2. three packets in one send buffer ---\n\n");
    std::vector<uint8_t> stream;
    memorypack::WritePacket(stream, PID_LOGIN,     LoginRequest{"alice", 12});
    memorypack::WritePacket(stream, PID_CHAT,      ChatMessage{"alice", "hello framing"});
    memorypack::WritePacket(stream, PID_HEARTBEAT, Heartbeat{1700000000LL});

    Dump("the wire stream", stream);
    std::printf("\n");

    // =======================================================================
    // 3. PacketFrameParser: the receive side.
    //
    //    Feed() takes whatever bytes arrived - a partial header, half a body,
    //    two and a half packets - buffers what it cannot use yet, and invokes
    //    the callback once per COMPLETE packet. The body span it hands over
    //    points into the parser's own buffer and is only valid for the
    //    duration of the callback: deserialize (or copy) inside it.
    //
    //    Here the stream is delivered in deliberately awkward 7-byte chunks,
    //    which is exactly the kind of split a real network produces.
    // =======================================================================
    std::printf("--- 3. reassembly from 7-byte chunks ---\n\n");
    {
        memorypack::PacketFrameParser parser;
        constexpr size_t kChunk = 7;

        for (size_t offset = 0; offset < stream.size(); offset += kChunk) {
            const size_t n = (stream.size() - offset < kChunk) ? stream.size() - offset : kChunk;
            const std::span<const uint8_t> chunk(stream.data() + offset, n);

            std::printf("  feed %zu byte(s) at offset %zu (buffered before: %zu)\n",
                        n, offset, parser.Buffered());

            // Feed returns false when the stream violated the configured
            // limits. That is unrecoverable - close the connection.
            const bool healthy = parser.Feed(chunk, Dispatch);
            Check("stream stays healthy", healthy);
        }

        std::printf("\n    delivered %d packets, %zu byte(s) left buffered\n\n",
                    g_delivered, parser.Buffered());
        Check("all three packets delivered", g_delivered == 3);
        Check("nothing left over", parser.Buffered() == 0);
    }

    // =======================================================================
    // 4. The same stream delivered as one big chunk must behave identically.
    // =======================================================================
    std::printf("--- 4. the same stream in a single chunk ---\n\n");
    {
        g_delivered = 0;
        memorypack::PacketFrameParser parser;
        Check("single-chunk feed succeeds", parser.Feed(stream, Dispatch));
        std::printf("\n    delivered %d packets\n\n", g_delivered);
        Check("single chunk delivers the same three", g_delivered == 3);
    }

    // =======================================================================
    // 5. Hostile input. A length prefix read off the network is attacker
    //    controlled: a header claiming a 2 GB body must not become a 2 GB
    //    allocation. The parser bounds it with MaxBodyLength (8 MB by default)
    //    and returns false instead of buffering, so the caller can drop the
    //    connection before any memory is committed.
    // =======================================================================
    std::printf("--- 5. rejecting an absurd declared length ---\n\n");
    {
        memorypack::PacketFrameParser guarded(1024);   // 1 KB cap for the demo
        std::printf("    MaxBodyLength = %zu\n", guarded.MaxBodyLength());

        std::vector<uint8_t> hostile(memorypack::PACKET_HEADER_SIZE, 0);
        hostile[0] = uint8_t{0x01};                             // id = 1
        hostile[2] = uint8_t{0xFF}; hostile[3] = uint8_t{0xFF}; // bodyLength = 0x7FFFFFFF
        hostile[4] = uint8_t{0xFF}; hostile[5] = uint8_t{0x7F};

        int delivered = 0;
        const bool healthy = guarded.Feed(hostile,
            [&](uint16_t, std::span<const uint8_t>) { ++delivered; });

        std::printf("    header claims a %d byte body -> Feed() returned %s\n",
                    0x7FFFFFFF, healthy ? "true" : "false");
        std::printf("    packets delivered: %d, parser buffer reset: %s\n\n",
                    delivered, guarded.Buffered() == 0 ? "yes" : "no");

        Check("hostile length rejected", !healthy);
        Check("nothing delivered", delivered == 0);
        Check("parser dropped its buffer", guarded.Buffered() == 0);
    }

    std::printf("packet framing %s\n", g_ok ? "OK" : "MISMATCH");
    return g_ok ? 0 : 1;
}
