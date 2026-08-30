#pragma once
/// @file containers.hpp
/// @brief Wire-format mappings for the standard library container types,
///        std::optional / smart pointers, and std::variant as a MemoryPack union.

#include "memorypack/core.hpp"

#include <deque>
#include <list>
#include <memory>
#include <set>
#include <unordered_set>
#include <variant>

namespace memorypack {

// -- Null encodings for container types -----------------------------------------
// C# spells "null" differently per kind of type; std::optional<T> has to follow
// whichever encoding T uses.

template<typename T, typename A>
struct WireNullEncoding<std::vector<T, A>> {
    static constexpr NullEncoding value = NullEncoding::Collection;
};
template<typename T, typename A>
struct WireNullEncoding<std::deque<T, A>> {
    static constexpr NullEncoding value = NullEncoding::Collection;
};
template<typename T, typename A>
struct WireNullEncoding<std::list<T, A>> {
    static constexpr NullEncoding value = NullEncoding::Collection;
};
template<typename K, typename V, typename C, typename A>
struct WireNullEncoding<std::map<K, V, C, A>> {
    static constexpr NullEncoding value = NullEncoding::Collection;
};
template<typename K, typename V, typename H, typename E, typename A>
struct WireNullEncoding<std::unordered_map<K, V, H, E, A>> {
    static constexpr NullEncoding value = NullEncoding::Collection;
};
template<typename T, typename C, typename A>
struct WireNullEncoding<std::set<T, C, A>> {
    static constexpr NullEncoding value = NullEncoding::Collection;
};
template<typename T, typename H, typename E, typename A>
struct WireNullEncoding<std::unordered_set<T, H, E, A>> {
    static constexpr NullEncoding value = NullEncoding::Collection;
};

// -- std::vector<T> -> C# List<T> / T[] -----------------------------------------

template<typename T, typename A>
struct MemoryPackFormatter<std::vector<T, A>> {
    static void Serialize(MemoryPackWriter& w, const std::vector<T, A>& v) {
        w.WriteCollection(v);
    }
    static void Deserialize(MemoryPackReader& r, std::vector<T, A>& v) {
        if constexpr (std::is_same_v<A, std::allocator<T>>) {
            r.ReadCollection(v);
        } else {
            v.clear();
            std::vector<T> tmp;
            r.ReadCollection(tmp);
            v.assign(std::make_move_iterator(tmp.begin()), std::make_move_iterator(tmp.end()));
        }
    }
};

// -- std::array<T, N> -> fixed-size C# collection -------------------------------

template<typename T, size_t N>
struct MemoryPackFormatter<std::array<T, N>> {
    static void Serialize(MemoryPackWriter& w, const std::array<T, N>& v) {
        w.WriteCollection(std::span<const T>(v.data(), N));
    }
    static void Deserialize(MemoryPackReader& r, std::array<T, N>& v) {
        v = {};
        if constexpr (std::is_arithmetic_v<T>) {
            r.ReadArray(v.data(), static_cast<int32_t>(N));
        } else {
            int32_t len = r.ReadCollectionLength(1);
            if (len <= 0) return;
            auto count = static_cast<size_t>(len);
            for (size_t i = 0; i < count && !r.Failed(); ++i) {
                T tmp{};
                r.Read(tmp);
                if (i < N) v[i] = std::move(tmp);
            }
        }
    }
};

// -- Sequence containers without contiguous storage -----------------------------

namespace detail {

template<typename Container>
struct SequenceFormatter {
    using T = typename Container::value_type;

    static void Serialize(MemoryPackWriter& w, const Container& c) {
        const int32_t n = w.CheckedLength(c.size());
        if (n < 0) return;
        w.WriteCollectionHeader(n);
        for (const auto& item : c) w.Write(item);
    }

    static void Deserialize(MemoryPackReader& r, Container& c) {
        c.clear();
        int32_t len = r.ReadCollectionLength(
            (std::is_arithmetic_v<T> || std::is_enum_v<T>) ? sizeof(T) : 1);
        if (len <= 0) return;
        for (int32_t i = 0; i < len && !r.Failed(); ++i) {
            T value{};
            r.Read(value);
            c.insert(c.end(), std::move(value));
        }
    }
};

template<typename Container>
struct SetFormatter {
    using T = typename Container::value_type;

    static void Serialize(MemoryPackWriter& w, const Container& c) {
        const int32_t n = w.CheckedLength(c.size());
        if (n < 0) return;
        w.WriteCollectionHeader(n);
        for (const auto& item : c) w.Write(item);
    }

    static void Deserialize(MemoryPackReader& r, Container& c) {
        c.clear();
        int32_t len = r.ReadCollectionLength(
            (std::is_arithmetic_v<T> || std::is_enum_v<T>) ? sizeof(T) : 1);
        if (len <= 0) return;
        for (int32_t i = 0; i < len && !r.Failed(); ++i) {
            T value{};
            r.Read(value);
            c.insert(std::move(value));
        }
    }
};

template<typename Map>
struct MapFormatter {
    using K = typename Map::key_type;
    using V = typename Map::mapped_type;

    static void Serialize(MemoryPackWriter& w, const Map& m) {
        const int32_t n = w.CheckedLength(m.size());
        if (n < 0) return;
        w.WriteCollectionHeader(n);
        for (const auto& [key, value] : m) {
            w.Write(key);
            w.Write(value);
        }
    }

    static void Deserialize(MemoryPackReader& r, Map& m) {
        m.clear();
        constexpr size_t minK = (std::is_arithmetic_v<K> || std::is_enum_v<K>) ? sizeof(K) : 1;
        constexpr size_t minV = (std::is_arithmetic_v<V> || std::is_enum_v<V>) ? sizeof(V) : 1;
        int32_t len = r.ReadCollectionLength(minK + minV);
        if (len <= 0) return;
        for (int32_t i = 0; i < len && !r.Failed(); ++i) {
            K key{};   r.Read(key);
            V value{}; r.Read(value);
            m.emplace(std::move(key), std::move(value));
        }
    }
};

} // namespace detail

template<typename T, typename A>
struct MemoryPackFormatter<std::deque<T, A>> : detail::SequenceFormatter<std::deque<T, A>> {};

template<typename T, typename A>
struct MemoryPackFormatter<std::list<T, A>> : detail::SequenceFormatter<std::list<T, A>> {};

template<typename T, typename C, typename A>
struct MemoryPackFormatter<std::set<T, C, A>> : detail::SetFormatter<std::set<T, C, A>> {};

template<typename T, typename H, typename E, typename A>
struct MemoryPackFormatter<std::unordered_set<T, H, E, A>>
    : detail::SetFormatter<std::unordered_set<T, H, E, A>> {};

template<typename K, typename V, typename C, typename A>
struct MemoryPackFormatter<std::map<K, V, C, A>> : detail::MapFormatter<std::map<K, V, C, A>> {};

template<typename K, typename V, typename H, typename E, typename A>
struct MemoryPackFormatter<std::unordered_map<K, V, H, E, A>>
    : detail::MapFormatter<std::unordered_map<K, V, H, E, A>> {};

// -- std::pair -> C# KeyValuePair<K, V> -----------------------------------------
// KeyValuePair is written as the key immediately followed by the value, with no
// object header of its own (verified against MemoryPack output).

template<typename K, typename V>
struct MemoryPackFormatter<std::pair<K, V>> {
    static void Serialize(MemoryPackWriter& w, const std::pair<K, V>& v) {
        w.Write(v.first);
        w.Write(v.second);
    }
    static void Deserialize(MemoryPackReader& r, std::pair<K, V>& v) {
        r.Read(v.first);
        r.Read(v.second);
    }
};

// -- std::tuple -> C# Tuple<...> ------------------------------------------------
// A C# reference Tuple<> carries an object header. An unmanaged ValueTuple is
// copied verbatim instead, so map those with MEMORYPACK_UNMANAGED.

template<typename... Ts>
struct MemoryPackFormatter<std::tuple<Ts...>> {
    static void Serialize(MemoryPackWriter& w, const std::tuple<Ts...>& v) { w.WriteTuple(v); }
    static void Deserialize(MemoryPackReader& r, std::tuple<Ts...>& v) {
        v = r.ReadTuple<Ts...>();
    }
};

// -- std::optional<T> -> nullable C# member -------------------------------------

template<typename T>
struct MemoryPackFormatter<std::optional<T>> {
    static constexpr NullEncoding kind = WireNullEncoding<T>::value;

    static void Serialize(MemoryPackWriter& w, const std::optional<T>& v) {
        if constexpr (kind == NullEncoding::NullableValue) {
            w.WriteNullable(v);
        } else if constexpr (kind == NullEncoding::String) {
            if (v) w.Write(*v); else w.WriteNullString();
        } else if constexpr (kind == NullEncoding::Collection) {
            if (v) w.Write(*v); else w.WriteNullCollectionHeader();
        } else {
            if (v) w.Write(*v); else w.WriteNullObjectHeader();
        }
    }

    static void Deserialize(MemoryPackReader& r, std::optional<T>& v) {
        if constexpr (kind == NullEncoding::NullableValue) {
            v = r.ReadNullable<T>();
        } else if constexpr (kind == NullEncoding::String) {
            auto s = r.ReadString();
            if (s) v = T(std::move(*s)); else v.reset();
        } else if constexpr (kind == NullEncoding::Collection) {
            // A null collection and an empty one differ only in the header value.
            if (r.Remaining() >= sizeof(int32_t)) {
                MemoryPackReader peek = r;
                if (peek.ReadCollectionHeader() == NULL_COLLECTION) {
                    r.Advance(sizeof(int32_t));
                    v.reset();
                    return;
                }
            }
            T value{};
            r.Read(value);
            v = std::move(value);
        } else {
            if (r.PeekIsNull()) { r.ReadByte(); v.reset(); return; }
            T value{};
            r.Read(value);
            v = std::move(value);
        }
    }
};

// UTF-16 strings need the string encoding, not the generic object one.
template<>
struct MemoryPackFormatter<std::optional<std::u16string>> {
    static void Serialize(MemoryPackWriter& w, const std::optional<std::u16string>& v) {
        if (v) w.WriteStringUtf16(*v); else w.WriteNullString();
    }
    static void Deserialize(MemoryPackReader& r, std::optional<std::u16string>& v) {
        if (r.Remaining() >= sizeof(int32_t)) {
            MemoryPackReader peek = r;
            if (peek.ReadCollectionHeader() == NULL_COLLECTION) {
                r.Advance(sizeof(int32_t));
                v.reset();
                return;
            }
        }
        std::u16string value;
        r.Read(value);
        v = std::move(value);
    }
};

// -- Smart pointers -> nullable C# reference ------------------------------------

template<typename T, typename D>
struct MemoryPackFormatter<std::unique_ptr<T, D>> {
    static void Serialize(MemoryPackWriter& w, const std::unique_ptr<T, D>& v) {
        if (v) w.Write(*v); else w.WriteNullObjectHeader();
    }
    static void Deserialize(MemoryPackReader& r, std::unique_ptr<T, D>& v) {
        if (r.PeekIsNull()) { r.ReadByte(); v.reset(); return; }
        auto value = std::make_unique<T>();
        r.Read(*value);
        v = std::unique_ptr<T, D>(value.release());
    }
};

template<typename T>
struct MemoryPackFormatter<std::shared_ptr<T>> {
    static void Serialize(MemoryPackWriter& w, const std::shared_ptr<T>& v) {
        if (v) w.Write(*v); else w.WriteNullObjectHeader();
    }
    static void Deserialize(MemoryPackReader& r, std::shared_ptr<T>& v) {
        if (r.PeekIsNull()) { r.ReadByte(); v.reset(); return; }
        auto value = std::make_shared<T>();
        r.Read(*value);
        v = std::move(value);
    }
};

// -- std::variant -> C# [MemoryPackUnion] ---------------------------------------
//
// Wire format: `[1B tag]` for tag < 250, `[1B 250][2B tag]` otherwise, followed
// by the concrete type serialized with its own object header. A null union is
// `[1B 255]`; model a nullable union as std::optional<std::variant<...>>.
//
// Declare the tag of each alternative with MEMORYPACK_UNION_TAG.

namespace detail {

template<typename Variant, size_t... Is>
bool ReadUnionAlternative(MemoryPackReader& r, Variant& v, uint16_t tag, std::index_sequence<Is...>) {
    bool handled = false;
    auto tryOne = [&]<size_t I>() {
        if (handled) return;
        using MemoryPackAlt = std::variant_alternative_t<I, Variant>;
        if (MemoryPackUnionTag<MemoryPackAlt>::value != tag) return;
        v.template emplace<I>();
        r.Read(std::get<I>(v));
        handled = true;
    };
    (tryOne.template operator()<Is>(), ...);
    return handled;
}

} // namespace detail

template<typename... Ts>
struct MemoryPackFormatter<std::variant<Ts...>> {
    using V = std::variant<Ts...>;

    static void Serialize(MemoryPackWriter& w, const V& v) {
        std::visit([&w](const auto& alt) {
            using MemoryPackAlt = std::decay_t<decltype(alt)>;
            w.WriteUnionHeader(MemoryPackUnionTag<MemoryPackAlt>::value);
            w.Write(alt);
        }, v);
    }

    static void Deserialize(MemoryPackReader& r, V& v) {
        auto tag = r.ReadUnionHeader();
        if (!tag) {
            // A bare variant cannot hold null; use std::optional<std::variant<...>>.
            v = V{};
            return;
        }
        if (!detail::ReadUnionAlternative(r, v, *tag, std::index_sequence_for<Ts...>{})) {
            // Unknown tag: the remaining bytes cannot be interpreted safely.
            r.Advance(r.Remaining());
        }
    }
};

// std::optional<std::variant<...>> is the nullable union: `[1B 255]` for null.
template<typename... Ts>
struct WireNullEncoding<std::variant<Ts...>> {
    static constexpr NullEncoding value = NullEncoding::Object;
};

} // namespace memorypack

/// Declares the C# [MemoryPackUnion] tag of one union alternative.
///
///     MEMORYPACK_UNION_TAG(CircleShape, 0);
///     MEMORYPACK_UNION_TAG(RectShape,   1);
///     using Shape = std::variant<CircleShape, RectShape>;
#define MEMORYPACK_UNION_TAG(Type, Tag)                                       \
    namespace memorypack {                                                    \
    template<> struct MemoryPackUnionTag<Type> {                              \
        static constexpr uint16_t value = static_cast<uint16_t>(Tag);         \
    };                                                                        \
    }
