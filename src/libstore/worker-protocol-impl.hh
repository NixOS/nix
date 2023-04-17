#pragma once
/**
 * @file
 *
 * Template implementations (as opposed to mere declarations).
 *
 * This file is an exmample of the "impl.hh" pattern. See the
 * contributing guide.
 */

#include "worker-protocol.hh"

namespace nix {

template<typename T>
std::vector<T> WorkerProto::Serialise<std::vector<T>>::read(const Store & store, WorkerProto::ReadConn conn)
{
    std::vector<T> resSet;
    auto size = readNum<size_t>(conn.from);
    while (size--) {
        resSet.push_back(WorkerProto::Serialise<T>::read(store, conn));
    }
    return resSet;
}

template<typename T>
void WorkerProto::Serialise<std::vector<T>>::write(const Store & store, WorkerProto::WriteConn conn, const std::vector<T> & resSet)
{
    conn.to << resSet.size();
    for (auto & key : resSet) {
        WorkerProto::Serialise<T>::write(store, conn, key);
    }
}

template<typename T>
std::set<T> WorkerProto::Serialise<std::set<T>>::read(const Store & store, WorkerProto::ReadConn conn)
{
    std::set<T> resSet;
    auto size = readNum<size_t>(conn.from);
    while (size--) {
        resSet.insert(WorkerProto::Serialise<T>::read(store, conn));
    }
    return resSet;
}

template<typename T>
void WorkerProto::Serialise<std::set<T>>::write(const Store & store, WorkerProto::WriteConn conn, const std::set<T> & resSet)
{
    conn.to << resSet.size();
    for (auto & key : resSet) {
        WorkerProto::Serialise<T>::write(store, conn, key);
    }
}

template<typename K, typename V>
std::map<K, V> WorkerProto::Serialise<std::map<K, V>>::read(const Store & store, WorkerProto::ReadConn conn)
{
    std::map<K, V> resMap;
    auto size = readNum<size_t>(conn.from);
    while (size--) {
        auto k = WorkerProto::Serialise<K>::read(store, conn);
        auto v = WorkerProto::Serialise<V>::read(store, conn);
        resMap.insert_or_assign(std::move(k), std::move(v));
    }
    return resMap;
}

template<typename K, typename V>
void WorkerProto::Serialise<std::map<K, V>>::write(const Store & store, WorkerProto::WriteConn conn, const std::map<K, V> & resMap)
{
    conn.to << resMap.size();
    for (auto & i : resMap) {
        WorkerProto::Serialise<K>::write(store, conn, i.first);
        WorkerProto::Serialise<V>::write(store, conn, i.second);
    }
}

}
