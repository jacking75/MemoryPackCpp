// examples/10_dotnet_types.cpp
// ============================================================================
// The .NET value types: Guid, DateTime, TimeSpan, Half (and friends).
//
// These are the types that make C#/C++ interop annoying, because C++ has no
// natural equivalent and the wire encoding is "whatever the CLR's in-memory
// layout happens to be". memorypack/dotnet.hpp mirrors each layout exactly -
// every one of them captured from real MemoryPack output and pinned down by the
// interop tests, not guessed from documentation.
//
//   C# type              C++ type                 wire size  encoding
//   ---------------------------------------------------------------------------
//   System.Guid          memorypack::Guid         16 bytes   .NET mixed-endian
//   System.DateTime      memorypack::DateTime      8 bytes   _dateData (kind+ticks)
//   System.TimeSpan      memorypack::TimeSpan      8 bytes   int64 ticks
//   System.Half          memorypack::Half          2 bytes   IEEE 754 binary16
//   System.DateTimeOffset memorypack::DateTimeOffset 16 bytes offset + padding + DateTime
//   System.Decimal       memorypack::Decimal      16 bytes   raw CLR bits
//   System.Int128        memorypack::Int128       16 bytes   little-endian
//   System.Numerics.Vector3 memorypack::Vector3   12 bytes   unmanaged struct
//
// None of them carries an object header: they are all unmanaged value types, so
// MemoryPack copies them the way it copies an int.
// ============================================================================

#include "memorypack/memorypack.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

// A packet using all four headline types together, so the combined layout is
// visible. The C# equivalent:
//
//     [MemoryPackable]
//     public partial class SessionRecord
//     {
//         public Guid     SessionId { get; set; }
//         public DateTime StartedAt { get; set; }
//         public TimeSpan Duration  { get; set; }
//         public Half     Quality   { get; set; }
//     }
struct SessionRecord {
    memorypack::Guid     sessionId;
    memorypack::DateTime startedAt;
    memorypack::TimeSpan duration;
    memorypack::Half     quality;
};
MEMORYPACK_DEFINE(SessionRecord, sessionId, startedAt, duration, quality)

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
    std::printf("== 10 .NET value types ==\n\n");

    using memorypack::DateTime;
    using memorypack::DateTimeKind;
    using memorypack::Guid;
    using memorypack::Half;
    using memorypack::TimeSpan;

    // =======================================================================
    // 1. System.Guid
    //
    //    The 16 wire bytes are Guid.ToByteArray(), which is NOT plain
    //    big-endian and NOT plain little-endian: the first three fields are
    //    little-endian integers (uint32, uint16, uint16) and the last eight
    //    bytes are stored in order. That "mixed-endian" quirk is why a hand
    //    rolled 16-byte memcpy of a UUID usually produces a Guid that C#
    //    prints with the first three groups reversed.
    //
    //    memorypack::Guid stores the same four fields, so Parse/ToString round
    //    trip the canonical text form and the bytes match C# exactly.
    // =======================================================================
    std::printf("--- 1. System.Guid ---\n\n");
    {
        const auto parsed = Guid::Parse("01020304-0506-0708-090a-0b0c0d0e0f10");
        Check("Guid parses", parsed.has_value());
        if (parsed) {
            std::printf("    parsed  : %s\n", parsed->ToString().c_str());

            const std::vector<uint8_t> bytes = memorypack::Serialize(*parsed);
            Dump("    on the wire", bytes);
            std::printf("    -> 04 03 02 01 | 06 05 | 08 07 | 09 0A 0B 0C 0D 0E 0F 10\n");
            std::printf("       ^ uint32 LE   ^u16 LE ^u16 LE ^ eight bytes in order\n\n");

            Check("Guid is 16 bytes", bytes.size() == 16);
            Check("Guid round trip", memorypack::Deserialize<Guid>(bytes) == *parsed);
            Check("Guid text round trip",
                  parsed->ToString() == "01020304-0506-0708-090a-0b0c0d0e0f10");
        }

        // Parse validates: a malformed string yields nullopt rather than junk.
        Check("bad Guid text rejected", !Guid::Parse("not-a-guid").has_value());
        Check("short Guid text rejected",
              !Guid::Parse("01020304-0506-0708-090a-0b0c0d0e0f1").has_value());
        std::printf("    Guid::Parse(\"not-a-guid\") -> nullopt (no exception, no garbage)\n\n");
    }

    // =======================================================================
    // 2. System.DateTime
    //
    //    On the wire it is the CLR's private `_dateData` field: a uint64 whose
    //    top two bits hold the DateTimeKind (Unspecified/Utc/Local) and whose
    //    remaining 62 bits hold the tick count.
    //
    //    A .NET "tick" is 100 nanoseconds since 0001-01-01T00:00:00. That epoch
    //    is 621355968000000000 ticks before the Unix epoch - the constant
    //    memorypack::TICKS_AT_UNIX_EPOCH - which is the entire conversion.
    //
    //    Note that Kind is metadata, not an offset: it does NOT shift the
    //    ticks. Converting a Local DateTime to a time_point gives you the local
    //    wall clock reinterpreted as UTC, exactly as DateTime.Ticks does in C#.
    //    If you care about instants, send Utc.
    // =======================================================================
    std::printf("--- 2. System.DateTime ---\n\n");
    {
        // 638000000000000000 ticks == 2022-09-08T06:13:20 UTC.
        const DateTime dt = DateTime::FromTicks(638000000000000000LL, DateTimeKind::Utc);

        std::printf("    ticks   : %lld\n", static_cast<long long>(dt.GetTicks()));
        std::printf("    kind    : %u (0=Unspecified 1=Utc 2=Local)\n",
                    static_cast<unsigned>(dt.GetKind()));
        std::printf("    dateData: 0x%016llX  <- kind is packed into the top 2 bits\n",
                    static_cast<unsigned long long>(dt.dateData));

        const std::vector<uint8_t> bytes = memorypack::Serialize(dt);
        Dump("    on the wire", bytes);
        std::printf("    -> a single little-endian uint64; the last byte carries both the\n");
        std::printf("       top bits of the tick count and the 2-bit Kind (0x40 == Utc).\n\n");

        Check("DateTime is 8 bytes", bytes.size() == 8);
        Check("DateTime round trip", memorypack::Deserialize<DateTime>(bytes) == dt);
        Check("DateTime kind survives", dt.GetKind() == DateTimeKind::Utc);
        Check("DateTime ticks survive", dt.GetTicks() == 638000000000000000LL);

        // std::chrono bridge, both directions.
        const auto now      = std::chrono::system_clock::now();
        const DateTime asDt = DateTime::FromTimePoint(now);
        const auto back     = asDt.ToTimePoint();
        const auto drift    = std::chrono::duration_cast<std::chrono::seconds>(
                                  back > now ? back - now : now - back);

        std::printf("    std::chrono::now() -> DateTime -> time_point drift: %lld s\n",
                    static_cast<long long>(drift.count()));
        std::printf("    (FromTimePoint stamps Kind=Utc, since a time_point is an instant)\n\n");

        Check("chrono round trip is lossless to the second", drift.count() == 0);
        Check("FromTimePoint marks the value as UTC", asDt.GetKind() == DateTimeKind::Utc);
    }

    // =======================================================================
    // 3. System.TimeSpan
    //
    //    Just an int64 tick count - no kind bits, no epoch, and it may be
    //    negative. 1 second == 10,000,000 ticks.
    // =======================================================================
    std::printf("--- 3. System.TimeSpan ---\n\n");
    {
        const TimeSpan span = TimeSpan::FromDuration(std::chrono::seconds(90));

        std::printf("    90 s          = %lld ticks\n", static_cast<long long>(span.ticks));
        std::printf("    back to chrono= %lld ns\n",
                    static_cast<long long>(span.ToDuration().count()));

        const std::vector<uint8_t> bytes = memorypack::Serialize(span);
        Dump("    on the wire", bytes);
        std::printf("    -> int64 little-endian, nothing else.\n\n");

        Check("TimeSpan is 8 bytes", bytes.size() == 8);
        Check("TimeSpan ticks", span.ticks == 900000000LL);
        Check("TimeSpan to chrono", span.ToDuration() == std::chrono::seconds(90));
        Check("TimeSpan round trip",
              memorypack::Deserialize<TimeSpan>(bytes).ticks == span.ticks);

        // Sub-tick precision is lost by definition: a tick is 100 ns.
        const TimeSpan tiny = TimeSpan::FromDuration(std::chrono::nanoseconds(150));
        std::printf("    150 ns        = %lld ticks (a tick is 100 ns, so this truncates)\n\n",
                    static_cast<long long>(tiny.ticks));
        Check("sub-tick precision truncates", tiny.ticks == 1);
    }

    // =======================================================================
    // 4. System.Half
    //
    //    IEEE 754 binary16: 1 sign bit, 5 exponent bits, 10 mantissa bits.
    //    Two bytes on the wire, which is why it shows up in position and
    //    normal data. C++ has no built-in half type, so memorypack::Half wraps
    //    the raw bits and converts to/from float on demand.
    //
    //    Range is small: the largest finite value is 65504, and anything above
    //    it becomes infinity. Precision is about 3 decimal digits.
    // =======================================================================
    std::printf("--- 4. System.Half ---\n\n");
    {
        const Half quality = Half::FromFloat(0.75f);

        std::printf("    0.75f -> bits 0x%04X -> %.4f\n",
                    static_cast<unsigned>(quality.bits),
                    static_cast<double>(quality.ToFloat()));

        const std::vector<uint8_t> bytes = memorypack::Serialize(quality);
        Dump("    on the wire", bytes);
        std::printf("    -> two bytes, little-endian.\n\n");

        Check("Half is 2 bytes", bytes.size() == 2);
        Check("Half round trip", memorypack::Deserialize<Half>(bytes) == quality);

        std::printf("    exactly representable values survive:\n");
        for (const float f : {0.0f, 1.0f, -1.0f, 0.5f, 1.5f, -0.25f, 65504.0f}) {
            const float back = Half::FromFloat(f).ToFloat();
            std::printf("        %10.4f -> %10.4f  %s\n",
                        static_cast<double>(f), static_cast<double>(back),
                        (back == f) ? "exact" : "LOSSY");
            Check("Half round trips an exactly representable value", back == f);
        }
        std::printf("        %10.4f -> overflows to infinity (max finite Half is 65504)\n\n",
                    100000.0);
    }

    // =======================================================================
    // 5. All four in one object.
    // =======================================================================
    std::printf("--- 5. a record using all four ---\n\n");
    {
        SessionRecord record;
        const auto sessionId = Guid::Parse("2f9a1c40-1111-2222-3333-444455556666");
        Check("session guid parses", sessionId.has_value());
        if (sessionId) record.sessionId = *sessionId;
        record.startedAt = DateTime::FromTicks(638000000000000000LL, DateTimeKind::Utc);
        record.duration  = TimeSpan::FromDuration(std::chrono::minutes(3));
        record.quality   = Half::FromFloat(0.75f);

        const std::vector<uint8_t> bytes = memorypack::Serialize(record);
        Dump("SessionRecord", bytes);
        std::printf("    -> 04 (object header) | 16 Guid | 8 DateTime | 8 TimeSpan | 2 Half\n");
        std::printf("       = 1 + 16 + 8 + 8 + 2 = %zu bytes, with no per-member framing.\n\n",
                    bytes.size());
        Check("SessionRecord size", bytes.size() == 35);

        const auto back = memorypack::Deserialize<SessionRecord>(bytes);
        std::printf("    round-tripped\n");
        std::printf("        sessionId = %s\n", back.sessionId.ToString().c_str());
        std::printf("        startedAt = %lld ticks, kind %u\n",
                    static_cast<long long>(back.startedAt.GetTicks()),
                    static_cast<unsigned>(back.startedAt.GetKind()));
        std::printf("        duration  = %lld ticks (%lld s)\n",
                    static_cast<long long>(back.duration.ticks),
                    static_cast<long long>(back.duration.ticks / memorypack::TICKS_PER_SECOND));
        std::printf("        quality   = %.4f\n\n", static_cast<double>(back.quality.ToFloat()));

        Check("record guid",     back.sessionId == record.sessionId);
        Check("record datetime", back.startedAt == record.startedAt);
        Check("record timespan", back.duration.ticks == record.duration.ticks);
        Check("record half",     back.quality == record.quality);
    }

    // =======================================================================
    // 6. The rest of the family, for reference. Same idea: fixed-size,
    //    header-free copies of the CLR layout.
    // =======================================================================
    std::printf("--- 6. the rest of dotnet.hpp ---\n\n");
    {
        const memorypack::DateTimeOffset dto{
            static_cast<int16_t>(9 * 60),   // UTC+09:00, expressed in minutes
            DateTime::FromTicks(638000000000000000LL, DateTimeKind::Unspecified)};
        const memorypack::Decimal   dec{};
        const memorypack::Int128    i128{0x0123456789ABCDEFULL, 1};
        const memorypack::Vector3   v3{1.0f, 2.0f, 3.0f};

        std::printf("    DateTimeOffset : %zu bytes  (int16 offset, 6 bytes CLR padding, DateTime)\n",
                    memorypack::Serialize(dto).size());
        std::printf("    Decimal        : %zu bytes  (raw CLR bits; kept opaque, no arithmetic)\n",
                    memorypack::Serialize(dec).size());
        std::printf("    Int128         : %zu bytes  (uint64 low, int64 high, little-endian)\n",
                    memorypack::Serialize(i128).size());
        std::printf("    Vector3        : %zu bytes  (an unmanaged struct - see example 05)\n\n",
                    memorypack::Serialize(v3).size());

        Check("DateTimeOffset size", memorypack::Serialize(dto).size() == 16);
        Check("Decimal size",        memorypack::Serialize(dec).size() == 16);
        Check("Int128 size",         memorypack::Serialize(i128).size() == 16);
        Check("Vector3 size",        memorypack::Serialize(v3).size() == 12);
        Check("DateTimeOffset round trip", memorypack::Deserialize<memorypack::DateTimeOffset>(
                                               memorypack::Serialize(dto)) == dto);
    }

    std::printf(".NET types %s\n", g_ok ? "OK" : "MISMATCH");
    return g_ok ? 0 : 1;
}
