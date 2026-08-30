// examples/09_version_tolerance.cpp
// ============================================================================
// Evolving a schema without breaking older peers.
//
// MemoryPack has TWO layouts, and they buy different amounts of tolerance.
//
// A. Default layout - C# [MemoryPackable] / GenerateType.Object
//
//        [1B memberCount][member0][member1]...
//
//    The single count byte is all the version information there is. A reader
//    reads min(itsOwnMemberCount, theSendersMemberCount) members and leaves the
//    rest at their defaults. That gives you exactly one safe schema change:
//    APPEND a member at the END. Inserting, removing or reordering a member
//    silently reinterprets every following byte.
//
// B. VersionTolerant layout - C# GenerateType.VersionTolerant
//
//        [1B memberCount][len0][len1]...[lenN-1][member0][member1]...
//
//    Every member is prefixed with its byte length, so a reader can SKIP a
//    member it does not understand and still land exactly on the next one.
//    That buys forward compatibility for unknown members - at the cost of a
//    length prefix per member on every single message. Use it for long-lived
//    persisted data and slow-moving config; do not use it for hot packets.
//
// This example shows both, including where layout A quietly falls short.
// ============================================================================

#include "memorypack/memorypack.hpp"

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Layout A: the same logical type at two schema versions.
//
//   V1:  [MemoryPackable] class Player { int Id; string Name; }
//   V2:  [MemoryPackable] class Player { int Id; string Name; float Score; }
//
// Score was APPENDED. That is the one safe edit.
// ---------------------------------------------------------------------------
struct PlayerV1 {
    int32_t     id = 0;
    std::string name;
};
MEMORYPACK_DEFINE(PlayerV1, id, name)

struct PlayerV2 {
    int32_t     id = 0;
    std::string name;
    float       score = -1.0f;    // a distinctive default, to prove it survives
};
MEMORYPACK_DEFINE(PlayerV2, id, name, score)

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
    std::printf("== 09 version tolerance ==\n\n");

    using memorypack::MemoryPackReader;
    using memorypack::MemoryPackWriter;
    using memorypack::VersionTolerantReader;
    using memorypack::VersionTolerantWriter;

    // =======================================================================
    // A1. New code reading OLD bytes.
    //     The header says "2 members"; V2's reader stops after two and leaves
    //     score at its default. Nothing to configure - MEMORYPACK_DEFINE emits
    //     the `if (count > index++)` guard for every member.
    // =======================================================================
    std::printf("--- A1. V2 reads V1 bytes (missing member keeps its default) ---\n\n");
    {
        const std::vector<uint8_t> v1Bytes = memorypack::Serialize(PlayerV1{7, "old"});
        Dump("PlayerV1{7, \"old\"}", v1Bytes);
        std::printf("    -> leading 02: only two members were written.\n\n");

        const auto asV2 = memorypack::Deserialize<PlayerV2>(v1Bytes);
        std::printf("    read as PlayerV2: id=%d name=\"%s\" score=%.1f\n\n",
                    asV2.id, asV2.name.c_str(), static_cast<double>(asV2.score));

        Check("V2 reads V1 id",    asV2.id == 7);
        Check("V2 reads V1 name",  asV2.name == "old");
        Check("V2 keeps its default for the absent member", asV2.score == -1.0f);
    }

    // =======================================================================
    // A2. Old code reading NEW bytes.
    //     The header says "3 members"; V1 only knows two, reads them, and
    //     ignores the rest. The VALUE decodes correctly...
    // =======================================================================
    std::printf("--- A2. V1 reads V2 bytes (extra member ignored) ---\n\n");
    const std::vector<uint8_t> v2Bytes = memorypack::Serialize(PlayerV2{9, "new", 12.5f});
    {
        Dump("PlayerV2{9, \"new\", 12.5}", v2Bytes);
        std::printf("    -> leading 03, and four extra bytes for the float.\n\n");

        const auto asV1 = memorypack::Deserialize<PlayerV1>(v2Bytes);
        std::printf("    read as PlayerV1: id=%d name=\"%s\"  (score dropped)\n\n",
                    asV1.id, asV1.name.c_str());

        Check("V1 reads V2 id",   asV1.id == 9);
        Check("V1 reads V2 name", asV1.name == "new");
    }

    // =======================================================================
    // A3. ...but the READER POSITION is now wrong.
    //
    //     This is the limitation people trip over. A standalone message is
    //     fine: the leftover bytes are simply never looked at. But as soon as
    //     the object is followed by anything else - another member, the next
    //     element of a list, the next value in a stream - the old reader is
    //     sitting four bytes too early and everything after it is garbage.
    //
    //     Practical consequences:
    //       * Framing your messages (07_packet_framing.cpp) contains the
    //         damage: each packet gets a fresh, exactly-sized reader.
    //       * DeserializeExact turns the leftover into a loud error in dev.
    //       * If unknown members must be skippable MID-STREAM, you need the
    //         VersionTolerant layout below.
    // =======================================================================
    std::printf("--- A3. why that is not enough: the reader ends up misaligned ---\n\n");
    {
        // A V2 object followed by an unrelated sentinel value.
        std::vector<uint8_t> stream = v2Bytes;
        MemoryPackWriter tail(stream);
        tail.WriteInt32(0x5A5A5A5A);

        MemoryPackReader reader(stream);
        const auto asV1 = reader.Read<PlayerV1>();
        const int32_t sentinel = reader.ReadInt32();

        std::printf("    object read as V1: id=%d name=\"%s\"\n", asV1.id, asV1.name.c_str());
        std::printf("    next value expected 0x5A5A5A5A, actually read 0x%08X\n",
                    static_cast<unsigned>(sentinel));
        std::printf("    -> the stream is DESYNCED: V1 stopped %zu bytes early.\n\n",
                    stream.size() - reader.Position());

        Check("default layout does desync an old reader", sentinel != 0x5A5A5A5A);
    }

    // =======================================================================
    // B1. The VersionTolerant layout.
    //
    //     C#: [MemoryPackable(GenerateType.VersionTolerant)]
    //
    //     VersionTolerantWriter buffers each member so it can emit all the
    //     lengths up front. Finish() flushes; the destructor calls Finish() for
    //     you, which is why the writer lives in its own scope below.
    //
    //     Member lengths use MemoryPack's own length encoding:
    //         <= 127     one byte
    //         <= 65535   0x84 followed by a uint16
    //         otherwise  0x82 followed by a uint32
    // =======================================================================
    std::printf("--- B1. the VersionTolerant layout ---\n\n");
    std::vector<uint8_t> vtBytes;
    {
        MemoryPackWriter writer;
        {
            VersionTolerantWriter vt(writer);
            vt.WriteMember(int32_t{77});                  // member 0: id
            vt.WriteMember(std::string("legacy-mode"));   // member 1: a member v2 dropped
            vt.WriteMember(0.5f);                         // member 2: score
        }   // Finish() runs here
        const std::span<const uint8_t> produced = writer.GetSpan();
        vtBytes.assign(produced.begin(), produced.end());

        Dump("VersionTolerant object with 3 members", vtBytes);
        std::printf("    -> 03            member count\n");
        std::printf("       04 13 04      the three member byte-lengths\n");
        std::printf("       4D 00 00 00   member 0: int32 77\n");
        std::printf("       ...           member 1: string \"legacy-mode\"\n");
        std::printf("       00 00 00 3F   member 2: float 0.5\n\n");
    }

    // =======================================================================
    // B2. Reading it, including SKIPPING the member this version no longer
    //     understands. Because member 1's length is on the wire, the reader
    //     jumps over it without decoding a single byte of it - and lands
    //     exactly on member 2.
    // =======================================================================
    std::printf("--- B2. skipping an unknown member ---\n\n");
    {
        MemoryPackReader reader(vtBytes);
        VersionTolerantReader vt(reader);

        std::printf("    sender wrote %u member(s)\n", static_cast<unsigned>(vt.Count()));
        Check("VT object is not null", !vt.IsNull());
        Check("VT member count", vt.Count() == 3);

        int32_t id = 0;
        float   score = 0.0f;

        Check("read member 0", vt.ReadMember(id));
        Check("skip member 1", vt.SkipMember());          // never decoded at all
        Check("read member 2", vt.ReadMember(score));
        vt.Finish();                                      // position after the object

        std::printf("    id = %d, (member 1 skipped without decoding), score = %.1f\n\n",
                    id, static_cast<double>(score));
        Check("id survived the skip", id == 77);
        Check("score survived the skip", score == 0.5f);
    }

    // =======================================================================
    // B3. And the payoff: an OLD reader that knows only the first member still
    //     ends up correctly positioned, so whatever follows stays readable.
    //     Compare with A3, where the same situation desynced the stream.
    // =======================================================================
    std::printf("--- B3. an old reader stays aligned (compare with A3) ---\n\n");
    {
        std::vector<uint8_t> stream = vtBytes;
        MemoryPackWriter tail(stream);
        tail.WriteInt32(0x5A5A5A5A);

        MemoryPackReader reader(stream);
        int32_t id = 0;
        {
            VersionTolerantReader vt(reader);
            vt.ReadMember(id);      // this build only knows member 0
            vt.Finish();            // skips members 1 and 2 using their lengths
        }
        const int32_t sentinel = reader.ReadInt32();

        std::printf("    id = %d, next value = 0x%08X (expected 0x5A5A5A5A)\n",
                    id, static_cast<unsigned>(sentinel));
        std::printf("    -> still aligned; the two unknown members were skipped exactly.\n\n");

        Check("VT reader stays aligned", sentinel == 0x5A5A5A5A);
        Check("VT reader got the known member", id == 77);
    }

    // =======================================================================
    // Cost comparison, so the trade-off is a number rather than a feeling.
    // =======================================================================
    std::printf("--- cost of the two layouts ---\n\n");
    {
        const size_t defaultSize = memorypack::Serialize(PlayerV2{77, "legacy-mode", 0.5f}).size();
        std::printf("    default layout      : %zu bytes\n", defaultSize);
        std::printf("    VersionTolerant     : %zu bytes  (+%zu for the length prefixes)\n\n",
                    vtBytes.size(), vtBytes.size() - defaultSize);
    }

    std::printf("rules of thumb\n");
    std::printf("    * default layout: only ever APPEND members, at the end\n");
    std::printf("    * never reorder, never remove, never change a member's type\n");
    std::printf("    * frame your messages so a stale reader cannot desync the stream\n");
    std::printf("    * reach for VersionTolerant only for persisted / slow-moving data\n\n");

    std::printf("version tolerance %s\n", g_ok ? "OK" : "MISMATCH");
    return g_ok ? 0 : 1;
}
