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
std::vector<T> WorkerProto<std::vector<T>>::read(const Store & store, Source & from)
{
    std::vector<T> resSet;
    auto size = readNum<size_t>(from);
    while (size--) {
        resSet.push_back(WorkerProto<T>::read(store, from));
    }
    return resSet;
}

template<typename T>
void WorkerProto<std::vector<T>>::write(const Store & store, Sink & out, const std::vector<T> & resSet)
{
    out << resSet.size();
    for (auto & key : resSet) {
        WorkerProto<T>::write(store, out, key);
    }
}

template<typename T>
std::set<T> WorkerProto<std::set<T>>::read(const Store & store, Source & from)
{
    std::set<T> resSet;
    auto size = readNum<size_t>(from);
    while (size--) {
        resSet.insert(WorkerProto<T>::read(store, from));
    }
    return resSet;
}

template<typename T>
void WorkerProto<std::set<T>>::write(const Store & store, Sink & out, const std::set<T> & resSet)
{
    out << resSet.size();
    for (auto & key : resSet) {
        WorkerProto<T>::write(store, out, key);
    }
}

template<typename K, typename V>
std::map<K, V> WorkerProto<std::map<K, V>>::read(const Store & store, Source & from)
{
    std::map<K, V> resMap;
    auto size = readNum<size_t>(from);
    while (size--) {
        auto k = WorkerProto<K>::read(store, from);
        auto v = WorkerProto<V>::read(store, from);
        resMap.insert_or_assign(std::move(k), std::move(v));
    }
    return resMap;
}

template<typename K, typename V>
void WorkerProto<std::map<K, V>>::write(const Store & store, Sink & out, const std::map<K, V> & resMap)
{
    out << resMap.size();
    for (auto & i : resMap) {
        WorkerProto<K>::write(store, out, i.first);
        WorkerProto<V>::write(store, out, i.second);
    }
}

}
