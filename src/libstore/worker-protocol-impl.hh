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
#include "granular-access-store.hh"

namespace nix {

template<typename T>
AccessStatusFor<T> WorkerProto::Serialise<AccessStatusFor<T>>::read(const Store & store, WorkerProto::ReadConn conn) {
    AccessStatusFor<T> status;
    conn.from >> status.isProtected;
    status.entities = WorkerProto::Serialise<std::set<T>>::read(store, conn);
    return status;
}

template<typename T>
void WorkerProto::Serialise<AccessStatusFor<T>>::write(const Store & store, WorkerProto::WriteConn conn, const AccessStatusFor<T> & status)
{
    conn.to << status.isProtected;
    WorkerProto::Serialise<std::set<T>>::write(store, conn, status.entities);
}

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

template<typename A, typename B>
std::variant<A, B> WorkerProto::Serialise<std::variant<A, B>>::read(const Store & store, WorkerProto::ReadConn conn)
{
    size_t index;
    conn.from >> index;
    switch (index) {
        case 0:
            return WorkerProto::Serialise<A>::read(store, conn);
            break;
        case 1:
            return WorkerProto::Serialise<B>::read(store, conn);
            break;
        default:
            throw Error("Invalid variant index from remote");
    }
}

template<typename A, typename B>
void WorkerProto::Serialise<std::variant<A, B>>::write(const Store & store, WorkerProto::WriteConn conn, const std::variant<A, B> & resVariant)
{
        size_t index = resVariant.index();
        conn.to << index;
        switch (index) {
            case 0:
                WorkerProto::Serialise<A>::write(store, conn, std::get<0>(resVariant));
                break;
            case 1:
                WorkerProto::Serialise<B>::write(store, conn, std::get<1>(resVariant));
                break;
            default:
                throw Error("Invalid variant index");
        }
}
template<typename A, typename B, typename C>
std::variant<A, B, C> WorkerProto::Serialise<std::variant<A, B, C>>::read(const Store & store, WorkerProto::ReadConn conn)
{
    size_t index;
    conn.from >> index;
    switch (index) {
        case 0:
            return WorkerProto::Serialise<A>::read(store, conn);
        case 1:
            return WorkerProto::Serialise<B>::read(store, conn);
        case 2:
            return WorkerProto::Serialise<C>::read(store, conn);
        default:
            throw Error("Invalid variant index from remote");
    }
}

template<typename A, typename B, typename C>
void WorkerProto::Serialise<std::variant<A, B, C>>::write(const Store & store, WorkerProto::WriteConn conn, const std::variant<A, B, C> & resVariant)
{
        size_t index = resVariant.index();
        conn.to << index;
        switch (index) {
            case 0:
                WorkerProto::Serialise<A>::write(store, conn, std::get<0>(resVariant));
                break;
            case 1:
                WorkerProto::Serialise<B>::write(store, conn, std::get<1>(resVariant));
                break;
            case 2:
                WorkerProto::Serialise<C>::write(store, conn, std::get<2>(resVariant));
                break;
            default:
                throw Error("Invalid variant index");
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

template<typename A, typename B>
std::pair<A, B> WorkerProto::Serialise<std::pair<A, B>>::read(const Store & store, WorkerProto::ReadConn conn)
{
    auto a = WorkerProto::Serialise<A>::read(store, conn);
    auto b = WorkerProto::Serialise<B>::read(store, conn);
    return {a, b};
}

template<typename A, typename B>
void WorkerProto::Serialise<std::pair<A, B>>::write(const Store & store, WorkerProto::WriteConn conn, const std::pair<A, B> & p)
{
    WorkerProto::Serialise<A>::write(store, conn, p.first);
    WorkerProto::Serialise<B>::write(store, conn, p.second);
}

}
