#pragma once
/// @file core.hpp
/// @brief Core writer/reader for the MemoryPack binary wire format.
///        Interoperable with C# MemoryPack (https://github.com/Cysharp/MemoryPack).
///        Requires C++23 or later.
///
/// Include <memorypack/memorypack.hpp> for the full library. This header alone
/// provides primitives, strings, objects, collections, unions and unmanaged
/// structs; <memorypack/containers.hpp> and <memorypack/dotnet.hpp> add the
/// remaining standard-library and .NET type mappings.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// -- Library version ------------------------------------------------------------
#define MEMORYPACK_VERSION_MAJOR 0
#define MEMORYPACK_VERSION_MINOR 2
#define MEMORYPACK_VERSION_PATCH 0
#define MEMORYPACK_VERSION_STRING "0.2.0"

// -- Exception configuration ----------------------------------------------------
// Define MEMORYPACK_NO_EXCEPTIONS to build without C++ exceptions (Unreal Engine,
// console toolchains). Errors are then reported through the reader/writer error
// state and the Try* / std::expected APIs instead of being thrown.
#if !defined(MEMORYPACK_NO_EXCEPTIONS)
#  if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#    define MEMORYPACK_HAS_EXCEPTIONS 1
#  else
#    define MEMORYPACK_HAS_EXCEPTIONS 0
#  endif
#else
#  define MEMORYPACK_HAS_EXCEPTIONS 0
#endif

#if MEMORYPACK_HAS_EXCEPTIONS
#  define MEMORYPACK_TRY try
#  define MEMORYPACK_CATCH_ALL catch (...)
#else
#  define MEMORYPACK_TRY if (true)
#  define MEMORYPACK_CATCH_ALL if (false)
#endif

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#  include <expected>
#  define MEMORYPACK_HAS_EXPECTED 1
#else
#  define MEMORYPACK_HAS_EXPECTED 0
#endif

namespace memorypack {

#if defined(_MSC_VER)
#  pragma warning(push)
// C4702 "unreachable code": with exceptions enabled the optimizer proves that
// Fail() always throws, so the `return` that follows every Fail() call looks
// dead. That return is not dead - it is the path the MEMORYPACK_NO_EXCEPTIONS
// build takes. Suppressing it here keeps consumers from having to pass
// /wd4702 just to compile our header at /W4 /WX in a release build.
#  pragma warning(disable : 4702)
#endif

// -- Wire Format Constants ------------------------------------------------------
/// Object header value meaning "this object is null".
constexpr uint8_t NULL_OBJECT = 255;
/// Union header value meaning "a uint16 tag follows".
constexpr uint8_t WIDE_TAG = 250;
/// Highest member count an object header may carry (250..254 are reserved).
constexpr uint8_t MAX_MEMBER_COUNT = 249;
/// Collection header value meaning "this collection is null".
constexpr int32_t NULL_COLLECTION = -1;

// VersionTolerant per-member length markers, derived from real MemoryPack output.
constexpr uint8_t VARINT_MAX_SINGLE = 127;
constexpr uint8_t VARINT_UINT16_TAG = 0x84;
constexpr uint8_t VARINT_UINT32_TAG = 0x82;

// -- Errors ---------------------------------------------------------------------

enum class MemoryPackError : uint8_t {
    None = 0,
    BufferUnderflow,   ///< Reader ran past the end of the input.
    BufferOverflow,    ///< Writer ran past the end of a fixed-size buffer.
    InvalidHeader,     ///< Reserved/illegal object or union header value.
    LengthLimit,       ///< A declared length is impossible or exceeds the configured limit.
    InvalidString,     ///< Malformed UTF-8/UTF-16 payload.
    TrailingBytes,     ///< Input had bytes left over when full consumption was required.
    NotSupported,      ///< Operation is unavailable in this configuration.
};

[[nodiscard]] constexpr const char* ToString(MemoryPackError e) noexcept {
    switch (e) {
        case MemoryPackError::None:            return "none";
        case MemoryPackError::BufferUnderflow: return "buffer underflow";
        case MemoryPackError::BufferOverflow:  return "buffer overflow";
        case MemoryPackError::InvalidHeader:   return "invalid header";
        case MemoryPackError::LengthLimit:     return "length limit exceeded";
        case MemoryPackError::InvalidString:   return "invalid string payload";
        case MemoryPackError::TrailingBytes:   return "trailing bytes";
        case MemoryPackError::NotSupported:    return "not supported";
    }
    return "unknown";
}

#if MEMORYPACK_HAS_EXCEPTIONS
/// Thrown by the reader/writer when exceptions are enabled.
class MemoryPackException : public std::runtime_error {
public:
    MemoryPackException(MemoryPackError code, size_t offset, const char* what)
        : std::runtime_error(BuildMessage(code, offset, what)), code_(code), offset_(offset) {}

    [[nodiscard]] MemoryPackError code() const noexcept { return code_; }
    [[nodiscard]] size_t offset() const noexcept { return offset_; }

private:
    static std::string BuildMessage(MemoryPackError code, size_t offset, const char* what) {
        std::string msg = "memorypack: ";
        msg += ToString(code);
        if (what && *what) { msg += " ("; msg += what; msg += ')'; }
        msg += " at offset ";
        msg += std::to_string(offset);
        return msg;
    }

    MemoryPackError code_;
    size_t offset_;
};
#endif

namespace detail {

template<typename>
inline constexpr bool always_false = false;

// -- Byte swapping --------------------------------------------------------------
// std::byteswap is C++23 but not yet in every standard library we support.
template<typename T>
requires std::is_integral_v<T>
[[nodiscard]] constexpr T byteswap(T value) noexcept {
#if defined(__cpp_lib_byteswap) && __cpp_lib_byteswap >= 202110L
    return std::byteswap(value);
#else
    using U = std::make_unsigned_t<T>;
    auto raw = static_cast<U>(value);
    U out = 0;
    for (size_t i = 0; i < sizeof(U); ++i) {
        out = static_cast<U>((out << 8) | (raw & 0xFFu));
        raw = static_cast<U>(raw >> 8);
    }
    return static_cast<T>(out);
#endif
}

/// Converts a value between native and little-endian representation.
/// Accepts integral and floating-point types (enums are converted by the caller).
template<typename T>
[[nodiscard]] inline T endian_convert(T value) noexcept {
    if constexpr (std::endian::native == std::endian::little || sizeof(T) == 1) {
        return value;
    } else if constexpr (std::is_integral_v<T>) {
        return static_cast<T>(byteswap(static_cast<std::make_unsigned_t<T>>(value)));
    } else {
        // float / double
        using U = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
        U raw;
        std::memcpy(&raw, &value, sizeof(T));
        raw = byteswap(raw);
        T result;
        std::memcpy(&result, &raw, sizeof(T));
        return result;
    }
}

/// Counts UTF-16 code units in a well-formed UTF-8 byte sequence.
/// ASCII/BMP code points count as 1, supplementary (4-byte UTF-8) as 2.
[[nodiscard]] inline int32_t utf16_length_from_utf8(const char* data, size_t len) noexcept {
    int32_t units = 0;
    size_t i = 0;
    // Fast path: 8 ASCII bytes at a time (the common case for game payloads).
    for (; i + 8 <= len; i += 8) {
        uint64_t block;
        std::memcpy(&block, data + i, 8);
        if (block & 0x8080808080808080ULL) break;
        units += 8;
    }
    for (; i < len; ++i) {
        auto b = static_cast<uint8_t>(data[i]);
        if ((b & 0xC0) == 0x80) continue;   // UTF-8 continuation byte
        units += (b >= 0xF0) ? 2 : 1;       // 4-byte lead -> surrogate pair
    }
    return units;
}

/// Appends a Unicode code point to a UTF-8 string.
inline void append_utf8_codepoint(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

} // namespace detail

// -- Extension points -----------------------------------------------------------

/// User extension point. Specialize this for your own types:
///
///     template<> struct memorypack::IMemoryPackable<MyType> {
///         static void Serialize(MemoryPackWriter&, const MyType*);
///         static void Deserialize(MemoryPackReader&, MyType&);
///     };
///
/// The MEMORYPACK_DEFINE macro generates such a specialization for you.
/// Deliberately declared but not defined: a missing specialization then fails at
/// compile time with a clear message instead of producing a linker error.
template<typename T>
struct IMemoryPackable;

/// Internal dispatch table used by Write<T>/Read<T>. Specialize IMemoryPackable
/// for user types; specialize this only when adding library-level type support.
template<typename T, typename Enable = void>
struct MemoryPackFormatter;

/// Marks a C++ type as the mapping of a C# unmanaged struct (a struct with no
/// reference-type fields), which MemoryPack copies verbatim with no object
/// header. Specialize it, or use the MEMORYPACK_UNMANAGED macro.
template<typename T>
struct IsUnmanaged : std::false_type {};

template<typename T>
inline constexpr bool IsUnmanagedV = IsUnmanaged<T>::value;

namespace detail {
/// Specialized by MEMORYPACK_UNMANAGED_EXACT to sum a type's member sizes at
/// compile time; deliberately has no generic definition.
template<typename T>
struct UnmanagedProbe;
} // namespace detail

/// How a null value of T is spelled on the wire. C# uses four different
/// encodings depending on the kind of type, and std::optional<T> has to follow
/// the one that matches T.
enum class NullEncoding {
    Object,         ///< reference type:  [1B 255]
    Collection,     ///< List<T>/T[]:     [4B -1]
    String,         ///< string:          [4B -1]
    NullableValue,  ///< Nullable<T>:     the Nullable<T> struct copied verbatim
};

template<typename T, typename Enable = void>
struct WireNullEncoding {
    static constexpr NullEncoding value =
        (std::is_arithmetic_v<T> || std::is_enum_v<T> || IsUnmanaged<T>::value)
            ? NullEncoding::NullableValue
            : NullEncoding::Object;
};

template<>
struct WireNullEncoding<std::string> {
    static constexpr NullEncoding value = NullEncoding::String;
};

template<>
struct WireNullEncoding<std::u16string> {
    static constexpr NullEncoding value = NullEncoding::String;
};

/// Byte layout of C# Nullable<T>: a bool followed by T at T's natural alignment,
/// with the whole struct padded to a multiple of T's alignment.
template<typename T>
struct NullableLayout {
    static constexpr size_t Align = alignof(T);
    static constexpr size_t ValueOffset = Align;
    static constexpr size_t Size = ((ValueOffset + sizeof(T) + Align - 1) / Align) * Align;
};

/// Union tag for a type participating in a C# [MemoryPackUnion] hierarchy.
/// Use the MEMORYPACK_UNION_TAG macro to specialize it.
template<typename T>
struct MemoryPackUnionTag;

namespace detail {

template<typename T, typename = void>
struct has_memorypackable : std::false_type {};

template<typename T>
struct has_memorypackable<T, std::void_t<decltype(sizeof(IMemoryPackable<T>))>> : std::true_type {};

template<typename T>
inline constexpr bool has_memorypackable_v = has_memorypackable<T>::value;

template<typename T, typename = void>
struct has_formatter : std::false_type {};

template<typename T>
struct has_formatter<T, std::void_t<decltype(sizeof(MemoryPackFormatter<T>))>> : std::true_type {};

} // namespace detail

/// True when Write<T>/Read<T> know how to handle T.
template<typename T>
concept Serializable =
    std::is_arithmetic_v<T> || std::is_enum_v<T> ||
    detail::has_memorypackable_v<T> || detail::has_formatter<T>::value;

class MemoryPackWriter;
class MemoryPackReader;

// -- Reader limits --------------------------------------------------------------

/// Caps applied to lengths read from untrusted input. The reader always bounds
/// lengths by the bytes actually remaining as well; these are additional limits
/// for servers that want to reject implausible packets early.
struct ReaderOptions {
    /// Maximum element count accepted for any single collection.
    uint32_t maxCollectionLength = 0xFFFFFFFFu;
    /// Maximum byte count accepted for any single string payload.
    uint32_t maxStringLength = 0xFFFFFFFFu;
    /// Maximum object nesting depth accepted.
    uint32_t maxDepth = 256;
};

// -- MemoryPackWriter -----------------------------------------------------------
/// Supports three buffer modes:
///   1. Default (internal vector) - grows automatically, use TakeBuffer() to move out
///   2. External std::vector<uint8_t>& - appends to a caller-owned vector
///   3. External fixed buffer (uint8_t* or std::array) - fixed capacity, fails on overflow
///
/// In mode 2 the caller-owned vector's size() stays exact after every write, so
/// existing "reserve a header, serialize, patch the length" patterns keep working.
class MemoryPackWriter {
public:
    /// Default: uses an internal growable vector.
    MemoryPackWriter() = default;

    /// External vector buffer (caller-owned, growable, appended to).
    explicit MemoryPackWriter(std::vector<uint8_t>& externalBuffer)
        : vec_(&externalBuffer) {}

    /// External fixed-size buffer (raw pointer + capacity).
    MemoryPackWriter(uint8_t* data, size_t capacity)
        : fixedBuf_(data), fixedCap_(capacity) {}

    /// External fixed-size buffer expressed as a span of bytes.
    explicit MemoryPackWriter(std::span<uint8_t> buffer)
        : fixedBuf_(buffer.data()), fixedCap_(buffer.size()) {}

    explicit MemoryPackWriter(std::span<std::byte> buffer)
        : fixedBuf_(reinterpret_cast<uint8_t*>(buffer.data())), fixedCap_(buffer.size()) {}

    /// External std::array buffer.
    template<size_t N>
    explicit MemoryPackWriter(std::array<uint8_t, N>& arr)
        : fixedBuf_(arr.data()), fixedCap_(N) {}

    MemoryPackWriter(const MemoryPackWriter&) = delete;
    MemoryPackWriter& operator=(const MemoryPackWriter&) = delete;

    MemoryPackWriter(MemoryPackWriter&& other) noexcept
        : owned_(std::move(other.owned_)),
          ownedPos_(other.ownedPos_),
          vec_(other.vec_),
          fixedBuf_(other.fixedBuf_),
          fixedCap_(other.fixedCap_),
          fixedPos_(other.fixedPos_),
          error_(other.error_) {
        other.Reset();
    }

    MemoryPackWriter& operator=(MemoryPackWriter&& other) noexcept {
        if (this != &other) {
            owned_ = std::move(other.owned_);
            ownedPos_ = other.ownedPos_;
            vec_ = other.vec_;
            fixedBuf_ = other.fixedBuf_;
            fixedCap_ = other.fixedCap_;
            fixedPos_ = other.fixedPos_;
            error_ = other.error_;
            other.Reset();
        }
        return *this;
    }

    // -- Buffer management -----------------------------------------------------

    void Reserve(size_t capacity) {
        if (vec_) vec_->reserve(capacity);
        else if (!fixedBuf_ && capacity > owned_.size()) owned_.resize(capacity);
    }

    /// Resets the write position. For an external vector this clears the vector,
    /// matching the "reuse my send buffer" pattern.
    void Clear() noexcept {
        if (vec_) vec_->clear();
        else if (fixedBuf_) fixedPos_ = 0;
        else ownedPos_ = 0;
        error_ = MemoryPackError::None;
    }

    [[nodiscard]] const uint8_t* Data() const noexcept {
        if (vec_) return vec_->data();
        if (fixedBuf_) return fixedBuf_;
        return owned_.data();
    }

    [[nodiscard]] size_t Size() const noexcept {
        if (vec_) return vec_->size();
        if (fixedBuf_) return fixedPos_;
        return ownedPos_;
    }

    [[nodiscard]] std::span<const uint8_t> GetSpan() const noexcept {
        return { Data(), Size() };
    }

    /// Returns the underlying vector (vector modes only).
    [[nodiscard]] const std::vector<uint8_t>& GetBuffer() const {
        if (vec_) return *vec_;
        if (fixedBuf_) {
            const_cast<MemoryPackWriter*>(this)->Fail(
                MemoryPackError::NotSupported, "GetBuffer() is unavailable for a fixed-size buffer");
            return EmptyBuffer();
        }
        owned_.resize(ownedPos_);
        return owned_;
    }

    /// Moves the internal buffer out (default-constructed writers only).
    [[nodiscard]] std::vector<uint8_t> TakeBuffer() {
        if (vec_ || fixedBuf_) {
            Fail(MemoryPackError::NotSupported, "TakeBuffer() is only available for the owned buffer");
            return {};
        }
        owned_.resize(ownedPos_);
        ownedPos_ = 0;
        return std::move(owned_);
    }

    /// Remaining capacity for fixed-size buffers (SIZE_MAX for growable modes).
    [[nodiscard]] size_t RemainingCapacity() const noexcept {
        if (fixedBuf_) return fixedCap_ - fixedPos_;
        return static_cast<size_t>(-1);
    }

    // -- Error state -----------------------------------------------------------

    [[nodiscard]] MemoryPackError Error() const noexcept { return error_; }
    [[nodiscard]] bool Failed() const noexcept { return error_ != MemoryPackError::None; }

    // -- Object header ---------------------------------------------------------

    /// Writes an object header. memberCount must be <= MAX_MEMBER_COUNT (249);
    /// 250..255 are reserved by the wire format for unions and null.
    void WriteObjectHeader(uint8_t memberCount) {
        if (memberCount > MAX_MEMBER_COUNT) {
            Fail(MemoryPackError::InvalidHeader, "member count above 249 collides with reserved codes");
            return;
        }
        AppendByte(memberCount);
    }
    void WriteNullObjectHeader() { AppendByte(NULL_OBJECT); }

    // -- Collection header -----------------------------------------------------

    void WriteCollectionHeader(int32_t length) { WriteRaw(length); }
    void WriteNullCollectionHeader() { WriteRaw(NULL_COLLECTION); }

    // -- Union header ----------------------------------------------------------

    /// Writes a union tag: `[1B tag]` for tag < 250, otherwise `[1B 250][2B tag]`.
    void WriteUnionHeader(uint16_t tag) {
        if (tag < WIDE_TAG) {
            AppendByte(static_cast<uint8_t>(tag));
        } else {
            AppendByte(WIDE_TAG);
            WriteRaw(tag);
        }
    }
    void WriteNullUnionHeader() { AppendByte(NULL_OBJECT); }

    // -- Primitives (little-endian, fixed size, no VarInt) ---------------------

    void WriteBool(bool v)       { AppendByte(v ? 1 : 0); }
    void WriteInt8(int8_t v)     { AppendByte(static_cast<uint8_t>(v)); }
    void WriteUInt8(uint8_t v)   { AppendByte(v); }
    void WriteInt16(int16_t v)   { WriteRaw(v); }
    void WriteUInt16(uint16_t v) { WriteRaw(v); }
    void WriteInt32(int32_t v)   { WriteRaw(v); }
    void WriteUInt32(uint32_t v) { WriteRaw(v); }
    void WriteInt64(int64_t v)   { WriteRaw(v); }
    void WriteUInt64(uint64_t v) { WriteRaw(v); }
    void WriteFloat(float v)     { WriteRaw(v); }
    void WriteDouble(double v)   { WriteRaw(v); }
    void WriteChar16(char16_t v) { WriteRaw(static_cast<uint16_t>(v)); }

    /// Enum, serialized as its underlying integer type (matches C#).
    template<typename T>
    requires std::is_enum_v<T>
    void WriteEnum(T v) {
        WriteRaw(static_cast<std::underlying_type_t<T>>(v));
    }

    // -- VersionTolerant member lengths ----------------------------------------

    /// Writes a member byte-length using MemoryPack's length encoding:
    /// <= 127 as one byte, <= 65535 as `0x84` + uint16, otherwise `0x82` + uint32.
    void WriteVarIntLength(uint32_t value) {
        if (value <= VARINT_MAX_SINGLE) {
            AppendByte(static_cast<uint8_t>(value));
        } else if (value <= 0xFFFFu) {
            AppendByte(VARINT_UINT16_TAG);
            WriteRaw(static_cast<uint16_t>(value));
        } else {
            AppendByte(VARINT_UINT32_TAG);
            WriteRaw(value);
        }
    }

    // -- Strings ---------------------------------------------------------------

    /// Writes a UTF-8 string in MemoryPack's wire format:
    ///   empty -> int32 0
    ///   else  -> int32 (~utf8ByteCount), int32 (utf16Length), utf8 bytes
    void WriteString(std::string_view s) {
        if (s.empty()) { WriteRaw(static_cast<int32_t>(0)); return; }
        auto byteCount = ToInt32Length(s.size());
        if (byteCount < 0) return;
        WriteRaw(static_cast<int32_t>(~byteCount));
        WriteRaw(detail::utf16_length_from_utf8(s.data(), s.size()));
        AppendBytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    void WriteNullString() { WriteRaw(static_cast<int32_t>(-1)); }

    void WriteOptionalString(const std::optional<std::string>& s) {
        if (s) WriteString(*s);
        else   WriteNullString();
    }

    /// Writes a string using MemoryPack's UTF-16 form: `[int32 utf16Length][UTF-16LE units]`.
    /// The C# reader accepts both forms; UTF-8 (WriteString) is the default.
    void WriteStringUtf16(std::u16string_view s) {
        if (s.empty()) { WriteRaw(static_cast<int32_t>(0)); return; }
        auto units = ToInt32Length(s.size());
        if (units < 0) return;
        WriteRaw(units);
        for (char16_t c : s) WriteRaw(static_cast<uint16_t>(c));
    }

    // -- Unmanaged structs -----------------------------------------------------

    /// Copies a trivially-copyable struct verbatim, matching how MemoryPack
    /// serializes a C# unmanaged struct (no object header, padding included).
    ///
    /// WARNING: the struct's padding bytes go on the wire as-is. After plain
    /// default-initialization (`T v;`) those bytes are indeterminate, so the
    /// output is not reproducible and may expose whatever was on the stack.
    /// Always value-initialize (`T v{};`), which zero-initializes the whole
    /// object including padding, before filling in the members. See
    /// docs/security.md#unmanaged-struct-padding.
    template<typename T>
    void WriteUnmanaged(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "WriteUnmanaged requires a trivially copyable type");
        static_assert(std::endian::native == std::endian::little,
                      "Unmanaged struct copying is only byte-compatible on little-endian hosts; "
                      "serialize the fields individually instead");
        AppendBytes(reinterpret_cast<const uint8_t*>(std::addressof(value)), sizeof(T));
    }

    // -- Raw bytes -------------------------------------------------------------

    void WriteBytes(std::span<const uint8_t> data) { AppendBytes(data.data(), data.size()); }

    // -- Collections -----------------------------------------------------------

    /// Vector of arithmetic types (bulk copy on little-endian hosts).
    template<typename T>
    requires std::is_arithmetic_v<T>
    void WriteVector(const std::vector<T>& vec) {
        auto n = ToInt32Length(vec.size());
        if (n < 0) return;
        WriteCollectionHeader(n);
        WriteContiguous(vec.data(), vec.size());
    }

    /// std::vector<bool> is a bit-packed specialisation with no data(); each
    /// element is still one wire byte, matching C# List<bool>.
    void WriteVector(const std::vector<bool>& vec) {
        auto n = ToInt32Length(vec.size());
        if (n < 0) return;
        WriteCollectionHeader(n);
        for (bool b : vec) AppendByte(b ? 1 : 0);
    }

    /// Vector of strings.
    void WriteStringVector(const std::vector<std::string>& vec) {
        auto n = ToInt32Length(vec.size());
        if (n < 0) return;
        WriteCollectionHeader(n);
        for (const auto& s : vec) WriteString(s);
    }

    /// C-style fixed array: writes only `count` elements as a collection.
    template<typename T>
    requires std::is_arithmetic_v<T>
    void WriteArray(const T* arr, int32_t count) {
        WriteCollectionHeader(count);
        if (count <= 0) return;
        WriteContiguous(arr, static_cast<size_t>(count));
    }

    /// std::array<T, N> written as a collection of N elements.
    template<typename T, size_t N>
    requires std::is_arithmetic_v<T>
    void WriteArray(const std::array<T, N>& arr) {
        auto n = ToInt32Length(N);
        if (n < 0) return;
        WriteCollectionHeader(n);
        WriteContiguous(arr.data(), N);
    }

    /// Collection of any serializable element type (objects, strings, nested
    /// collections). Mirrors C# List<T>/T[].
    template<typename T>
    void WriteCollection(std::span<const T> items) {
        auto n = ToInt32Length(items.size());
        if (n < 0) return;
        WriteCollectionHeader(n);
        if constexpr (std::is_arithmetic_v<T>) {
            WriteContiguous(items.data(), items.size());
        } else {
            for (const auto& item : items) Write(item);
        }
    }

    template<typename T, typename Alloc>
    void WriteCollection(const std::vector<T, Alloc>& items) {
        auto n = ToInt32Length(items.size());
        if (n < 0) return;
        WriteCollectionHeader(n);
        if constexpr (std::is_same_v<T, bool>) {
            for (bool b : items) AppendByte(b ? 1 : 0);
        } else if constexpr (std::is_arithmetic_v<T>) {
            WriteContiguous(items.data(), items.size());
        } else {
            for (const auto& item : items) Write(item);
        }
    }

    /// Bulk-copies a contiguous range of unmanaged structs, matching C#
    /// List<UnmanagedStruct>: `[int32 count][count * sizeof(T) bytes]`.
    template<typename T>
    void WriteUnmanagedCollection(std::span<const T> items) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "WriteUnmanagedCollection requires a trivially copyable element type");
        static_assert(std::endian::native == std::endian::little,
                      "Unmanaged struct copying is only byte-compatible on little-endian hosts");
        auto n = ToInt32Length(items.size());
        if (n < 0) return;
        WriteCollectionHeader(n);
        if (!items.empty())
            AppendBytes(reinterpret_cast<const uint8_t*>(items.data()), items.size() * sizeof(T));
    }

    // -- Maps ------------------------------------------------------------------

    template<typename K, typename V, typename... Rest>
    void WriteMap(const std::map<K, V, Rest...>& map) { WriteMapImpl(map); }

    template<typename K, typename V, typename... Rest>
    void WriteMap(const std::unordered_map<K, V, Rest...>& map) { WriteMapImpl(map); }

    // -- Tuples ----------------------------------------------------------------

    /// Writes a tuple as a C# Tuple<...>: an object header followed by the items.
    /// A C# unmanaged ValueTuple is copied verbatim instead - use WriteUnmanaged.
    template<typename... Ts>
    void WriteTuple(const std::tuple<Ts...>& t) {
        static_assert(sizeof...(Ts) <= MAX_MEMBER_COUNT,
                      "a tuple may have at most 249 elements on the wire");
        WriteObjectHeader(static_cast<uint8_t>(sizeof...(Ts)));
        std::apply([this](const auto&... args) { (Write(args), ...); }, t);
    }

    // -- Optionals / pointers --------------------------------------------------

    /// Writes a nullable object: the value, or a null object header.
    template<typename T>
    void WriteOptional(const std::optional<T>& v) {
        if (v) Write(*v);
        else   WriteNullObjectHeader();
    }

    template<typename T>
    void WritePointer(const T* v) {
        if (v) Write(*v);
        else   WriteNullObjectHeader();
    }

    /// Writes C# Nullable<T> for a managed T: `[1B 1][value]`, or `[1B 255]`.
    template<typename T>
    void WriteNullableObject(const std::optional<T>& v) {
        if (!v) { WriteNullObjectHeader(); return; }
        AppendByte(1);
        Write(*v);
    }

    /// Writes C# Nullable<T> for an unmanaged T. MemoryPack copies the whole
    /// Nullable<T> struct verbatim: `[1B hasValue][padding][T]`.
    template<typename T>
    void WriteNullable(const std::optional<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "WriteNullable maps C# Nullable<T> for unmanaged T only");
        using L = NullableLayout<T>;
        uint8_t buf[L::Size] = {};
        buf[0] = v ? uint8_t{1} : uint8_t{0};
        if (v) {
            if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
                auto le = detail::endian_convert(*v);
                std::memcpy(buf + L::ValueOffset, &le, sizeof(T));
            } else {
                static_assert(std::endian::native == std::endian::little,
                              "Nullable<struct> copying is only byte-compatible on little-endian hosts");
                std::memcpy(buf + L::ValueOffset, std::addressof(*v), sizeof(T));
            }
        }
        AppendBytes(buf, L::Size);
    }

    // -- Generic dispatch ------------------------------------------------------

    /// Serializes any supported type. This is the recommended entry point;
    /// the typed Write* methods remain available for explicit control.
    template<typename T>
    void Write(const T& value);

    /// Converts a container size to the int32 length the wire format uses.
    /// Returns -1 and records MemoryPackError::LengthLimit when it does not fit,
    /// so a caller can bail out rather than emitting a wrong header.
    int32_t CheckedLength(size_t size) { return ToInt32Length(size); }

    // -- Low-level append ------------------------------------------------------

    void AppendByte(uint8_t b) {
        if (vec_) { vec_->push_back(b); return; }
        if (fixedBuf_) {
            if (fixedPos_ >= fixedCap_) [[unlikely]] {
                Fail(MemoryPackError::BufferOverflow, "fixed buffer is full");
                return;
            }
            fixedBuf_[fixedPos_++] = b;
            return;
        }
        if (ownedPos_ >= owned_.size()) [[unlikely]] GrowOwned(1);
        owned_[ownedPos_++] = b;
    }

    void AppendBytes(const uint8_t* src, size_t n) {
        if (n == 0) return;
        if (vec_) { vec_->insert(vec_->end(), src, src + n); return; }
        if (fixedBuf_) {
            if (n > fixedCap_ - fixedPos_) [[unlikely]] {
                Fail(MemoryPackError::BufferOverflow, "fixed buffer is full");
                return;
            }
            std::memcpy(fixedBuf_ + fixedPos_, src, n);
            fixedPos_ += n;
            return;
        }
        if (n > owned_.size() - ownedPos_) [[unlikely]] GrowOwned(n);
        std::memcpy(owned_.data() + ownedPos_, src, n);
        ownedPos_ += n;
    }

private:
    friend class MemoryPackReader;

    static const std::vector<uint8_t>& EmptyBuffer() {
        static const std::vector<uint8_t> empty;
        return empty;
    }

    void Reset() noexcept {
        owned_.clear();
        ownedPos_ = 0;
        vec_ = nullptr;
        fixedBuf_ = nullptr;
        fixedCap_ = 0;
        fixedPos_ = 0;
        error_ = MemoryPackError::None;
    }

    void GrowOwned(size_t needed) {
        size_t target = ownedPos_ + needed;
        size_t next = owned_.size() ? owned_.size() * 2 : 64;
        while (next < target) next *= 2;
        owned_.resize(next);
    }

    /// Converts a container size to the int32 length the wire format uses.
    /// Returns -1 and records an error when the size cannot be represented.
    int32_t ToInt32Length(size_t size) {
        if (size > 0x7FFFFFFFu) [[unlikely]] {
            Fail(MemoryPackError::LengthLimit, "collection or string exceeds 2^31-1 elements");
            return -1;
        }
        return static_cast<int32_t>(size);
    }

    template<typename T>
    void WriteContiguous(const T* data, size_t count) {
        if (count == 0) return;
        if constexpr (std::endian::native == std::endian::little) {
            AppendBytes(reinterpret_cast<const uint8_t*>(data), count * sizeof(T));
        } else {
            for (size_t i = 0; i < count; ++i) WriteRaw(data[i]);
        }
    }

    template<typename Map>
    void WriteMapImpl(const Map& map) {
        auto n = ToInt32Length(map.size());
        if (n < 0) return;
        WriteCollectionHeader(n);
        for (const auto& [key, value] : map) {
            Write(key);
            Write(value);
        }
    }

    template<typename T>
    void WriteRaw(T value) {
        value = detail::endian_convert(value);
        AppendBytes(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
    }

    void Fail(MemoryPackError code, const char* what) {
        if (error_ == MemoryPackError::None) error_ = code;
#if MEMORYPACK_HAS_EXCEPTIONS
        throw MemoryPackException(code, Size(), what);
#else
        (void)what;
#endif
    }

    // Mutable so the const accessors can trim it to the write position; the
    // observable value of the writer does not change.
    mutable std::vector<uint8_t> owned_;
    size_t ownedPos_ = 0;
    std::vector<uint8_t>* vec_ = nullptr;
    uint8_t* fixedBuf_ = nullptr;
    size_t fixedCap_ = 0;
    size_t fixedPos_ = 0;
    MemoryPackError error_ = MemoryPackError::None;
};

// -- Object header result -------------------------------------------------------

/// Result of MemoryPackReader::ReadObjectHeader(). Structured bindings keep
/// working: `auto [count, isNull] = reader.ReadObjectHeader();`
struct ObjectHeader {
    uint8_t count = 0;
    bool isNull = false;
};

// -- MemoryPackReader -----------------------------------------------------------

class MemoryPackReader {
public:
    MemoryPackReader(const uint8_t* data, size_t size)
        : data_(data), size_(size) {}

    explicit MemoryPackReader(std::span<const uint8_t> buf)
        : data_(buf.data()), size_(buf.size()) {}

    explicit MemoryPackReader(std::span<const std::byte> buf)
        : data_(reinterpret_cast<const uint8_t*>(buf.data())), size_(buf.size()) {}

    MemoryPackReader(const uint8_t* data, size_t size, const ReaderOptions& options)
        : data_(data), size_(size), options_(options) {}

    MemoryPackReader(std::span<const uint8_t> buf, const ReaderOptions& options)
        : data_(buf.data()), size_(buf.size()), options_(options) {}

    // -- Options / error state -------------------------------------------------

    [[nodiscard]] const ReaderOptions& Options() const noexcept { return options_; }
    void SetOptions(const ReaderOptions& options) noexcept { options_ = options; }

    [[nodiscard]] MemoryPackError Error() const noexcept { return error_; }
    [[nodiscard]] bool Failed() const noexcept { return error_ != MemoryPackError::None; }

    // -- Position --------------------------------------------------------------

    void Advance(size_t n) { if (EnsureBytes(n)) pos_ += n; }
    [[nodiscard]] bool   IsEnd()     const noexcept { return pos_ >= size_; }
    [[nodiscard]] size_t Position()  const noexcept { return pos_; }
    [[nodiscard]] size_t Remaining() const noexcept { return size_ - pos_; }

    void Seek(size_t position) {
        if (position > size_) { Fail(MemoryPackError::BufferUnderflow, "seek past end"); return; }
        pos_ = position;
    }

    void Reset() noexcept {
        pos_ = 0;
        depth_ = 0;
        error_ = MemoryPackError::None;
    }

    /// A reader limited to the next `length` bytes, for bounded nested payloads.
    [[nodiscard]] MemoryPackReader SubReader(size_t length) {
        if (!EnsureBytes(length)) return MemoryPackReader(data_ + pos_, 0, options_);
        MemoryPackReader sub(data_ + pos_, length, options_);
        pos_ += length;
        return sub;
    }

    // -- Object header ---------------------------------------------------------

    ObjectHeader ReadObjectHeader() {
        uint8_t b = ReadByte();
        if (b == NULL_OBJECT) return { 0, true };
        if (b > MAX_MEMBER_COUNT) [[unlikely]] {
            Fail(MemoryPackError::InvalidHeader, "reserved object header value 250..254");
            return { 0, true };
        }
        return { b, false };
    }

    [[nodiscard]] bool PeekIsNull() const noexcept {
        if (pos_ >= size_) return false;
        return data_[pos_] == NULL_OBJECT;
    }

    // -- Collection header -----------------------------------------------------

    /// Raw collection length; -1 means null. Prefer ReadCollectionLength(), which
    /// also rejects lengths that cannot possibly fit in the remaining input.
    int32_t ReadCollectionHeader() { return ReadRaw<int32_t>(); }

    /// Reads a collection length and validates it against the bytes actually
    /// remaining and the configured limits. Returns -1 for a null collection.
    int32_t ReadCollectionLength(size_t minElementSize) {
        int32_t len = ReadRaw<int32_t>();
        if (len < 0) {
            if (len != NULL_COLLECTION) [[unlikely]] {
                Fail(MemoryPackError::LengthLimit, "negative collection length");
                return -1;
            }
            return -1;
        }
        auto n = static_cast<uint32_t>(len);
        if (n > options_.maxCollectionLength) [[unlikely]] {
            Fail(MemoryPackError::LengthLimit, "collection length above the configured limit");
            return -1;
        }
        // A length is only plausible if its minimum encoded size fits what is left.
        if (minElementSize > 0 && n > Remaining() / minElementSize) [[unlikely]] {
            Fail(MemoryPackError::LengthLimit, "collection length exceeds the remaining input");
            return -1;
        }
        return len;
    }

    // -- Union header ----------------------------------------------------------

    /// Reads a union tag. Returns nullopt when the union value is null.
    std::optional<uint16_t> ReadUnionHeader() {
        uint8_t b = ReadByte();
        if (b == NULL_OBJECT) return std::nullopt;
        if (b == WIDE_TAG) return ReadRaw<uint16_t>();
        if (b > WIDE_TAG) [[unlikely]] {
            Fail(MemoryPackError::InvalidHeader, "reserved union header value 251..254");
            return std::nullopt;
        }
        return static_cast<uint16_t>(b);
    }

    // -- Primitives ------------------------------------------------------------

    bool     ReadBool()   { return ReadByte() != 0; }
    int8_t   ReadInt8()   { return static_cast<int8_t>(ReadByte()); }
    uint8_t  ReadUInt8()  { return ReadByte(); }
    int16_t  ReadInt16()  { return ReadRaw<int16_t>(); }
    uint16_t ReadUInt16() { return ReadRaw<uint16_t>(); }
    int32_t  ReadInt32()  { return ReadRaw<int32_t>(); }
    uint32_t ReadUInt32() { return ReadRaw<uint32_t>(); }
    int64_t  ReadInt64()  { return ReadRaw<int64_t>(); }
    uint64_t ReadUInt64() { return ReadRaw<uint64_t>(); }
    float    ReadFloat()  { return ReadRaw<float>(); }
    double   ReadDouble() { return ReadRaw<double>(); }
    char16_t ReadChar16() { return static_cast<char16_t>(ReadRaw<uint16_t>()); }

    template<typename T>
    requires std::is_enum_v<T>
    T ReadEnum() { return static_cast<T>(ReadRaw<std::underlying_type_t<T>>()); }

    // -- VersionTolerant member lengths ----------------------------------------

    /// Reads a member byte-length written by WriteVarIntLength.
    uint32_t ReadVarIntLength() {
        uint8_t b = ReadByte();
        if (b <= VARINT_MAX_SINGLE) return b;
        if (b == VARINT_UINT16_TAG) return ReadRaw<uint16_t>();
        if (b == VARINT_UINT32_TAG) return ReadRaw<uint32_t>();
        Fail(MemoryPackError::InvalidHeader, "unsupported member length marker");
        return 0;
    }

    // -- Strings ---------------------------------------------------------------

    /// Reads a string. Returns nullopt for a null string.
    ///   header == -1 -> null, == 0 -> empty, > 0 -> UTF-16 code-unit count,
    ///   <= -2 -> UTF-8 (~header = byte count, followed by an int32 utf16Length).
    std::optional<std::string> ReadString() {
        int32_t header = ReadRaw<int32_t>();
        if (Failed()) return std::string{};
        if (header == -1) return std::nullopt;
        if (header == 0)  return std::string{};
        if (header > 0)   return ReadUtf16String(header);

        auto byteCount = static_cast<uint32_t>(~header);
        ReadRaw<int32_t>();   // utf16Length, not needed to decode UTF-8
        if (!CheckStringLength(byteCount, 1)) return std::string{};
        if (!EnsureBytes(byteCount)) return std::string{};
        std::string result(reinterpret_cast<const char*>(data_ + pos_), byteCount);
        pos_ += byteCount;
        return result;
    }

    /// In-place variant that reuses the caller's buffer. Returns false for null.
    bool ReadString(std::string& out) {
        auto s = ReadString();
        if (!s) { out.clear(); return false; }
        out = std::move(*s);
        return true;
    }

    /// Zero-copy view over a UTF-8 payload. Returns nullopt for a null string or
    /// for a UTF-16 payload (which must be transcoded - use ReadString there).
    /// The view borrows the reader's input buffer and is invalidated with it.
    std::optional<std::string_view> ReadStringView() {
        size_t start = pos_;
        int32_t header = ReadRaw<int32_t>();
        if (Failed()) return std::nullopt;
        if (header == -1) return std::nullopt;
        if (header == 0)  return std::string_view{};
        if (header > 0) { pos_ = start; return std::nullopt; }   // UTF-16: rewind

        auto byteCount = static_cast<uint32_t>(~header);
        ReadRaw<int32_t>();
        if (!CheckStringLength(byteCount, 1)) return std::nullopt;
        if (!EnsureBytes(byteCount)) return std::nullopt;
        std::string_view view(reinterpret_cast<const char*>(data_ + pos_), byteCount);
        pos_ += byteCount;
        return view;
    }

    // -- Unmanaged structs -----------------------------------------------------

    template<typename T>
    T ReadUnmanaged() {
        static_assert(std::is_trivially_copyable_v<T>,
                      "ReadUnmanaged requires a trivially copyable type");
        static_assert(std::endian::native == std::endian::little,
                      "Unmanaged struct copying is only byte-compatible on little-endian hosts");
        T value{};
        if (!EnsureBytes(sizeof(T))) return value;
        std::memcpy(std::addressof(value), data_ + pos_, sizeof(T));
        pos_ += sizeof(T);
        return value;
    }

    template<typename T>
    void ReadUnmanaged(T& out) { out = ReadUnmanaged<T>(); }

    // -- Collections -----------------------------------------------------------

    /// Vector of arithmetic types (bulk copy on little-endian hosts).
    template<typename T>
    requires std::is_arithmetic_v<T>
    std::vector<T> ReadVector() {
        std::vector<T> result;
        ReadVector(result);
        return result;
    }

    /// In-place variant that reuses the caller's vector.
    template<typename T>
    requires std::is_arithmetic_v<T>
    void ReadVector(std::vector<T>& out) {
        out.clear();
        int32_t len = ReadCollectionLength(sizeof(T));
        if (len <= 0) return;
        auto count = static_cast<size_t>(len);
        if (!EnsureBytes(count * sizeof(T))) return;
        if constexpr (std::endian::native == std::endian::little) {
            const uint8_t* p = data_ + pos_;
            // `pos_` is the sum of every preceding field's size and is not
            // generally a multiple of alignof(T) (e.g. a 1-byte field followed
            // by an int32 vector) - found by fuzzing, which is exactly the kind
            // of misaligned offset a real packet layout can produce. Casting p
            // to `const T*` and dereferencing it through assign() would be a
            // misaligned access (UB, and a real fault on strict-alignment
            // targets) whenever it lands on a non-aligned offset, so only take
            // that path when p actually satisfies alignof(T); otherwise fall
            // back to a memcpy, which places no alignment requirement on
            // either side.
            if (reinterpret_cast<uintptr_t>(p) % alignof(T) == 0) {
                const T* src = reinterpret_cast<const T*>(p);
                out.assign(src, src + count);   // single pass, no zero-fill
            } else {
                out.resize(count);
                std::memcpy(out.data(), p, count * sizeof(T));
            }
            pos_ += count * sizeof(T);
        } else {
            out.reserve(count);
            for (size_t i = 0; i < count; ++i) out.push_back(ReadRaw<T>());
        }
    }

    /// std::vector<bool>: one wire byte per element, matching C# List<bool>.
    void ReadVector(std::vector<bool>& out) {
        out.clear();
        int32_t len = ReadCollectionLength(1);
        if (len <= 0) return;
        auto count = static_cast<size_t>(len);
        if (!EnsureBytes(count)) return;
        out.reserve(count);
        for (size_t i = 0; i < count; ++i) out.push_back(data_[pos_ + i] != 0);
        pos_ += count;
    }

    /// Vector of strings.
    std::vector<std::string> ReadStringVector() {
        std::vector<std::string> result;
        ReadStringVector(result);
        return result;
    }

    void ReadStringVector(std::vector<std::string>& out) {
        out.clear();
        int32_t len = ReadCollectionLength(sizeof(int32_t));
        if (len <= 0) return;
        out.reserve(static_cast<size_t>(len));
        for (int32_t i = 0; i < len && !Failed(); ++i)
            out.push_back(ReadString().value_or(std::string{}));
    }

    /// C-style fixed array: reads up to `maxCount` elements and skips the excess.
    /// Returns the number of elements written into `arr`.
    template<typename T>
    requires std::is_arithmetic_v<T>
    int32_t ReadArray(T* arr, int32_t maxCount) {
        int32_t len = ReadCollectionLength(sizeof(T));
        if (len <= 0) return 0;
        int32_t readCount = (len <= maxCount) ? len : maxCount;
        if (readCount > 0) {
            auto bytes = static_cast<size_t>(readCount) * sizeof(T);
            if (!EnsureBytes(bytes)) return 0;
            if constexpr (std::endian::native == std::endian::little) {
                std::memcpy(arr, data_ + pos_, bytes);
                pos_ += bytes;
            } else {
                for (int32_t i = 0; i < readCount; ++i) arr[i] = ReadRaw<T>();
            }
        }
        int32_t skipCount = len - readCount;
        if (skipCount > 0) {
            auto skipBytes = static_cast<size_t>(skipCount) * sizeof(T);
            if (!EnsureBytes(skipBytes)) return readCount;
            pos_ += skipBytes;
        }
        return readCount;
    }

    /// std::array<T, N>: reads a collection into a fixed-size array, skipping excess.
    template<typename T, size_t N>
    requires std::is_arithmetic_v<T>
    std::array<T, N> ReadArray() {
        std::array<T, N> result{};
        ReadArray(result.data(), static_cast<int32_t>(N));
        return result;
    }

    /// Collection of any serializable element type. Mirrors C# List<T>/T[].
    template<typename T>
    std::vector<T> ReadCollection() {
        std::vector<T> result;
        ReadCollection(result);
        return result;
    }

    template<typename T>
    void ReadCollection(std::vector<T>& out) {
        if constexpr (std::is_arithmetic_v<T>) {
            ReadVector(out);
        } else {
            out.clear();
            int32_t len = ReadCollectionLength(MinEncodedSize<T>());
            if (len <= 0) return;
            out.reserve(static_cast<size_t>(len));
            for (int32_t i = 0; i < len && !Failed(); ++i) {
                T value{};
                Read(value);
                out.push_back(std::move(value));
            }
        }
    }

    /// Bulk-reads a collection of unmanaged structs written by
    /// MemoryPackWriter::WriteUnmanagedCollection.
    template<typename T>
    void ReadUnmanagedCollection(std::vector<T>& out) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "ReadUnmanagedCollection requires a trivially copyable element type");
        static_assert(std::endian::native == std::endian::little,
                      "Unmanaged struct copying is only byte-compatible on little-endian hosts");
        out.clear();
        int32_t len = ReadCollectionLength(sizeof(T));
        if (len <= 0) return;
        auto count = static_cast<size_t>(len);
        if (!EnsureBytes(count * sizeof(T))) return;
        out.resize(count);
        std::memcpy(out.data(), data_ + pos_, count * sizeof(T));
        pos_ += count * sizeof(T);
    }

    template<typename T>
    std::vector<T> ReadUnmanagedCollection() {
        std::vector<T> result;
        ReadUnmanagedCollection(result);
        return result;
    }

    // -- Maps ------------------------------------------------------------------

    template<typename K, typename V>
    std::map<K, V> ReadMap() {
        std::map<K, V> result;
        int32_t len = ReadCollectionLength(MinEncodedSize<K>() + MinEncodedSize<V>());
        for (int32_t i = 0; i < len && !Failed(); ++i) {
            K key{};   Read(key);
            V value{}; Read(value);
            result.emplace(std::move(key), std::move(value));
        }
        return result;
    }

    template<typename K, typename V>
    std::unordered_map<K, V> ReadUnorderedMap() {
        std::unordered_map<K, V> result;
        int32_t len = ReadCollectionLength(MinEncodedSize<K>() + MinEncodedSize<V>());
        if (len <= 0) return result;
        result.reserve(static_cast<size_t>(len));   // length already validated
        for (int32_t i = 0; i < len && !Failed(); ++i) {
            K key{};   Read(key);
            V value{}; Read(value);
            result.emplace(std::move(key), std::move(value));
        }
        return result;
    }

    // -- Tuples ----------------------------------------------------------------

    template<typename... Ts>
    std::tuple<Ts...> ReadTuple() {
        auto header = ReadObjectHeader();
        if (header.isNull) return {};
        return ReadTupleImpl<Ts...>(header.count, std::index_sequence_for<Ts...>{});
    }

    // -- Optionals -------------------------------------------------------------

    /// Reads a nullable object written by WriteOptional.
    template<typename T>
    std::optional<T> ReadOptional() {
        if (PeekIsNull()) { ReadByte(); return std::nullopt; }
        T value{};
        Read(value);
        return value;
    }

    /// Reads C# Nullable<T> for a managed T, written by WriteNullableObject.
    template<typename T>
    std::optional<T> ReadNullableObject() {
        uint8_t b = ReadByte();
        if (b == NULL_OBJECT) return std::nullopt;
        T value{};
        Read(value);
        return value;
    }

    /// Reads C# Nullable<T> for an unmanaged T, written by WriteNullable.
    template<typename T>
    std::optional<T> ReadNullable() {
        static_assert(std::is_trivially_copyable_v<T>,
                      "ReadNullable maps C# Nullable<T> for unmanaged T only");
        using L = NullableLayout<T>;
        if (!EnsureBytes(L::Size)) return std::nullopt;
        const uint8_t* src = data_ + pos_;
        pos_ += L::Size;
        if (src[0] == 0) return std::nullopt;
        T value{};
        std::memcpy(std::addressof(value), src + L::ValueOffset, sizeof(T));
        if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
            value = detail::endian_convert(value);
        } else {
            static_assert(std::endian::native == std::endian::little,
                          "Nullable<struct> copying is only byte-compatible on little-endian hosts");
        }
        return value;
    }

    // -- Generic dispatch ------------------------------------------------------

    /// Deserializes any supported type into `out`.
    template<typename T>
    void Read(T& out);

    /// Deserializes any supported type and returns it by value.
    template<typename T>
    T Read() {
        T value{};
        Read(value);
        return value;
    }

    // -- Depth tracking (used by generated object readers) ---------------------

    bool EnterObject() {
        if (++depth_ > options_.maxDepth) [[unlikely]] {
            Fail(MemoryPackError::LengthLimit, "object nesting deeper than the configured limit");
            return false;
        }
        return true;
    }
    void LeaveObject() noexcept { if (depth_ > 0) --depth_; }

    /// Verifies that no input is left. Used by DeserializeExact.
    bool RequireEnd() {
        if (!IsEnd()) {
            Fail(MemoryPackError::TrailingBytes, "input contains unread bytes");
            return false;
        }
        return true;
    }

    uint8_t ReadByte() {
        if (!EnsureBytes(1)) return 0;
        return data_[pos_++];
    }

    /// Bounds check. Returns false (and records an error) instead of reading past
    /// the end; with exceptions enabled it throws before returning.
    bool EnsureBytes(size_t n) {
        if (error_ != MemoryPackError::None) [[unlikely]] return false;
        // Subtraction form: never overflows, unlike `pos_ + n > size_`.
        if (n > size_ - pos_) [[unlikely]] {
            Fail(MemoryPackError::BufferUnderflow, "not enough bytes remaining");
            return false;
        }
        return true;
    }

private:
    /// Smallest number of bytes any encoding of T can occupy on the wire.
    /// Used to reject implausible collection lengths before allocating.
    template<typename T>
    static constexpr size_t MinEncodedSize() {
        if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) return sizeof(T);
        else if constexpr (std::is_same_v<T, std::string>) return sizeof(int32_t);
        else return 1;   // at minimum a null/object header byte
    }

    bool CheckStringLength(uint32_t byteCount, size_t unitSize) {
        if (byteCount > options_.maxStringLength) [[unlikely]] {
            Fail(MemoryPackError::LengthLimit, "string length above the configured limit");
            return false;
        }
        if (unitSize > 0 && byteCount > Remaining() / unitSize) [[unlikely]] {
            Fail(MemoryPackError::LengthLimit, "string length exceeds the remaining input");
            return false;
        }
        return true;
    }

    template<typename T>
    T ReadRaw() {
        if (!EnsureBytes(sizeof(T))) return T{};
        T value;
        std::memcpy(&value, data_ + pos_, sizeof(T));
        pos_ += sizeof(T);
        return detail::endian_convert(value);
    }

    /// Decodes `utf16Length` UTF-16LE code units (surrogate pairs included) to UTF-8.
    std::string ReadUtf16String(int32_t utf16Length) {
        auto units = static_cast<uint32_t>(utf16Length);
        if (!CheckStringLength(units, 2)) return {};
        if (!EnsureBytes(static_cast<size_t>(units) * 2)) return {};
        std::string out;
        out.reserve(units);
        for (uint32_t i = 0; i < units; ) {
            uint16_t hi = ReadRaw<uint16_t>();
            ++i;
            uint32_t cp;
            if (hi >= 0xD800 && hi <= 0xDBFF) {
                // High surrogate: a valid low surrogate must follow.
                if (i < units) {
                    uint16_t lo = ReadRaw<uint16_t>();
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        ++i;
                        cp = 0x10000u + ((static_cast<uint32_t>(hi - 0xD800) << 10)
                                       | static_cast<uint32_t>(lo - 0xDC00));
                    } else {
                        // Unpaired high surrogate; keep the next unit for the following round.
                        pos_ -= 2;
                        cp = 0xFFFD;
                    }
                } else {
                    cp = 0xFFFD;
                }
            } else if (hi >= 0xDC00 && hi <= 0xDFFF) {
                cp = 0xFFFD;   // lone low surrogate
            } else {
                cp = hi;
            }
            detail::append_utf8_codepoint(out, cp);
        }
        return out;
    }

    template<typename... Ts, size_t... Is>
    std::tuple<Ts...> ReadTupleImpl(uint8_t cnt, std::index_sequence<Is...>) {
        std::tuple<Ts...> result{};
        ((Is < cnt ? (Read(std::get<Is>(result)), 0) : 0), ...);
        return result;
    }

    void Fail(MemoryPackError code, const char* what) {
        if (error_ == MemoryPackError::None) error_ = code;
#if MEMORYPACK_HAS_EXCEPTIONS
        throw MemoryPackException(code, pos_, what);
#else
        (void)what;
#endif
    }

    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    uint32_t depth_ = 0;
    ReaderOptions options_{};
    MemoryPackError error_ = MemoryPackError::None;
};

// -- VersionTolerant objects ----------------------------------------------------
//
// C# `[MemoryPackable(GenerateType.VersionTolerant)]` prefixes the members with
// their byte lengths, so a reader can skip members it does not know:
//
//     [1B memberCount][len0][len1]...[lenN-1][member0][member1]...
//
// Each length uses MemoryPack's length encoding (see WriteVarIntLength). This is
// strictly more expensive than the default layout - use it for long-lived
// persisted data, not for hot packets.

/// Builds a VersionTolerant object. Members are buffered so their lengths can be
/// written first, then everything is flushed to the target writer by Finish().
class VersionTolerantWriter {
public:
    explicit VersionTolerantWriter(MemoryPackWriter& out) : out_(&out) {}

    VersionTolerantWriter(const VersionTolerantWriter&) = delete;
    VersionTolerantWriter& operator=(const VersionTolerantWriter&) = delete;

    template<typename T>
    void WriteMember(const T& value) {
        const size_t start = body_.Size();
        body_.Write(value);
        lengths_.push_back(static_cast<uint32_t>(body_.Size() - start));
    }

    /// Emits the header and the buffered member bytes.
    void Finish() {
        if (finished_) return;
        finished_ = true;
        if (lengths_.size() > MAX_MEMBER_COUNT) {
            // WriteObjectHeader records the error; do not truncate the count.
            out_->WriteObjectHeader(static_cast<uint8_t>(NULL_OBJECT));
            return;
        }
        out_->WriteObjectHeader(static_cast<uint8_t>(lengths_.size()));
        for (uint32_t len : lengths_) out_->WriteVarIntLength(len);
        out_->WriteBytes(body_.GetSpan());
    }

    /// Flushes on scope exit. A failure is recorded in the target writer's error
    /// state rather than propagating - throwing from a destructor would terminate.
    ~VersionTolerantWriter() {
        MEMORYPACK_TRY {
            Finish();
        } MEMORYPACK_CATCH_ALL {
        }
    }

private:
    MemoryPackWriter* out_;
    MemoryPackWriter body_;
    std::vector<uint32_t> lengths_;
    bool finished_ = false;
};

/// Reads a VersionTolerant object. Members are read in order; each one is
/// positioned exactly at its recorded length, so a member whose encoding grew on
/// the sender's side is skipped cleanly.
class VersionTolerantReader {
public:
    explicit VersionTolerantReader(MemoryPackReader& in) : in_(&in) {
        const auto header = in.ReadObjectHeader();
        if (header.isNull) { isNull_ = true; return; }
        count_ = header.count;
        lengths_.reserve(count_);
        for (uint8_t i = 0; i < count_ && !in.Failed(); ++i)
            lengths_.push_back(in.ReadVarIntLength());
        next_ = in.Position();
    }

    [[nodiscard]] bool IsNull() const noexcept { return isNull_; }
    [[nodiscard]] uint8_t Count() const noexcept { return count_; }

    /// Reads the next member. Returns false when the sender did not include it,
    /// leaving `out` untouched.
    template<typename T>
    bool ReadMember(T& out) {
        if (isNull_ || index_ >= count_) return false;
        const size_t start = next_;
        const size_t end = start + lengths_[index_];
        ++index_;
        in_->Seek(start);
        if (in_->Failed()) return false;
        in_->Read(out);
        next_ = end;
        in_->Seek(end);
        return true;
    }

    /// Skips a member without decoding it.
    bool SkipMember() {
        if (isNull_ || index_ >= count_) return false;
        next_ += lengths_[index_];
        ++index_;
        in_->Seek(next_);
        return true;
    }

    /// Positions the reader after the whole object, skipping any members the
    /// local type does not know about.
    void Finish() {
        while (index_ < count_) SkipMember();
        if (!isNull_) in_->Seek(next_);
    }

private:
    MemoryPackReader* in_;
    std::vector<uint32_t> lengths_;
    uint8_t count_ = 0;
    uint8_t index_ = 0;
    size_t next_ = 0;
    bool isNull_ = false;
};

// -- Core formatters ------------------------------------------------------------

/// Primary template: reached only for types with no known mapping.
template<typename T, typename Enable>
struct MemoryPackFormatter {
    static_assert(detail::always_false<T>,
                  "memorypack: no serializer for this type. Specialize "
                  "memorypack::IMemoryPackable<T> (or use MEMORYPACK_DEFINE) "
                  "for your own types - see docs/serialization.md.");
};

/// bool -> 1 byte.
template<>
struct MemoryPackFormatter<bool> {
    static void Serialize(MemoryPackWriter& w, const bool& v) { w.WriteBool(v); }
    static void Deserialize(MemoryPackReader& r, bool& v) { v = r.ReadBool(); }
};

/// Arithmetic types -> little-endian fixed size.
template<typename T>
struct MemoryPackFormatter<T, std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>>> {
    static void Serialize(MemoryPackWriter& w, const T& v) {
        if constexpr (std::is_same_v<T, float>)       w.WriteFloat(v);
        else if constexpr (std::is_same_v<T, double>) w.WriteDouble(v);
        else if constexpr (sizeof(T) == 1)            w.WriteUInt8(static_cast<uint8_t>(v));
        else if constexpr (sizeof(T) == 2)            w.WriteUInt16(static_cast<uint16_t>(v));
        else if constexpr (sizeof(T) == 4)            w.WriteUInt32(static_cast<uint32_t>(v));
        else                                          w.WriteUInt64(static_cast<uint64_t>(v));
    }
    static void Deserialize(MemoryPackReader& r, T& v) {
        if constexpr (std::is_same_v<T, float>)       v = r.ReadFloat();
        else if constexpr (std::is_same_v<T, double>) v = r.ReadDouble();
        else if constexpr (sizeof(T) == 1)            v = static_cast<T>(r.ReadUInt8());
        else if constexpr (sizeof(T) == 2)            v = static_cast<T>(r.ReadUInt16());
        else if constexpr (sizeof(T) == 4)            v = static_cast<T>(r.ReadUInt32());
        else                                          v = static_cast<T>(r.ReadUInt64());
    }
};

/// Enums -> underlying integer type.
template<typename T>
struct MemoryPackFormatter<T, std::enable_if_t<std::is_enum_v<T>>> {
    using U = std::underlying_type_t<T>;
    static void Serialize(MemoryPackWriter& w, const T& v) { w.WriteEnum(v); }
    static void Deserialize(MemoryPackReader& r, T& v) { v = r.ReadEnum<T>(); }
};

/// std::string -> MemoryPack UTF-8 string.
template<>
struct MemoryPackFormatter<std::string> {
    static void Serialize(MemoryPackWriter& w, const std::string& v) { w.WriteString(v); }
    static void Deserialize(MemoryPackReader& r, std::string& v) { r.ReadString(v); }
};

/// std::u16string -> MemoryPack UTF-16 string.
template<>
struct MemoryPackFormatter<std::u16string> {
    static void Serialize(MemoryPackWriter& w, const std::u16string& v) { w.WriteStringUtf16(v); }
    static void Deserialize(MemoryPackReader& r, std::u16string& v) {
        v.clear();
        auto s = r.ReadString();
        if (!s) return;
        // Transcode UTF-8 back to UTF-16.
        const auto& u8 = *s;
        for (size_t i = 0; i < u8.size(); ) {
            auto b0 = static_cast<uint8_t>(u8[i]);
            uint32_t cp; size_t len;
            if (b0 < 0x80)            { cp = b0;          len = 1; }
            else if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1Fu; len = 2; }
            else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0Fu; len = 3; }
            else                          { cp = b0 & 0x07u; len = 4; }
            if (i + len > u8.size()) break;
            for (size_t k = 1; k < len; ++k)
                cp = (cp << 6) | (static_cast<uint8_t>(u8[i + k]) & 0x3Fu);
            i += len;
            if (cp <= 0xFFFF) {
                v.push_back(static_cast<char16_t>(cp));
            } else {
                cp -= 0x10000u;
                v.push_back(static_cast<char16_t>(0xD800u + (cp >> 10)));
                v.push_back(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
            }
        }
    }
};

/// User types with an IMemoryPackable specialization.
template<typename T>
struct MemoryPackFormatter<T, std::enable_if_t<
    detail::has_memorypackable_v<T> && !std::is_arithmetic_v<T> && !std::is_enum_v<T>>> {
    static void Serialize(MemoryPackWriter& w, const T& v) { IMemoryPackable<T>::Serialize(w, &v); }
    static void Deserialize(MemoryPackReader& r, T& v) { IMemoryPackable<T>::Deserialize(r, v); }
};

// -- Generic dispatch definitions -----------------------------------------------

template<typename T>
void MemoryPackWriter::Write(const T& value) {
    MemoryPackFormatter<T>::Serialize(*this, value);
}

template<typename T>
void MemoryPackReader::Read(T& out) {
    MemoryPackFormatter<T>::Deserialize(*this, out);
}

// -- Top-level API --------------------------------------------------------------

/// Serializes a value into a new buffer.
template<typename T>
[[nodiscard]] std::vector<uint8_t> Serialize(const T& value) {
    MemoryPackWriter writer;
    writer.Write(value);
    return writer.TakeBuffer();
}

/// Serializes a value into a caller-owned buffer, appending to it.
template<typename T>
void Serialize(const T& value, std::vector<uint8_t>& out) {
    MemoryPackWriter writer(out);
    writer.Write(value);
}

/// Serializes into a fixed-size buffer. Returns the number of bytes written,
/// or 0 if the value did not fit (with exceptions disabled).
template<typename T>
size_t SerializeTo(std::span<uint8_t> buffer, const T& value) {
    MemoryPackWriter writer(buffer);
    writer.Write(value);
    return writer.Failed() ? 0 : writer.Size();
}

template<typename T>
[[nodiscard]] T Deserialize(std::span<const uint8_t> data) {
    MemoryPackReader reader(data);
    T value{};
    reader.Read(value);
    return value;
}

template<typename T>
[[nodiscard]] T Deserialize(const std::vector<uint8_t>& data) {
    return Deserialize<T>(std::span<const uint8_t>(data));
}

template<typename T>
void Deserialize(const uint8_t* data, size_t size, T& out) {
    MemoryPackReader reader(data, size);
    reader.Read(out);
}

/// Like Deserialize, but also requires the whole input to be consumed - a cheap
/// way to catch a C#/C++ member-order mismatch during development.
template<typename T>
[[nodiscard]] T DeserializeExact(std::span<const uint8_t> data) {
    MemoryPackReader reader(data);
    T value{};
    reader.Read(value);
    reader.RequireEnd();
    return value;
}

#if MEMORYPACK_HAS_EXPECTED
/// Exception-free deserialization.
template<typename T>
[[nodiscard]] std::expected<T, MemoryPackError> TryDeserialize(std::span<const uint8_t> data) {
    MemoryPackReader reader(data);
    T value{};
    MEMORYPACK_TRY {
        reader.Read(value);
    } MEMORYPACK_CATCH_ALL {
        return std::unexpected(reader.Error() == MemoryPackError::None
                                   ? MemoryPackError::BufferUnderflow
                                   : reader.Error());
    }
    if (reader.Failed()) return std::unexpected(reader.Error());
    return value;
}

/// Exception-free serialization into a fixed-size buffer.
template<typename T>
[[nodiscard]] std::expected<size_t, MemoryPackError> TrySerializeTo(
    std::span<uint8_t> buffer, const T& value) {
    MemoryPackWriter writer(buffer);
    MEMORYPACK_TRY {
        writer.Write(value);
    } MEMORYPACK_CATCH_ALL {
        return std::unexpected(writer.Error() == MemoryPackError::None
                                   ? MemoryPackError::BufferOverflow
                                   : writer.Error());
    }
    if (writer.Failed()) return std::unexpected(writer.Error());
    return writer.Size();
}
#endif

#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

} // namespace memorypack

// -- MEMORYPACK_DEFINE ----------------------------------------------------------
//
//   struct PlayerState { int32_t id; float x, y, z; std::string name; };
//   MEMORYPACK_DEFINE(PlayerState, id, x, y, z, name);
//
// Generates the IMemoryPackable specialization, keeping member count and order in
// one place. Version tolerance is built in: members the sender did not write are
// left at their default value.

#define MEMORYPACK_PP_EXPAND(x) x

#define MEMORYPACK_PP_NARG(...) \
    MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_NARG_(__VA_ARGS__, MEMORYPACK_PP_RSEQ()))
#define MEMORYPACK_PP_NARG_(...) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_ARG_N(__VA_ARGS__))
#define MEMORYPACK_PP_ARG_N( \
     _1, _2, _3, _4, _5, _6, _7, _8, _9,_10,_11,_12,_13,_14,_15,_16, \
    _17,_18,_19,_20,_21,_22,_23,_24,_25,_26,_27,_28,_29,_30,_31,_32, N, ...) N
#define MEMORYPACK_PP_RSEQ() \
    32,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17, \
    16,15,14,13,12,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

#define MEMORYPACK_PP_CAT(a, b) MEMORYPACK_PP_CAT_(a, b)
#define MEMORYPACK_PP_CAT_(a, b) a##b

#define MEMORYPACK_PP_FE_1(m, x)      m(x)
#define MEMORYPACK_PP_FE_2(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_1(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_3(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_2(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_4(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_3(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_5(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_4(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_6(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_5(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_7(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_6(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_8(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_7(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_9(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_8(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_10(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_9(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_11(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_10(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_12(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_11(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_13(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_12(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_14(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_13(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_15(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_14(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_16(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_15(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_17(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_16(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_18(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_17(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_19(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_18(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_20(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_19(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_21(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_20(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_22(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_21(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_23(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_22(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_24(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_23(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_25(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_24(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_26(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_25(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_27(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_26(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_28(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_27(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_29(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_28(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_30(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_29(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_31(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_30(m, __VA_ARGS__))
#define MEMORYPACK_PP_FE_32(m, x, ...) m(x) MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_FE_31(m, __VA_ARGS__))

#define MEMORYPACK_PP_FOREACH(m, ...) \
    MEMORYPACK_PP_EXPAND(MEMORYPACK_PP_CAT(MEMORYPACK_PP_FE_, MEMORYPACK_PP_NARG(__VA_ARGS__))(m, __VA_ARGS__))

#define MEMORYPACK_DETAIL_WRITE_MEMBER(name) w.Write(v->name);
// `count > index++` reads the current index and then advances it, so members are
// checked in declaration order without needing an indexed macro expansion.
#define MEMORYPACK_DETAIL_READ_MEMBER(name) \
    if (memorypackHeader.count > memorypackIndex++) r.Read(v.name);

/// Generates memorypack::IMemoryPackable<Type> from a member list.
#define MEMORYPACK_DEFINE(Type, ...)                                                       \
    namespace memorypack {                                                                 \
    template<>                                                                             \
    struct IMemoryPackable<Type> {                                                         \
        static constexpr uint8_t MemberCount =                                             \
            static_cast<uint8_t>(MEMORYPACK_PP_NARG(__VA_ARGS__));                         \
        static_assert(MEMORYPACK_PP_NARG(__VA_ARGS__) <= 249,                              \
                      "an object may have at most 249 members on the wire");               \
        static void Serialize(MemoryPackWriter& w, const Type* v) {                        \
            if (!v) { w.WriteNullObjectHeader(); return; }                                 \
            w.WriteObjectHeader(MemberCount);                                              \
            MEMORYPACK_PP_FOREACH(MEMORYPACK_DETAIL_WRITE_MEMBER, __VA_ARGS__)             \
        }                                                                                  \
        static void Deserialize(MemoryPackReader& r, Type& v) {                            \
            const auto memorypackHeader = r.ReadObjectHeader();                            \
            if (memorypackHeader.isNull) { v = Type{}; return; }                           \
            if (!r.EnterObject()) return;                                                  \
            unsigned memorypackIndex = 0;                                                  \
            MEMORYPACK_PP_FOREACH(MEMORYPACK_DETAIL_READ_MEMBER, __VA_ARGS__)              \
            (void)memorypackIndex;                                                         \
            r.LeaveObject();                                                               \
        }                                                                                  \
    };                                                                                     \
    }

/// MEMORYPACK_DEFINE for a type with no serialized members (C# writes a single
/// zero byte for such an object).
#define MEMORYPACK_DEFINE_EMPTY(Type)                                                      \
    namespace memorypack {                                                                 \
    template<>                                                                             \
    struct IMemoryPackable<Type> {                                                         \
        static constexpr uint8_t MemberCount = 0;                                          \
        static void Serialize(MemoryPackWriter& w, const Type* v) {                        \
            if (!v) { w.WriteNullObjectHeader(); return; }                                 \
            w.WriteObjectHeader(0);                                                        \
        }                                                                                  \
        static void Deserialize(MemoryPackReader& r, Type& v) {                            \
            const auto memorypackHeader = r.ReadObjectHeader();                            \
            if (memorypackHeader.isNull) { v = Type{}; return; }                           \
        }                                                                                  \
    };                                                                                     \
    }

/// Marks a trivially-copyable C++ struct as the mapping of a C# unmanaged struct.
/// `ExpectedSize` is checked against sizeof to catch layout drift early.
///
/// Unchecked with respect to padding: values of such a type must be
/// value-initialized (`T v{};`) before use, or the struct's padding bytes -
/// indeterminate otherwise - are copied to the wire verbatim. See
/// docs/security.md#unmanaged-struct-padding. Prefer MEMORYPACK_UNMANAGED_EXACT
/// (compile-time proof there is no padding) or MEMORYPACK_UNMANAGED_SCRUBBED
/// (padding always zeroed at runtime) below when you can list the members.
#define MEMORYPACK_UNMANAGED(Type, ExpectedSize)                                           \
    static_assert(std::is_trivially_copyable_v<Type>,                                      \
                  #Type " must be trivially copyable to map a C# unmanaged struct");        \
    static_assert(sizeof(Type) == (ExpectedSize),                                          \
                  #Type " must be " #ExpectedSize " bytes to match the C# layout");         \
    namespace memorypack {                                                                 \
    template<> struct IsUnmanaged<Type> : std::true_type {};                               \
    template<> struct MemoryPackFormatter<Type> {                                          \
        static void Serialize(MemoryPackWriter& w, const Type& v) { w.WriteUnmanaged(v); } \
        static void Deserialize(MemoryPackReader& r, Type& v) { r.ReadUnmanaged(v); }      \
    };                                                                                     \
    }

// -- MEMORYPACK_UNMANAGED_EXACT / MEMORYPACK_UNMANAGED_SCRUBBED ------------------
//
// Both take the same (Type, ExpectedSize) as MEMORYPACK_UNMANAGED, plus the
// struct's member names in declaration order, and turn the padding caveat above
// from a documentation warning into either a compile error or a runtime
// guarantee:
//
//   MEMORYPACK_UNMANAGED_EXACT(Type, size, m1, m2, ...)
//     Proves at compile time that Type has NO padding (the sum of the listed
//     members' sizes equals sizeof(Type)) - so there is nothing left for the
//     padding caveat to apply to. Fails to compile otherwise. Use this for
//     `[StructLayout(Pack = 1)]` structs, or any type you can show is naturally
//     packed (e.g. an all-float vector).
//
//   MEMORYPACK_UNMANAGED_SCRUBBED(Type, size, m1, m2, ...)
//     For a type that DOES have padding: serialization copies each member's
//     bytes to its real offset in a zero-filled byte buffer (never assigning
//     into an actual `Type` instance - see the note further down for why that
//     distinction matters), so the wire bytes are always zero where the
//     source struct had padding, no matter how the caller's instance was
//     constructed. Costs a small stack buffer and a per-member memcpy.
//
// Listed members must be scalars, or types with no padding of their own -
// MEMORYPACK_UNMANAGED_SCRUBBED's member-by-member copy does not recurse, so a
// nested type with its own padding would still leak it into the buffer.

#define MEMORYPACK_DETAIL_MEMBER_SIZE(name) + sizeof(((MemorypackProbeType*)nullptr)->name)

/// MEMORYPACK_UNMANAGED, plus a compile-time proof that Type has no padding.
/// See the block comment above.
#define MEMORYPACK_UNMANAGED_EXACT(Type, ExpectedSize, ...)                                \
    static_assert(std::is_trivially_copyable_v<Type>,                                      \
                  #Type " must be trivially copyable to map a C# unmanaged struct");        \
    static_assert(sizeof(Type) == (ExpectedSize),                                          \
                  #Type " must be " #ExpectedSize " bytes to match the C# layout");         \
    namespace memorypack { namespace detail {                                              \
    template<> struct UnmanagedProbe<Type> {                                               \
        using MemorypackProbeType = Type;                                                  \
        static constexpr size_t MemberBytes =                                              \
            0 MEMORYPACK_PP_FOREACH(MEMORYPACK_DETAIL_MEMBER_SIZE, __VA_ARGS__);           \
    };                                                                                     \
    }}                                                                                     \
    static_assert(memorypack::detail::UnmanagedProbe<Type>::MemberBytes == sizeof(Type),   \
                  #Type " has padding between members, so its padding bytes would go on "  \
                  "the wire. Use MEMORYPACK_UNMANAGED_SCRUBBED, or [StructLayout(Pack=1)]");\
    namespace memorypack {                                                                 \
    template<> struct IsUnmanaged<Type> : std::true_type {};                               \
    template<> struct MemoryPackFormatter<Type> {                                          \
        static void Serialize(MemoryPackWriter& w, const Type& v) { w.WriteUnmanaged(v); } \
        static void Deserialize(MemoryPackReader& r, Type& v) { r.ReadUnmanaged(v); }      \
    };                                                                                     \
    }

// NOTE on the implementation below: this deliberately does NOT build a
// `Type memorypackTmp{};` and assign members into it (`memorypackTmp.m = v.m;`
// for each m). That looks equivalent, but measured on MSVC it is not: assigning
// every declared member from `v` gives the compiler license to notice that the
// result has the same member values as `memorypackTmp = v;` would (padding is
// unspecified either way, so both are "equally valid") and fold the whole
// sequence into a single whole-object copy FROM v - silently reintroducing v's
// padding and defeating the entire macro. Building the wire bytes in a plain
// `uint8_t` array that is never itself a `Type` instance leaves the compiler
// no such shortcut: there is no second `Type` object for it to relate to `v`.
#define MEMORYPACK_DETAIL_SCRUB_MEMBER(name) \
    std::memcpy(memorypackBuf.data() + offsetof(MemorypackScrubType, name), &v.name, sizeof(v.name));

/// MEMORYPACK_UNMANAGED, but serialization always builds the wire bytes in a
/// zero-filled byte buffer, copying each member to its real offset, so a
/// struct with padding never leaks it. See the block comment above.
#define MEMORYPACK_UNMANAGED_SCRUBBED(Type, ExpectedSize, ...)                             \
    static_assert(std::is_trivially_copyable_v<Type>,                                      \
                  #Type " must be trivially copyable to map a C# unmanaged struct");        \
    static_assert(std::is_standard_layout_v<Type>,                                         \
                  #Type " must be standard-layout for MEMORYPACK_UNMANAGED_SCRUBBED to "    \
                  "compute member offsets");                                               \
    static_assert(sizeof(Type) == (ExpectedSize),                                          \
                  #Type " must be " #ExpectedSize " bytes to match the C# layout");         \
    namespace memorypack {                                                                 \
    template<> struct IsUnmanaged<Type> : std::true_type {};                               \
    template<> struct MemoryPackFormatter<Type> {                                          \
        static void Serialize(MemoryPackWriter& w, const Type& v) {                        \
            using MemorypackScrubType = Type;                                              \
            std::array<uint8_t, sizeof(Type)> memorypackBuf{};                             \
            MEMORYPACK_PP_FOREACH(MEMORYPACK_DETAIL_SCRUB_MEMBER, __VA_ARGS__)              \
            w.WriteBytes(std::span<const uint8_t>(memorypackBuf));                         \
        }                                                                                  \
        static void Deserialize(MemoryPackReader& r, Type& v) { r.ReadUnmanaged(v); }      \
    };                                                                                     \
    }
