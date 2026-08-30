// examples/06_fixed_buffer.cpp
// ============================================================================
// Zero-allocation serialization: three buffer modes and when to use each.
//
// memorypack::Serialize(value) is convenient but allocates a fresh
// std::vector every call. In a send loop that runs thousands of times a second
// that allocation is often the most expensive thing in the whole path - the
// encoding itself is little more than a memcpy. MemoryPackWriter therefore
// supports three buffer modes:
//
//   1. Owned (default ctor)          grows internally; TakeBuffer() moves it out,
//                                    Clear() rewinds and KEEPS the capacity.
//   2. External std::vector<uint8_t>& appends to a caller-owned vector, keeping
//                                    its size() exact after every write. This is
//                                    what makes the "reserve a header, serialize,
//                                    patch the length" pattern work.
//   3. External fixed buffer         a std::array / span / pointer+capacity.
//                                    Never allocates; overflow is an error.
//
// This example covers all three, plus RemainingCapacity() and what an overflow
// actually does.
// ============================================================================

#include "memorypack/memorypack.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

// A small hot-path message: 1 header byte + 4 + 4 + 4 + 4 = 17 bytes.
struct Telemetry {
    int32_t tick = 0;
    float   x    = 0.0f;
    float   y    = 0.0f;
    float   z    = 0.0f;
};
MEMORYPACK_DEFINE(Telemetry, tick, x, y, z)

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

void Check(const char* what, bool condition) {
    if (!condition) {
        g_ok = false;
        std::printf("  FAILED: %s\n", what);
    }
}

} // namespace

int main() {
    std::printf("== 06 fixed buffers and zero-allocation serialization ==\n\n");

    // =======================================================================
    // 1. SerializeTo: straight into a stack std::array. No heap involved.
    // =======================================================================
    std::printf("--- 1. SerializeTo into a std::array (no allocation) ---\n\n");
    {
        std::array<uint8_t, 64> buffer{};
        const size_t written = memorypack::SerializeTo(std::span<uint8_t>(buffer),
                                                       Telemetry{1, 1.0f, 2.0f, 3.0f});

        Dump("Telemetry{1, 1, 2, 3}", std::span<const uint8_t>(buffer.data(), written));
        std::printf("    -> %zu of %zu bytes used; the array lives on the stack.\n\n",
                    written, buffer.size());

        // Deserialize reads back from the used prefix only.
        const auto back = memorypack::Deserialize<Telemetry>(
            std::span<const uint8_t>(buffer.data(), written));
        Check("SerializeTo round trip", back.tick == 1 && back.z == 3.0f);
        Check("SerializeTo size", written == 17);
    }

    // =======================================================================
    // 2. A reused writer over one fixed buffer. Clear() rewinds the write
    //    position (and resets the error state); the buffer itself is untouched.
    //    This is the shape of a real send loop: one scratch buffer for the
    //    lifetime of the connection, reused for every outgoing message.
    // =======================================================================
    std::printf("--- 2. One writer, one buffer, reused per message ---\n\n");
    {
        std::array<uint8_t, 256> scratch{};
        memorypack::MemoryPackWriter writer(scratch);

        std::printf("    capacity %zu, RemainingCapacity() before any write: %zu\n",
                    scratch.size(), writer.RemainingCapacity());

        size_t totalSent = 0;
        for (int32_t tick = 1; tick <= 3; ++tick) {
            writer.Clear();                       // rewind, keep the buffer
            writer.Write(Telemetry{tick, static_cast<float>(tick), 0.0f, 0.0f});

            // GetSpan() is the exact bytes to hand to send()/write().
            const std::span<const uint8_t> frame = writer.GetSpan();
            totalSent += frame.size();

            std::printf("    tick %d -> %zu bytes, RemainingCapacity() = %zu\n",
                        tick, frame.size(), writer.RemainingCapacity());

            const auto back = memorypack::Deserialize<Telemetry>(frame);
            Check("reused-writer round trip", back.tick == tick);
        }
        std::printf("    %zu bytes produced with zero heap allocations.\n\n", totalSent);
        Check("three messages sent", totalSent == 3 * 17);

        // Note: RemainingCapacity() reports SIZE_MAX for the growable modes,
        // because there is no fixed limit to report.
        memorypack::MemoryPackWriter growable;
        Check("growable writer reports an unbounded capacity",
              growable.RemainingCapacity() == static_cast<size_t>(-1));
    }

    // =======================================================================
    // 3. Reserve a header, serialize the body, patch the length.
    //
    //    This is THE pattern for length-prefixed protocols, and the reason
    //    MemoryPackWriter supports an external std::vector: the vector's
    //    size() stays exact after every write, so the body length is simply
    //    (size() - headerSize) once serialization finishes. The body is
    //    serialized once, into its final place - no temporary buffer, no
    //    second pass to measure it, no copy.
    //
    //    memorypack/packet.hpp already implements exactly this (see
    //    07_packet_framing.cpp); it is spelled out here so the mechanism is
    //    clear and so you can adapt it to your own header layout.
    // =======================================================================
    std::printf("--- 3. Reserve header -> serialize -> patch length ---\n\n");
    {
        constexpr size_t kHeaderSize = 6;      // [2B packetId][4B bodyLength]
        constexpr uint16_t kPacketId = 0x2A;

        std::vector<uint8_t> sendBuffer;
        sendBuffer.reserve(128);               // one allocation for the connection

        // Step A: make room for the header we cannot fill in yet.
        sendBuffer.resize(kHeaderSize);

        // Step B: serialize the body straight onto the end of the same vector.
        {
            memorypack::MemoryPackWriter writer(sendBuffer);
            writer.Write(Telemetry{99, 4.0f, 5.0f, 6.0f});
            Check("external-vector writer keeps size() exact",
                  writer.Size() == sendBuffer.size());
        }

        // Step C: now the length is known - patch it into the reserved space.
        // The wire format is little-endian everywhere, so the bytes are written
        // out explicitly rather than memcpy'ing host integers (which would be
        // wrong on a big-endian host).
        const size_t bodyLength = sendBuffer.size() - kHeaderSize;
        sendBuffer[0] = static_cast<uint8_t>(kPacketId & 0xFFu);
        sendBuffer[1] = static_cast<uint8_t>((kPacketId >> 8) & 0xFFu);
        sendBuffer[2] = static_cast<uint8_t>(bodyLength & 0xFFu);
        sendBuffer[3] = static_cast<uint8_t>((bodyLength >> 8) & 0xFFu);
        sendBuffer[4] = static_cast<uint8_t>((bodyLength >> 16) & 0xFFu);
        sendBuffer[5] = static_cast<uint8_t>((bodyLength >> 24) & 0xFFu);

        Dump("framed packet", sendBuffer);
        std::printf("    -> [2A 00 id][11 00 00 00 length=%zu][17-byte body]\n\n", bodyLength);

        Check("body length", bodyLength == 17);
        const auto back = memorypack::Deserialize<Telemetry>(
            std::span<const uint8_t>(sendBuffer.data() + kHeaderSize, bodyLength));
        Check("framed body round trip", back.tick == 99 && back.y == 5.0f);
    }

    // =======================================================================
    // 4. Overflow. A fixed buffer cannot grow, so running out of room is a
    //    real error rather than a silent truncation.
    //
    //    In the default build (exceptions enabled) the writer throws
    //    memorypack::MemoryPackException, whose code() and offset() say what
    //    happened and where. In a MEMORYPACK_NO_EXCEPTIONS build nothing is
    //    thrown: the writer records the error, every further write is a no-op,
    //    Failed() becomes true, and SerializeTo returns 0.
    //
    //    Either way, nothing is written past the end of the buffer.
    // =======================================================================
    std::printf("--- 4. Overflowing a fixed buffer ---\n\n");
    {
        std::array<uint8_t, 8> tiny{};         // a Telemetry needs 17
        std::printf("    buffer capacity: %zu, message size: 17\n", tiny.size());

#if MEMORYPACK_HAS_EXCEPTIONS
        bool threw = false;
        try {
            const size_t written = memorypack::SerializeTo(std::span<uint8_t>(tiny),
                                                           Telemetry{1, 1.0f, 2.0f, 3.0f});
            std::printf("    unexpectedly wrote %zu bytes\n", written);
        } catch (const memorypack::MemoryPackException& e) {
            threw = true;
            std::printf("    threw: %s\n", e.what());
            std::printf("    code : %s\n", memorypack::ToString(e.code()));
            std::printf("    at   : offset %zu (where the write was refused)\n", e.offset());
        }
        Check("overflow is reported", threw);
#else
        const size_t written = memorypack::SerializeTo(std::span<uint8_t>(tiny),
                                                       Telemetry{1, 1.0f, 2.0f, 3.0f});
        std::printf("    SerializeTo returned %zu (0 == did not fit)\n", written);
        Check("overflow is reported", written == 0);
#endif

        // The error state is also inspectable without exceptions: after a
        // failure the writer stays failed until Clear() resets it.
        memorypack::MemoryPackWriter writer(tiny);
        writer.WriteInt32(1);
        writer.WriteInt32(2);
        std::printf("    after two int32 writes: Size()=%zu RemainingCapacity()=%zu Failed()=%s\n",
                    writer.Size(), writer.RemainingCapacity(),
                    writer.Failed() ? "true" : "false");
        Check("exactly full", writer.Size() == 8 && writer.RemainingCapacity() == 0);
        Check("still healthy when exactly full", !writer.Failed());

        writer.Clear();
        Check("Clear() rewinds", writer.Size() == 0 && writer.RemainingCapacity() == 8);
    }

    std::printf("\nfixed buffers %s\n", g_ok ? "OK" : "MISMATCH");
    return g_ok ? 0 : 1;
}
