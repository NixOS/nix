#pragma once
/**
 * @file
 *
 * Template implementations (as opposed to mere declarations).
 *
 * This file is an example of the "impl.hh" pattern. See the
 * contributing guide.
 */

#include "nix/store/worker-protocol.hh"
#include "nix/store/length-prefixed-protocol-helper.hh"

namespace nix {

/* protocol-agnostic templates */

#define WORKER_USE_LENGTH_PREFIX_SERIALISER(TEMPLATE, T)                                                 \
    TEMPLATE T WorkerProto::Serialise<T>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn) \
    {                                                                                                    \
        return LengthPrefixedProtoHelper<WorkerProto, T>::read(store, conn);                             \
    }                                                                                                    \
    TEMPLATE void WorkerProto::Serialise<T>::write(                                                      \
        const StoreDirConfig & store, WorkerProto::WriteConn conn, const T & t)                          \
    {                                                                                                    \
        LengthPrefixedProtoHelper<WorkerProto, T>::write(store, conn, t);                                \
    }

WORKER_USE_LENGTH_PREFIX_SERIALISER(template<typename T>, std::vector<T>)
#define COMMA_ ,
WORKER_USE_LENGTH_PREFIX_SERIALISER(template<typename T COMMA_ typename Compare>, std::set<T COMMA_ Compare>)
#undef COMMA_
WORKER_USE_LENGTH_PREFIX_SERIALISER(template<typename... Ts>, std::tuple<Ts...>)

#define WORKER_USE_LENGTH_PREFIX_SERIALISER_COMMA ,
WORKER_USE_LENGTH_PREFIX_SERIALISER(
    template<typename K WORKER_USE_LENGTH_PREFIX_SERIALISER_COMMA typename V WORKER_USE_LENGTH_PREFIX_SERIALISER_COMMA
             typename Compare>
    ,
    std::map<K WORKER_USE_LENGTH_PREFIX_SERIALISER_COMMA V WORKER_USE_LENGTH_PREFIX_SERIALISER_COMMA Compare>)

/**
 * Use `CommonProto` where possible.
 */
template<typename T>
struct WorkerProto::Serialise
{
    static T read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
    {
        return CommonProto::Serialise<T>::read(store, CommonProto::ReadConn{.from = conn.from});
    }

    static void write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const T & t)
    {
        CommonProto::Serialise<T>::write(store, CommonProto::WriteConn{.to = conn.to}, t);
    }
};

/* protocol-specific templates */

} // namespace nix
