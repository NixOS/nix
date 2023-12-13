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
#include "length-prefixed-protocol-helper.hh"

namespace nix {

/* protocol-agnostic templates */

#define WORKER_USE_LENGTH_PREFIX_SERIALISER(TEMPLATE, T) \
    TEMPLATE T WorkerProto::Serialise< T >::read(const StoreDirConfig & store, WorkerProto::ReadConn conn) \
    { \
        return LengthPrefixedProtoHelper<WorkerProto, T >::read(store, conn); \
    } \
    TEMPLATE void WorkerProto::Serialise< T >::write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const T & t) \
    { \
        LengthPrefixedProtoHelper<WorkerProto, T >::write(store, conn, t); \
    }

WORKER_USE_LENGTH_PREFIX_SERIALISER(template<typename T>, std::vector<T>)
WORKER_USE_LENGTH_PREFIX_SERIALISER(template<typename T>, std::set<T>)
WORKER_USE_LENGTH_PREFIX_SERIALISER(template<typename... Ts>, std::tuple<Ts...>)

#define COMMA_ ,
WORKER_USE_LENGTH_PREFIX_SERIALISER(
    template<typename K COMMA_ typename V>,
    std::map<K COMMA_ V>)
#undef COMMA_

/**
 * Use `CommonProto` where possible.
 */
template<typename T>
struct WorkerProto::Serialise
{
    static T read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
    {
        return CommonProto::Serialise<T>::read(store,
            CommonProto::ReadConn { .from = conn.from });
    }
    static void write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const T & t)
    {
        CommonProto::Serialise<T>::write(store,
            CommonProto::WriteConn { .to = conn.to },
            t);
    }
};

/* protocol-specific templates */

template<typename T>
AccessStatusFor<T> WorkerProto::Serialise<AccessStatusFor<T>>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn) {
    AccessStatusFor<T> status;
    conn.from >> status.isProtected;
    status.entities = WorkerProto::Serialise<std::set<T>>::read(store, conn);
    return status;
}

template<typename T>
void WorkerProto::Serialise<AccessStatusFor<T>>::write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const AccessStatusFor<T> & status)
{
    conn.to << status.isProtected;
    WorkerProto::Serialise<std::set<T>>::write(store, conn, status.entities);
}

template<typename A, typename B>
std::variant<A, B> WorkerProto::Serialise<std::variant<A, B>>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
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
void WorkerProto::Serialise<std::variant<A, B>>::write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const std::variant<A, B> & resVariant)
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
std::variant<A, B, C> WorkerProto::Serialise<std::variant<A, B, C>>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
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
void WorkerProto::Serialise<std::variant<A, B, C>>::write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const std::variant<A, B, C> & resVariant)
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

}
