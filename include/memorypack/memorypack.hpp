#pragma once
/// @file memorypack.hpp
/// @brief MemoryPack Binary Wire Format compatible C++ header-only library.
///        Interoperable with C# MemoryPack (https://github.com/Cysharp/MemoryPack).
///        Requires C++23 or later.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include <tuple>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <bit>
#include <span>

namespace memorypack {

// ── Wire Format Constants ──────────────────────────────────────────────────────
constexpr uint8_t NULL_OBJECT     = 255;
constexpr uint8_t WIDE_TAG        = 250;
constexpr int32_t NULL_COLLECTION = -1;

// ── Endian Utilities ───────────────────────────────────────────────────────────
namespace detail {

template<typename T>
inline T endian_convert(T value) noexcept {
    if constexpr (std::endian::native == std::endian::little || sizeof(T) == 1) {
        return value;
    } else if constexpr (std::is_integral_v<T>) {
        return std::byteswap(value);
    } else {
        // float / double
        using U = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
        U raw;
        std::memcpy(&raw, &value, sizeof(T));
        raw = std::byteswap(raw);
        T result;
        std::memcpy(&result, &raw, sizeof(T));
        return result;
    }
}

} // namespace detail

// Forward declaration for WriteValue / ReadValue generic helpers
template<typename T>
struct IMemoryPackable;

// ── MemoryPackWriter ───────────────────────────────────────────────────────────
/// Supports three buffer modes:
///   1. Default (internal vector) — grows automatically, use TakeBuffer() to move out
///   2. External std::vector<uint8_t>& — writes into caller-owned vector
///   3. External fixed buffer (uint8_t* or std::array) — fixed capacity, throws on overflow
class MemoryPackWriter {
public:
    // Default: uses internal growable vector
    MemoryPackWriter() : vec_(&ownedBuffer_) {}

    // External vector buffer (caller-owned, growable)
    explicit MemoryPackWriter(std::vector<uint8_t>& externalBuffer)
        : vec_(&externalBuffer) {}

    // External fixed-size buffer (raw pointer + capacity)
    MemoryPackWriter(uint8_t* data, size_t capacity)
        : fixedBuf_(data), fixedCap_(capacity) {}

    // External std::array buffer
    template<size_t N>
    explicit MemoryPackWriter(std::array<uint8_t, N>& arr)
        : fixedBuf_(arr.data()), fixedCap_(N) {}

    // Non-copyable, non-movable (vec_ may point to &ownedBuffer_)
    MemoryPackWriter(const MemoryPackWriter&) = delete;
    MemoryPackWriter& operator=(const MemoryPackWriter&) = delete;
    MemoryPackWriter(MemoryPackWriter&&) = delete;
    MemoryPackWriter& operator=(MemoryPackWriter&&) = delete;

    void Reserve(size_t capacity) {
        if (vec_) vec_->reserve(capacity);
    }

    // Object Header
    void WriteObjectHeader(uint8_t memberCount) { AppendByte(memberCount); }
    void WriteNullObjectHeader()                { AppendByte(NULL_OBJECT); }

    // Collection Header
    void WriteCollectionHeader(int32_t length)  { WriteRaw(length); }
    void WriteNullCollectionHeader()            { WriteRaw(NULL_COLLECTION); }

    // Primitives (Little-Endian, fixed-size, no VarInt)
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

    // String (UTF-8)
    void WriteString(const std::string& s) {
        auto byteLen = static_cast<int32_t>(s.size());
        WriteRaw(byteLen);
        AppendBytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    void WriteNullString() {
        WriteRaw(static_cast<int32_t>(-1));
    }

    void WriteOptionalString(const std::optional<std::string>& s) {
        if (s) WriteString(*s);
        else   WriteNullString();
    }

    // Vector of arithmetic types (bulk-copy fast path on LE)
    template<typename T>
    requires std::is_arithmetic_v<T>
    void WriteVector(const std::vector<T>& vec) {
        WriteCollectionHeader(static_cast<int32_t>(vec.size()));
        if constexpr (std::endian::native == std::endian::little) {
            AppendBytes(reinterpret_cast<const uint8_t*>(vec.data()), vec.size() * sizeof(T));
        } else {
            for (const auto& elem : vec) WriteRaw(elem);
        }
    }

    // C-style fixed array — writes only 'count' elements as a collection
    template<typename T>
    requires std::is_arithmetic_v<T>
    void WriteArray(const T* arr, int32_t count) {
        WriteCollectionHeader(count);
        if (count <= 0) return;
        if constexpr (std::endian::native == std::endian::little) {
            AppendBytes(reinterpret_cast<const uint8_t*>(arr), static_cast<size_t>(count) * sizeof(T));
        } else {
            for (int32_t i = 0; i < count; ++i) WriteRaw(arr[i]);
        }
    }

    // Vector of strings
    void WriteStringVector(const std::vector<std::string>& vec) {
        WriteCollectionHeader(static_cast<int32_t>(vec.size()));
        for (const auto& s : vec) WriteString(s);
    }

    // Raw bytes
    void WriteBytes(std::span<const uint8_t> data) {
        AppendBytes(data.data(), data.size());
    }

    // ── Extended Types ────────────────────────────────────────────────────────

    // Enum (serialized as underlying integer type)
    template<typename T>
    requires std::is_enum_v<T>
    void WriteEnum(T v) {
        WriteRaw(static_cast<std::underlying_type_t<T>>(v));
    }

    // std::array<T, N> as collection
    template<typename T, size_t N>
    requires std::is_arithmetic_v<T>
    void WriteArray(const std::array<T, N>& arr) {
        WriteCollectionHeader(static_cast<int32_t>(N));
        if constexpr (std::endian::native == std::endian::little) {
            AppendBytes(reinterpret_cast<const uint8_t*>(arr.data()), N * sizeof(T));
        } else {
            for (const auto& elem : arr) WriteRaw(elem);
        }
    }

    // std::map<K, V>
    template<typename K, typename V>
    void WriteMap(const std::map<K, V>& map) {
        WriteCollectionHeader(static_cast<int32_t>(map.size()));
        for (const auto& [key, value] : map) {
            WriteValue(key);
            WriteValue(value);
        }
    }

    // std::unordered_map<K, V>
    template<typename K, typename V>
    void WriteMap(const std::unordered_map<K, V>& map) {
        WriteCollectionHeader(static_cast<int32_t>(map.size()));
        for (const auto& [key, value] : map) {
            WriteValue(key);
            WriteValue(value);
        }
    }

    // std::tuple<Ts...> (serialized as Object)
    template<typename... Ts>
    void WriteTuple(const std::tuple<Ts...>& t) {
        WriteObjectHeader(static_cast<uint8_t>(sizeof...(Ts)));
        std::apply([this](const auto&... args) {
            (WriteValue(args), ...);
        }, t);
    }

    // Buffer access — works for all modes
    [[nodiscard]] const uint8_t* Data() const noexcept {
        if (vec_) return vec_->data();
        return fixedBuf_;
    }

    [[nodiscard]] size_t Size() const noexcept {
        if (vec_) return vec_->size();
        return fixedPos_;
    }

    [[nodiscard]] std::span<const uint8_t> GetSpan() const noexcept {
        return { Data(), Size() };
    }

    // Returns the underlying vector (vector modes only, throws for fixed buffer)
    [[nodiscard]] const std::vector<uint8_t>& GetBuffer() const {
        if (!vec_)
            throw std::runtime_error("MemoryPackWriter: GetBuffer() not available for fixed-size buffer");
        return *vec_;
    }

    // Move internal buffer out (default constructor only)
    [[nodiscard]] std::vector<uint8_t> TakeBuffer() {
        if (vec_ != &ownedBuffer_)
            throw std::runtime_error("MemoryPackWriter: TakeBuffer() only available for owned buffer");
        return std::move(ownedBuffer_);
    }

    // Remaining capacity for fixed-size buffers (returns SIZE_MAX for vector modes)
    [[nodiscard]] size_t RemainingCapacity() const noexcept {
        if (vec_) return SIZE_MAX;
        return fixedCap_ - fixedPos_;
    }

    // Reset write position (for reusing a fixed buffer or clearing a vector)
    void Clear() noexcept {
        if (vec_) vec_->clear();
        else fixedPos_ = 0;
    }

private:
    std::vector<uint8_t> ownedBuffer_;
    std::vector<uint8_t>* vec_ = nullptr;
    uint8_t* fixedBuf_ = nullptr;
    size_t fixedCap_ = 0;
    size_t fixedPos_ = 0;

    // Generic value writer for map/tuple element serialization
    template<typename T>
    void WriteValue(const T& v) {
        if constexpr (std::is_enum_v<T>) {
            WriteRaw(static_cast<std::underlying_type_t<T>>(v));
        } else if constexpr (std::is_same_v<T, bool>) {
            WriteBool(v);
        } else if constexpr (std::is_arithmetic_v<T> && sizeof(T) == 1) {
            AppendByte(static_cast<uint8_t>(v));
        } else if constexpr (std::is_arithmetic_v<T>) {
            WriteRaw(v);
        } else if constexpr (std::is_same_v<T, std::string>) {
            WriteString(v);
        } else {
            IMemoryPackable<T>::Serialize(*this, &v);
        }
    }

    void AppendByte(uint8_t b) {
        if (vec_) {
            vec_->push_back(b);
        } else {
            if (fixedPos_ >= fixedCap_)
                throw std::runtime_error("MemoryPackWriter: fixed buffer overflow");
            fixedBuf_[fixedPos_++] = b;
        }
    }

    void AppendBytes(const uint8_t* src, size_t n) {
        if (n == 0) return;
        if (vec_) {
            vec_->insert(vec_->end(), src, src + n);
        } else {
            if (fixedPos_ + n > fixedCap_)
                throw std::runtime_error("MemoryPackWriter: fixed buffer overflow");
            std::memcpy(fixedBuf_ + fixedPos_, src, n);
            fixedPos_ += n;
        }
    }

    template<typename T>
    void WriteRaw(T value) {
        value = detail::endian_convert(value);
        AppendBytes(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
    }
};

// ── MemoryPackReader ───────────────────────────────────────────────────────────
class MemoryPackReader {
public:
    MemoryPackReader(const uint8_t* data, size_t size)
        : data_(data), size_(size) {}

    explicit MemoryPackReader(std::span<const uint8_t> buf)
        : data_(buf.data()), size_(buf.size()) {}

    // Object Header — returns {memberCount, isNull}
    std::pair<uint8_t, bool> ReadObjectHeader() {
        uint8_t b = ReadByte();
        if (b == NULL_OBJECT) return {0, true};
        return {b, false};
    }

    [[nodiscard]] bool PeekIsNull() const {
        EnsureBytes(1);
        return data_[pos_] == NULL_OBJECT;
    }

    // Collection Header — returns length (-1 = null)
    int32_t ReadCollectionHeader() { return ReadRaw<int32_t>(); }

    // Primitives
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

    // String (UTF-8) — returns nullopt for null string
    std::optional<std::string> ReadString() {
        int32_t byteLen = ReadRaw<int32_t>();
        if (byteLen == -1) return std::nullopt;
        if (byteLen < 0) throw std::runtime_error("MemoryPackReader: invalid string length");
        EnsureBytes(static_cast<size_t>(byteLen));
        std::string result(reinterpret_cast<const char*>(data_ + pos_), static_cast<size_t>(byteLen));
        pos_ += static_cast<size_t>(byteLen);
        return result;
    }

    // Vector of arithmetic types (bulk-copy fast path on LE)
    template<typename T>
    requires std::is_arithmetic_v<T>
    std::vector<T> ReadVector() {
        int32_t len = ReadCollectionHeader();
        if (len < 0) return {};
        auto count = static_cast<size_t>(len);
        std::vector<T> result(count);
        if (count == 0) return result;
        EnsureBytes(count * sizeof(T));
        if constexpr (std::endian::native == std::endian::little) {
            std::memcpy(result.data(), data_ + pos_, count * sizeof(T));
            pos_ += count * sizeof(T);
        } else {
            for (size_t i = 0; i < count; ++i)
                result[i] = ReadRaw<T>();
        }
        return result;
    }

    // C-style fixed array — reads into arr (up to maxCount), skips excess.
    // Returns the number of elements actually written into arr.
    template<typename T>
    requires std::is_arithmetic_v<T>
    int32_t ReadArray(T* arr, int32_t maxCount) {
        int32_t len = ReadCollectionHeader();
        if (len <= 0) return 0;
        int32_t readCount = (len <= maxCount) ? len : maxCount;
        if (readCount > 0) {
            auto bytes = static_cast<size_t>(readCount) * sizeof(T);
            EnsureBytes(bytes);
            if constexpr (std::endian::native == std::endian::little) {
                std::memcpy(arr, data_ + pos_, bytes);
                pos_ += bytes;
            } else {
                for (int32_t i = 0; i < readCount; ++i) arr[i] = ReadRaw<T>();
            }
        }
        // Skip excess elements that don't fit in arr
        int32_t skipCount = len - readCount;
        if (skipCount > 0) {
            auto skipBytes = static_cast<size_t>(skipCount) * sizeof(T);
            EnsureBytes(skipBytes);
            pos_ += skipBytes;
        }
        return readCount;
    }

    // Vector of strings
    std::vector<std::string> ReadStringVector() {
        int32_t len = ReadCollectionHeader();
        if (len < 0) return {};
        std::vector<std::string> result;
        result.reserve(static_cast<size_t>(len));
        for (int32_t i = 0; i < len; ++i) {
            auto s = ReadString();
            result.push_back(std::move(s).value_or(""));
        }
        return result;
    }

    // ── Extended Types ────────────────────────────────────────────────────────

    // Enum
    template<typename T>
    requires std::is_enum_v<T>
    T ReadEnum() {
        return static_cast<T>(ReadRaw<std::underlying_type_t<T>>());
    }

    // std::array<T, N> — reads collection into fixed-size array, skips excess
    template<typename T, size_t N>
    requires std::is_arithmetic_v<T>
    std::array<T, N> ReadArray() {
        int32_t len = ReadCollectionHeader();
        std::array<T, N> result{};
        if (len <= 0) return result;
        size_t readCount = (static_cast<size_t>(len) <= N) ? static_cast<size_t>(len) : N;
        if (readCount > 0) {
            EnsureBytes(readCount * sizeof(T));
            if constexpr (std::endian::native == std::endian::little) {
                std::memcpy(result.data(), data_ + pos_, readCount * sizeof(T));
                pos_ += readCount * sizeof(T);
            } else {
                for (size_t i = 0; i < readCount; ++i)
                    result[i] = ReadRaw<T>();
            }
        }
        if (static_cast<size_t>(len) > readCount) {
            auto skipBytes = (static_cast<size_t>(len) - readCount) * sizeof(T);
            EnsureBytes(skipBytes);
            pos_ += skipBytes;
        }
        return result;
    }

    // std::map<K, V>
    template<typename K, typename V>
    std::map<K, V> ReadMap() {
        int32_t len = ReadCollectionHeader();
        if (len < 0) return {};
        std::map<K, V> result;
        for (int32_t i = 0; i < len; ++i) {
            K key = ReadValue<K>();
            V value = ReadValue<V>();
            result.emplace(std::move(key), std::move(value));
        }
        return result;
    }

    // std::unordered_map<K, V>
    template<typename K, typename V>
    std::unordered_map<K, V> ReadUnorderedMap() {
        int32_t len = ReadCollectionHeader();
        if (len < 0) return {};
        std::unordered_map<K, V> result;
        result.reserve(static_cast<size_t>(len));
        for (int32_t i = 0; i < len; ++i) {
            K key = ReadValue<K>();
            V value = ReadValue<V>();
            result.emplace(std::move(key), std::move(value));
        }
        return result;
    }

    // std::tuple<Ts...>
    template<typename... Ts>
    std::tuple<Ts...> ReadTuple() {
        auto [cnt, isNull] = ReadObjectHeader();
        if (isNull) return {};
        return ReadTupleImpl<Ts...>(cnt, std::index_sequence_for<Ts...>{});
    }

    void   Advance(size_t n) { EnsureBytes(n); pos_ += n; }
    [[nodiscard]] bool   IsEnd()     const noexcept { return pos_ >= size_; }
    [[nodiscard]] size_t Position()  const noexcept { return pos_; }
    [[nodiscard]] size_t Remaining() const noexcept { return size_ - pos_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;

    uint8_t ReadByte() {
        EnsureBytes(1);
        return data_[pos_++];
    }

    template<typename T>
    T ReadRaw() {
        EnsureBytes(sizeof(T));
        T value;
        std::memcpy(&value, data_ + pos_, sizeof(T));
        pos_ += sizeof(T);
        return detail::endian_convert(value);
    }

    // Generic value reader for map/tuple element deserialization
    template<typename T>
    T ReadValue() {
        if constexpr (std::is_enum_v<T>) {
            return static_cast<T>(ReadRaw<std::underlying_type_t<T>>());
        } else if constexpr (std::is_same_v<T, bool>) {
            return ReadBool();
        } else if constexpr (std::is_arithmetic_v<T> && sizeof(T) == 1) {
            return static_cast<T>(ReadByte());
        } else if constexpr (std::is_arithmetic_v<T>) {
            return ReadRaw<T>();
        } else if constexpr (std::is_same_v<T, std::string>) {
            auto s = ReadString();
            return s.value_or("");
        } else {
            T v{};
            IMemoryPackable<T>::Deserialize(*this, v);
            return v;
        }
    }

    // Tuple element reader — reads elements in order with version tolerance
    template<typename... Ts, size_t... Is>
    std::tuple<Ts...> ReadTupleImpl(uint8_t cnt, std::index_sequence<Is...>) {
        std::tuple<Ts...> result{};
        ((Is < cnt ? (std::get<Is>(result) = ReadValue<Ts>(), 0) : 0), ...);
        return result;
    }

    void EnsureBytes(size_t n) const {
        if (pos_ + n > size_)
            throw std::runtime_error("MemoryPackReader: buffer underflow");
    }
};

// ── IMemoryPackable (CRTP Interface) ───────────────────────────────────────────
template<typename T>
struct IMemoryPackable {
    static void Serialize(MemoryPackWriter& writer, const T* value);
    static void Deserialize(MemoryPackReader& reader, T& value);
};

// ── Top-level Serialize / Deserialize API ──────────────────────────────────────
template<typename T>
std::vector<uint8_t> Serialize(const T& value) {
    MemoryPackWriter writer;
    IMemoryPackable<T>::Serialize(writer, &value);
    return writer.TakeBuffer();
}

template<typename T>
T Deserialize(std::span<const uint8_t> data) {
    MemoryPackReader reader(data);
    T value{};
    IMemoryPackable<T>::Deserialize(reader, value);
    return value;
}

template<typename T>
T Deserialize(const std::vector<uint8_t>& data) {
    return Deserialize<T>(std::span<const uint8_t>(data));
}

template<typename T>
void Deserialize(const uint8_t* data, size_t size, T& out) {
    MemoryPackReader reader(data, size);
    IMemoryPackable<T>::Deserialize(reader, out);
}

} // namespace memorypack
