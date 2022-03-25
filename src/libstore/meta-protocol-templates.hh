#pragma once

#include "types.hh"

/* Shared code between worker and serv protocols, injected into proper header.
 */
namespace nix {

struct Store;

namespace meta_protocol {

#define WRAP_META_PROTO(TEMPLATE, T) \
    TEMPLATE \
    T read(const Store & store, ReadConn conn, Phantom< T > p) { \
       return meta_protocol::read(store, conn, p); \
    } \
    \
    TEMPLATE \
    void write(const Store & store, WriteConn conn, const T & v) { \
       meta_protocol::write(store, conn, v); \
    }

template<typename ReadConn, typename T>
std::vector<T> read(const Store & store, ReadConn conn, Phantom<std::vector<T>> _)
{
    std::vector<T> resSet;
    auto size = readNum<size_t>(conn.from);
    while (size--) {
        resSet.push_back(read(store, conn, Phantom<T> {}));
    }
    return resSet;
}

template<typename WriteConn, typename T>
void write(const Store & store, WriteConn conn, const std::vector<T> & resSet)
{
    conn.to << resSet.size();
    for (auto & key : resSet) {
        write(store, conn, key);
    }
}

template<typename ReadConn, typename T>
std::set<T> read(const Store & store, ReadConn conn, Phantom<std::set<T>> _)
{
    std::set<T> resSet;
    auto size = readNum<size_t>(conn.from);
    while (size--) {
        resSet.insert(read(store, conn, Phantom<T> {}));
    }
    return resSet;
}

template<typename WriteConn, typename T>
void write(const Store & store, WriteConn conn, const std::set<T> & resSet)
{
    conn.to << resSet.size();
    for (auto & key : resSet) {
        write(store, conn, key);
    }
}

template<typename ReadConn, typename K, typename V>
std::map<K, V> read(const Store & store, ReadConn conn, Phantom<std::map<K, V>> _)
{
    std::map<K, V> resMap;
    auto size = readNum<size_t>(conn.from);
    while (size--) {
        auto k = read(store, conn, Phantom<K> {});
        auto v = read(store, conn, Phantom<V> {});
        resMap.insert_or_assign(std::move(k), std::move(v));
    }
    return resMap;
}

template<typename WriteConn, typename K, typename V>
void write(const Store & store, WriteConn conn, const std::map<K, V> & resMap)
{
    conn.to << resMap.size();
    for (auto & i : resMap) {
        write(store, conn, i.first);
        write(store, conn, i.second);
    }
}

}

}
