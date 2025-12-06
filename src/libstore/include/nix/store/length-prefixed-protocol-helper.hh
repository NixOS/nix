#pragma once
/**
 * @file
 *
 * Reusable serialisers for serialization container types in a
 * length-prefixed manner.
 *
 * Used by both the Worker and Serve protocols.
 */

#include "nix/util/types.hh"

namespace nix {

struct StoreDirConfig;

/**
 * Reusable serialisers for serialization container types in a
 * length-prefixed manner.
 *
 * @param T The type of the collection being serialised
 *
 * @param Inner This the most important parameter; this is the "inner"
 * protocol. The user of this will substitute `MyProtocol` or similar
 * when making a `MyProtocol::Serialiser<Collection<T>>`. Note that the
 * inside is allowed to call to call `Inner::Serialiser` on different
 * types. This is especially important for `std::map` which doesn't have
 * a single `T` but one `K` and one `V`.
 */
template<class Inner, typename T>
struct LengthPrefixedProtoHelper;

#define LENGTH_PREFIXED_PROTO_HELPER(Inner, T)                                                          \
    struct LengthPrefixedProtoHelper<Inner, T>                                                          \
    {                                                                                                   \
        static T read(const StoreDirConfig & store, typename Inner::ReadConn conn);                     \
        static void write(const StoreDirConfig & store, typename Inner::WriteConn conn, const T & str); \
    private:                                                                                            \
        /*!                                                                                             \
         * Read this as simply `using S = Inner::Serialise;`.                                           \
         *                                                                                              \
         * It would be nice to use that directly, but C++ doesn't seem to allow                         \
         * it. The `typename` keyword needed to refer to `Inner` seems to greedy                        \
         * (low precedence), and then C++ complains that `Serialise` is not a                           \
         * type parameter but a real type.                                                              \
         *                                                                                              \
         * Making this `S` alias seems to be the only way to avoid these issues.                        \
         */                                                                                             \
        template<typename U>                                                                            \
        using S = typename Inner::template Serialise<U>;                                                \
    }

template<class Inner, typename T>
LENGTH_PREFIXED_PROTO_HELPER(Inner, std::vector<T>);

#define COMMA_ ,
template<class Inner, typename T, typename Compare>
LENGTH_PREFIXED_PROTO_HELPER(Inner, std::set<T COMMA_ Compare>);

template<class Inner, typename... Ts>
LENGTH_PREFIXED_PROTO_HELPER(Inner, std::tuple<Ts...>);

template<class Inner, typename K, typename V, typename Compare>
#define LENGTH_PREFIXED_PROTO_HELPER_X std::map<K, V, Compare>
LENGTH_PREFIXED_PROTO_HELPER(Inner, LENGTH_PREFIXED_PROTO_HELPER_X);
#undef COMMA_

template<class Inner, typename T>
std::vector<T>
LengthPrefixedProtoHelper<Inner, std::vector<T>>::read(const StoreDirConfig & store, typename Inner::ReadConn conn)
{
    std::vector<T> resSet;
    auto size = readNum<size_t>(conn.from);
    while (size--) {
        resSet.push_back(S<T>::read(store, conn));
    }
    return resSet;
}

template<class Inner, typename T>
void LengthPrefixedProtoHelper<Inner, std::vector<T>>::write(
    const StoreDirConfig & store, typename Inner::WriteConn conn, const std::vector<T> & resSet)
{
    conn.to << resSet.size();
    for (auto & key : resSet) {
        S<T>::write(store, conn, key);
    }
}

template<class Inner, typename T, typename Compare>
std::set<T, Compare> LengthPrefixedProtoHelper<Inner, std::set<T, Compare>>::read(
    const StoreDirConfig & store, typename Inner::ReadConn conn)
{
    std::set<T, Compare> resSet;
    auto size = readNum<size_t>(conn.from);
    while (size--) {
        resSet.insert(S<T>::read(store, conn));
    }
    return resSet;
}

template<class Inner, typename T, typename Compare>
void LengthPrefixedProtoHelper<Inner, std::set<T, Compare>>::write(
    const StoreDirConfig & store, typename Inner::WriteConn conn, const std::set<T, Compare> & resSet)
{
    conn.to << resSet.size();
    for (auto & key : resSet) {
        S<T>::write(store, conn, key);
    }
}

template<class Inner, typename K, typename V, typename Compare>
std::map<K, V, Compare> LengthPrefixedProtoHelper<Inner, std::map<K, V, Compare>>::read(
    const StoreDirConfig & store, typename Inner::ReadConn conn)
{
    std::map<K, V, Compare> resMap;
    auto size = readNum<size_t>(conn.from);
    while (size--) {
        auto k = S<K>::read(store, conn);
        auto v = S<V>::read(store, conn);
        resMap.insert_or_assign(std::move(k), std::move(v));
    }
    return resMap;
}

template<class Inner, typename K, typename V, typename Compare>
void LengthPrefixedProtoHelper<Inner, std::map<K, V, Compare>>::write(
    const StoreDirConfig & store, typename Inner::WriteConn conn, const std::map<K, V, Compare> & resMap)
{
    conn.to << resMap.size();
    for (auto & i : resMap) {
        S<K>::write(store, conn, i.first);
        S<V>::write(store, conn, i.second);
    }
}

template<class Inner, typename... Ts>
std::tuple<Ts...>
LengthPrefixedProtoHelper<Inner, std::tuple<Ts...>>::read(const StoreDirConfig & store, typename Inner::ReadConn conn)
{
    return std::tuple<Ts...>{
        S<Ts>::read(store, conn)...,
    };
}

template<class Inner, typename... Ts>
void LengthPrefixedProtoHelper<Inner, std::tuple<Ts...>>::write(
    const StoreDirConfig & store, typename Inner::WriteConn conn, const std::tuple<Ts...> & res)
{
    std::apply([&]<typename... Us>(const Us &... args) { (S<Us>::write(store, conn, args), ...); }, res);
}

} // namespace nix
