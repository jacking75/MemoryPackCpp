#pragma once
/// @file dotnet.hpp
/// @brief C++ mappings for the .NET value types MemoryPack serializes as
///        fixed-size unmanaged blobs: Guid, DateTime, TimeSpan, DateTimeOffset,
///        decimal, Half, Int128/UInt128.
///
/// Every layout here was captured from real MemoryPack output (see
/// tests/fixtures/report.txt) and is locked down by the interop tests.

#include "memorypack/core.hpp"

#include <chrono>
#include <compare>
#include <cstdint>
#include <cstdio>

namespace memorypack {

/// .NET ticks are 100-nanosecond units since 0001-01-01T00:00:00.
using Ticks = int64_t;

/// Ticks between 0001-01-01 and the Unix epoch (1970-01-01).
inline constexpr Ticks TICKS_AT_UNIX_EPOCH = 621355968000000000LL;
inline constexpr Ticks TICKS_PER_SECOND = 10000000LL;

// -- System.Guid ----------------------------------------------------------------

/// C# System.Guid. On the wire it is the 16-byte .NET layout, which is the same
/// as Guid.ToByteArray(): a little-endian int32, two little-endian int16 fields,
/// then eight bytes in order.
struct Guid {
    uint32_t data1 = 0;
    uint16_t data2 = 0;
    uint16_t data3 = 0;
    std::array<uint8_t, 8> data4{};

    friend bool operator==(const Guid&, const Guid&) = default;

    /// Builds a Guid from its 16 wire bytes.
    static Guid FromBytes(std::span<const uint8_t, 16> bytes) noexcept {
        Guid g;
        std::memcpy(&g.data1, bytes.data() + 0, 4);
        std::memcpy(&g.data2, bytes.data() + 4, 2);
        std::memcpy(&g.data3, bytes.data() + 6, 2);
        std::memcpy(g.data4.data(), bytes.data() + 8, 8);
        g.data1 = detail::endian_convert(g.data1);
        g.data2 = detail::endian_convert(g.data2);
        g.data3 = detail::endian_convert(g.data3);
        return g;
    }

    /// Parses the canonical "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" form.
    /// Returns nullopt when the text is not a well-formed GUID.
    static std::optional<Guid> Parse(std::string_view text) noexcept {
        if (text.size() != 36) return std::nullopt;
        if (text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
            return std::nullopt;

        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        auto readHex = [&](size_t at, size_t digits, uint64_t& out) -> bool {
            uint64_t v = 0;
            for (size_t i = 0; i < digits; ++i) {
                int d = hex(text[at + i]);
                if (d < 0) return false;
                v = (v << 4) | static_cast<uint64_t>(d);
            }
            out = v;
            return true;
        };

        Guid g;
        uint64_t v = 0;
        if (!readHex(0, 8, v)) return std::nullopt;
        g.data1 = static_cast<uint32_t>(v);
        if (!readHex(9, 4, v)) return std::nullopt;
        g.data2 = static_cast<uint16_t>(v);
        if (!readHex(14, 4, v)) return std::nullopt;
        g.data3 = static_cast<uint16_t>(v);
        if (!readHex(19, 2, v)) return std::nullopt;
        g.data4[0] = static_cast<uint8_t>(v);
        if (!readHex(21, 2, v)) return std::nullopt;
        g.data4[1] = static_cast<uint8_t>(v);
        for (size_t i = 0; i < 6; ++i) {
            if (!readHex(24 + i * 2, 2, v)) return std::nullopt;
            g.data4[2 + i] = static_cast<uint8_t>(v);
        }
        return g;
    }

    /// Formats as "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".
    [[nodiscard]] std::string ToString() const {
        char buf[37];
        std::snprintf(buf, sizeof(buf),
                      "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                      data1, data2, data3,
                      data4[0], data4[1], data4[2], data4[3],
                      data4[4], data4[5], data4[6], data4[7]);
        return std::string(buf, 36);
    }
};

template<>
struct MemoryPackFormatter<Guid> {
    static void Serialize(MemoryPackWriter& w, const Guid& v) {
        w.WriteUInt32(v.data1);
        w.WriteUInt16(v.data2);
        w.WriteUInt16(v.data3);
        w.WriteBytes(std::span<const uint8_t>(v.data4.data(), v.data4.size()));
    }
    static void Deserialize(MemoryPackReader& r, Guid& v) {
        v.data1 = r.ReadUInt32();
        v.data2 = r.ReadUInt16();
        v.data3 = r.ReadUInt16();
        for (auto& b : v.data4) b = r.ReadUInt8();
    }
};

// -- System.DateTime ------------------------------------------------------------

enum class DateTimeKind : uint8_t { Unspecified = 0, Utc = 1, Local = 2 };

/// C# System.DateTime. On the wire it is the 8-byte `_dateData` field: the top
/// two bits carry DateTimeKind and the remaining 62 bits carry the tick count.
struct DateTime {
    uint64_t dateData = 0;

    friend bool operator==(const DateTime&, const DateTime&) = default;

    [[nodiscard]] Ticks GetTicks() const noexcept {
        return static_cast<Ticks>(dateData & 0x3FFFFFFFFFFFFFFFULL);
    }

    [[nodiscard]] DateTimeKind GetKind() const noexcept {
        return static_cast<DateTimeKind>((dateData >> 62) & 0x3ULL);
    }

    static DateTime FromTicks(Ticks ticks, DateTimeKind kind = DateTimeKind::Unspecified) noexcept {
        DateTime dt;
        dt.dateData = (static_cast<uint64_t>(ticks) & 0x3FFFFFFFFFFFFFFFULL)
                    | (static_cast<uint64_t>(kind) << 62);
        return dt;
    }

    /// Converts from a std::chrono time point (treated as UTC).
    static DateTime FromTimePoint(std::chrono::system_clock::time_point tp) noexcept {
        auto since = tp.time_since_epoch();
        auto hundredNs =
            std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<1, 10000000>>>(since);
        return FromTicks(hundredNs.count() + TICKS_AT_UNIX_EPOCH, DateTimeKind::Utc);
    }

    /// Converts to a std::chrono time point. The DateTimeKind is not applied;
    /// a Local value is returned as-is, exactly like DateTime.Ticks in C#.
    [[nodiscard]] std::chrono::system_clock::time_point ToTimePoint() const noexcept {
        std::chrono::duration<int64_t, std::ratio<1, 10000000>> d(GetTicks() - TICKS_AT_UNIX_EPOCH);
        return std::chrono::system_clock::time_point(
            std::chrono::duration_cast<std::chrono::system_clock::duration>(d));
    }
};

template<>
struct MemoryPackFormatter<DateTime> {
    static void Serialize(MemoryPackWriter& w, const DateTime& v) { w.WriteUInt64(v.dateData); }
    static void Deserialize(MemoryPackReader& r, DateTime& v) { v.dateData = r.ReadUInt64(); }
};

// -- System.TimeSpan ------------------------------------------------------------

/// C# System.TimeSpan: an 8-byte tick count.
struct TimeSpan {
    Ticks ticks = 0;

    friend auto operator<=>(const TimeSpan&, const TimeSpan&) = default;

    template<typename Rep, typename Period>
    static TimeSpan FromDuration(std::chrono::duration<Rep, Period> d) noexcept {
        auto hundredNs =
            std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<1, 10000000>>>(d);
        return TimeSpan{ hundredNs.count() };
    }

    [[nodiscard]] std::chrono::nanoseconds ToDuration() const noexcept {
        return std::chrono::nanoseconds(ticks * 100);
    }
};

template<>
struct MemoryPackFormatter<TimeSpan> {
    static void Serialize(MemoryPackWriter& w, const TimeSpan& v) { w.WriteInt64(v.ticks); }
    static void Deserialize(MemoryPackReader& r, TimeSpan& v) { v.ticks = r.ReadInt64(); }
};

// -- System.DateTimeOffset ------------------------------------------------------

/// C# System.DateTimeOffset, copied verbatim as its 16-byte runtime layout.
/// The CLR places the 2-byte offset first and the 8-byte DateTime at offset 8;
/// this order is asserted by the interop fixtures rather than assumed.
struct DateTimeOffset {
    int16_t offsetMinutes = 0;
    DateTime dateTime{};

    friend bool operator==(const DateTimeOffset&, const DateTimeOffset&) = default;
};

template<>
struct MemoryPackFormatter<DateTimeOffset> {
    static void Serialize(MemoryPackWriter& w, const DateTimeOffset& v) {
        w.WriteInt16(v.offsetMinutes);
        for (int i = 0; i < 6; ++i) w.WriteUInt8(0);   // padding
        w.WriteUInt64(v.dateTime.dateData);
    }
    static void Deserialize(MemoryPackReader& r, DateTimeOffset& v) {
        v.offsetMinutes = r.ReadInt16();
        r.Advance(6);
        v.dateTime.dateData = r.ReadUInt64();
    }
};

// -- System.Decimal -------------------------------------------------------------

/// C# System.Decimal, kept as its raw 16-byte representation so values survive a
/// round trip exactly. Arithmetic is deliberately not provided.
struct Decimal {
    std::array<uint8_t, 16> bits{};

    friend bool operator==(const Decimal&, const Decimal&) = default;
};

template<>
struct MemoryPackFormatter<Decimal> {
    static void Serialize(MemoryPackWriter& w, const Decimal& v) {
        w.WriteBytes(std::span<const uint8_t>(v.bits.data(), v.bits.size()));
    }
    static void Deserialize(MemoryPackReader& r, Decimal& v) {
        for (auto& b : v.bits) b = r.ReadUInt8();
    }
};

// -- System.Half ----------------------------------------------------------------

/// C# System.Half: an IEEE 754 binary16 value, 2 bytes on the wire.
struct Half {
    uint16_t bits = 0;

    friend bool operator==(const Half&, const Half&) = default;

    /// Converts from float (round-to-nearest-even, with overflow to infinity).
    static Half FromFloat(float value) noexcept {
        uint32_t x;
        std::memcpy(&x, &value, 4);
        const uint32_t sign = (x >> 16) & 0x8000u;
        int32_t exp = static_cast<int32_t>((x >> 23) & 0xFFu) - 127 + 15;
        uint32_t mant = x & 0x007FFFFFu;

        Half h;
        if (((x >> 23) & 0xFFu) == 0xFFu) {                 // Inf / NaN
            h.bits = static_cast<uint16_t>(sign | 0x7C00u | (mant ? 0x0200u : 0u));
            return h;
        }
        if (exp >= 0x1F) {                                   // overflow
            h.bits = static_cast<uint16_t>(sign | 0x7C00u);
            return h;
        }
        if (exp <= 0) {                                      // subnormal / zero
            if (exp < -10) { h.bits = static_cast<uint16_t>(sign); return h; }
            mant |= 0x00800000u;
            const uint32_t shift = static_cast<uint32_t>(14 - exp);
            uint32_t sub = mant >> shift;
            if ((mant >> (shift - 1)) & 1u) ++sub;           // round half up
            h.bits = static_cast<uint16_t>(sign | sub);
            return h;
        }
        uint32_t half = (static_cast<uint32_t>(exp) << 10) | (mant >> 13);
        if ((mant >> 12) & 1u) ++half;                       // round half up
        h.bits = static_cast<uint16_t>(sign | half);
        return h;
    }

    [[nodiscard]] float ToFloat() const noexcept {
        const uint32_t sign = static_cast<uint32_t>(bits & 0x8000u) << 16;
        const uint32_t exp = (bits >> 10) & 0x1Fu;
        const uint32_t mant = bits & 0x03FFu;
        uint32_t out;
        if (exp == 0) {
            if (mant == 0) {
                out = sign;
            } else {
                // Subnormal: normalise it.
                uint32_t e = 127 - 15 + 1;
                uint32_t m = mant;
                while ((m & 0x0400u) == 0) { m <<= 1; --e; }
                m &= 0x03FFu;
                out = sign | (e << 23) | (m << 13);
            }
        } else if (exp == 0x1F) {
            out = sign | 0x7F800000u | (mant << 13);
        } else {
            out = sign | ((exp - 15 + 127) << 23) | (mant << 13);
        }
        float f;
        std::memcpy(&f, &out, 4);
        return f;
    }
};

template<>
struct MemoryPackFormatter<Half> {
    static void Serialize(MemoryPackWriter& w, const Half& v) { w.WriteUInt16(v.bits); }
    static void Deserialize(MemoryPackReader& r, Half& v) { v.bits = r.ReadUInt16(); }
};

// -- System.Int128 / System.UInt128 ---------------------------------------------

/// C# System.Int128 / System.UInt128: 16 little-endian bytes.
struct Int128 {
    uint64_t low = 0;
    int64_t high = 0;
    friend bool operator==(const Int128&, const Int128&) = default;
};

struct UInt128 {
    uint64_t low = 0;
    uint64_t high = 0;
    friend bool operator==(const UInt128&, const UInt128&) = default;
};

template<>
struct MemoryPackFormatter<Int128> {
    static void Serialize(MemoryPackWriter& w, const Int128& v) {
        w.WriteUInt64(v.low);
        w.WriteInt64(v.high);
    }
    static void Deserialize(MemoryPackReader& r, Int128& v) {
        v.low = r.ReadUInt64();
        v.high = r.ReadInt64();
    }
};

template<>
struct MemoryPackFormatter<UInt128> {
    static void Serialize(MemoryPackWriter& w, const UInt128& v) {
        w.WriteUInt64(v.low);
        w.WriteUInt64(v.high);
    }
    static void Deserialize(MemoryPackReader& r, UInt128& v) {
        v.low = r.ReadUInt64();
        v.high = r.ReadUInt64();
    }
};

// -- System.Numerics vectors ----------------------------------------------------
// These are C# unmanaged structs, so MemoryPack copies them verbatim.

struct Vector2 { float x = 0, y = 0;                   friend bool operator==(const Vector2&, const Vector2&) = default; };
struct Vector3 { float x = 0, y = 0, z = 0;            friend bool operator==(const Vector3&, const Vector3&) = default; };
struct Vector4 { float x = 0, y = 0, z = 0, w = 0;     friend bool operator==(const Vector4&, const Vector4&) = default; };
struct Quaternion { float x = 0, y = 0, z = 0, w = 0;  friend bool operator==(const Quaternion&, const Quaternion&) = default; };

} // namespace memorypack

MEMORYPACK_UNMANAGED(memorypack::Vector2, 8)
MEMORYPACK_UNMANAGED(memorypack::Vector3, 12)
MEMORYPACK_UNMANAGED(memorypack::Vector4, 16)
MEMORYPACK_UNMANAGED(memorypack::Quaternion, 16)
