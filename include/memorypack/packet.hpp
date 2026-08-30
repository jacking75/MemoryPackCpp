#pragma once
/// @file packet.hpp
/// @brief Optional TCP packet framing helpers.
///
/// MemoryPack itself only defines how a value is encoded, not how messages are
/// delimited on a stream. This header provides the `[2B packetId][4B bodyLength]`
/// framing used by the samples, plus a reassembly parser for TCP streams. Nothing
/// in the core library depends on it.

#include "memorypack/core.hpp"

#include <functional>

namespace memorypack {

/// Default frame layout: `[2B uint16 packetId][4B int32 bodyLength][body...]`,
/// all little-endian. Swap in your own policy for a different header.
struct DefaultPacketHeaderPolicy {
    static constexpr size_t HeaderSize = 6;

    static void Write(uint8_t* dst, uint16_t id, int32_t bodyLength) noexcept {
        const auto leId = detail::endian_convert(id);
        const auto leLen = detail::endian_convert(bodyLength);
        std::memcpy(dst, &leId, sizeof(leId));
        std::memcpy(dst + sizeof(leId), &leLen, sizeof(leLen));
    }

    static void Read(const uint8_t* src, uint16_t& id, int32_t& bodyLength) noexcept {
        uint16_t rawId;
        int32_t rawLen;
        std::memcpy(&rawId, src, sizeof(rawId));
        std::memcpy(&rawLen, src + sizeof(rawId), sizeof(rawLen));
        id = detail::endian_convert(rawId);
        bodyLength = detail::endian_convert(rawLen);
    }
};

/// Size of the default packet header, in bytes.
inline constexpr size_t PACKET_HEADER_SIZE = DefaultPacketHeaderPolicy::HeaderSize;

/// Decoded packet header.
struct PacketHeader {
    uint16_t id = 0;
    int32_t bodyLength = 0;
};

/// Serializes `body` into `out` behind a packet header, in a single pass:
/// header space is reserved first and the length is patched afterwards, so the
/// body is never serialized twice or copied.
template<typename Policy = DefaultPacketHeaderPolicy, typename T>
void WritePacket(std::vector<uint8_t>& out, uint16_t id, const T& body) {
    const size_t headerAt = out.size();
    out.resize(headerAt + Policy::HeaderSize);

    MemoryPackWriter writer(out);
    writer.Write(body);

    const size_t bodyLength = out.size() - headerAt - Policy::HeaderSize;
    Policy::Write(out.data() + headerAt, id, static_cast<int32_t>(bodyLength));
}

/// Serializes `body` into a fresh buffer behind a packet header.
template<typename Policy = DefaultPacketHeaderPolicy, typename T>
[[nodiscard]] std::vector<uint8_t> MakePacket(uint16_t id, const T& body) {
    std::vector<uint8_t> out;
    WritePacket<Policy>(out, id, body);
    return out;
}

/// Reads a packet header from the front of `data`. Returns nullopt when there
/// are not enough bytes yet.
template<typename Policy = DefaultPacketHeaderPolicy>
[[nodiscard]] std::optional<PacketHeader> PeekPacketHeader(std::span<const uint8_t> data) noexcept {
    if (data.size() < Policy::HeaderSize) return std::nullopt;
    PacketHeader h;
    Policy::Read(data.data(), h.id, h.bodyLength);
    return h;
}

/// Reassembles length-prefixed packets from a TCP byte stream.
///
///     memorypack::PacketFrameParser parser;
///     parser.Feed(receivedBytes, [](uint16_t id, std::span<const uint8_t> body) {
///         // dispatch on id, deserialize body
///     });
///
/// Feed() returns false once a frame violates the configured limits; the caller
/// should then close the connection, since the stream can no longer be trusted.
template<typename Policy = DefaultPacketHeaderPolicy>
class BasicPacketFrameParser {
public:
    explicit BasicPacketFrameParser(size_t maxBodyLength = 8u * 1024u * 1024u)
        : maxBodyLength_(maxBodyLength) {}

    /// Maximum accepted body length. A larger declared length aborts parsing.
    [[nodiscard]] size_t MaxBodyLength() const noexcept { return maxBodyLength_; }
    void SetMaxBodyLength(size_t value) noexcept { maxBodyLength_ = value; }

    /// Bytes currently held while waiting for the rest of a frame.
    [[nodiscard]] size_t Buffered() const noexcept { return buffer_.size() - consumed_; }

    void Reset() noexcept {
        buffer_.clear();
        consumed_ = 0;
    }

    template<typename OnPacket>
    bool Feed(std::span<const uint8_t> bytes, OnPacket&& onPacket) {
        buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());

        for (;;) {
            const size_t available = buffer_.size() - consumed_;
            if (available < Policy::HeaderSize) break;

            uint16_t id = 0;
            int32_t bodyLength = 0;
            Policy::Read(buffer_.data() + consumed_, id, bodyLength);

            if (bodyLength < 0 || static_cast<size_t>(bodyLength) > maxBodyLength_) {
                Reset();
                return false;
            }
            const size_t frameSize = Policy::HeaderSize + static_cast<size_t>(bodyLength);
            if (available < frameSize) break;

            onPacket(id, std::span<const uint8_t>(
                buffer_.data() + consumed_ + Policy::HeaderSize,
                static_cast<size_t>(bodyLength)));
            consumed_ += frameSize;
        }

        Compact();
        return true;
    }

private:
    void Compact() {
        if (consumed_ == 0) return;
        if (consumed_ == buffer_.size()) {
            buffer_.clear();
        } else if (consumed_ > 4096 || consumed_ * 2 >= buffer_.size()) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed_));
        } else {
            return;
        }
        consumed_ = 0;
    }

    std::vector<uint8_t> buffer_;
    size_t consumed_ = 0;
    size_t maxBodyLength_;
};

using PacketFrameParser = BasicPacketFrameParser<DefaultPacketHeaderPolicy>;

} // namespace memorypack
