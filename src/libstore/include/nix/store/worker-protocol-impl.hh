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

/* Wire format: a single length-prefixed JSON string. Decoding goes
   through the free `to_json`/`from_json` overloads the type's JSON
   header defines (typically via
   `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT`), picked up by
   nlohmann via ADL. Invoke in a TU that pulls in
   `<nlohmann/json.hpp>` and the type's JSON serializer header. The
   `store` parameter (required by the `WorkerProto::Serialise`
   interface) is unused for JSON-encoded types — translation between
   value and JSON does not need the `StoreDirConfig`. */
#define WORKER_USE_JSON_SERIALISER(T)                                                            \
    T WorkerProto::Serialise<T>::read(                                                           \
        [[maybe_unused]] const StoreDirConfig & store, WorkerProto::ReadConn conn)               \
    {                                                                                            \
        return nlohmann::json::parse(readString(conn.from)).get<T>();                            \
    }                                                                                            \
    void WorkerProto::Serialise<T>::write(                                                       \
        [[maybe_unused]] const StoreDirConfig & store,                                           \
        WorkerProto::WriteConn conn,                                                             \
        const T & t)                                                                             \
    {                                                                                            \
        conn.to << nlohmann::json(t).dump();                                                     \
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
