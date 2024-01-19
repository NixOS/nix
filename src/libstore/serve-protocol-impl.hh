#pragma once
/**
 * @file
 *
 * Template implementations (as opposed to mere declarations).
 *
 * This file is an exmample of the "impl.hh" pattern. See the
 * contributing guide.
 */

#include "serve-protocol.hh"
#include "length-prefixed-protocol-helper.hh"
#include "store-api.hh"

namespace nix {

/* protocol-agnostic templates */

#define SERVE_USE_LENGTH_PREFIX_SERIALISER(TEMPLATE, T) \
    TEMPLATE T ServeProto::Serialise< T >::read(const StoreDirConfig & store, ServeProto::ReadConn conn) \
    { \
        return LengthPrefixedProtoHelper<ServeProto, T >::read(store, conn); \
    } \
    TEMPLATE void ServeProto::Serialise< T >::write(const StoreDirConfig & store, ServeProto::WriteConn conn, const T & t) \
    { \
        LengthPrefixedProtoHelper<ServeProto, T >::write(store, conn, t); \
    }

SERVE_USE_LENGTH_PREFIX_SERIALISER(template<typename T>, std::vector<T>)
SERVE_USE_LENGTH_PREFIX_SERIALISER(template<typename T>, std::set<T>)
SERVE_USE_LENGTH_PREFIX_SERIALISER(template<typename... Ts>, std::tuple<Ts...>)

#define COMMA_ ,
SERVE_USE_LENGTH_PREFIX_SERIALISER(
    template<typename K COMMA_ typename V>,
    std::map<K COMMA_ V>)
#undef COMMA_

/**
 * Use `CommonProto` where possible.
 */
template<typename T>
struct ServeProto::Serialise
{
    static T read(const StoreDirConfig & store, ServeProto::ReadConn conn)
    {
        return CommonProto::Serialise<T>::read(store,
            CommonProto::ReadConn { .from = conn.from });
    }
    static void write(const StoreDirConfig & store, ServeProto::WriteConn conn, const T & t)
    {
        CommonProto::Serialise<T>::write(store,
            CommonProto::WriteConn { .to = conn.to },
            t);
    }
};

/* protocol-specific templates */

struct ServeProto::BasicClientConnection
{
    FdSink to;
    FdSource from;
    ServeProto::Version remoteVersion;

    /**
     * Establishes connection, negotiating version.
     *
     * @return the version provided by the other side of the
     * connection.
     *
     * @param to Taken by reference to allow for various error handling
     * mechanisms.
     *
     * @param from Taken by reference to allow for various error
     * handling mechanisms.
     *
     * @param localVersion Our version which is sent over
     *
     * @param host Just used to add context to thrown exceptions.
     */
    static ServeProto::Version handshake(
        BufferedSink & to,
        Source & from,
        ServeProto::Version localVersion,
        std::string_view host);

    /**
     * Coercion to `ServeProto::ReadConn`. This makes it easy to use the
     * factored out serve protocol serializers with a
     * `LegacySSHStore::Connection`.
     *
     * The serve protocol connection types are unidirectional, unlike
     * this type.
     */
    operator ServeProto::ReadConn ()
    {
        return ServeProto::ReadConn {
            .from = from,
            .version = remoteVersion,
        };
    }

    /**
     * Coercion to `ServeProto::WriteConn`. This makes it easy to use the
     * factored out serve protocol serializers with a
     * `LegacySSHStore::Connection`.
     *
     * The serve protocol connection types are unidirectional, unlike
     * this type.
     */
    operator ServeProto::WriteConn ()
    {
        return ServeProto::WriteConn {
            .to = to,
            .version = remoteVersion,
        };
    }

    StorePathSet queryValidPaths(
        const Store & remoteStore,
        bool lock, const StorePathSet & paths,
        SubstituteFlag maybeSubstitute);

    /**
     * Just the request half, because Hydra may do other things between
     * issuing the request and reading the `BuildResult` response.
     */
    void putBuildDerivationRequest(
        const Store & store,
        const StorePath & drvPath, const BasicDerivation & drv,
        const ServeProto::BuildOptions & options);
};

struct ServeProto::BasicServerConnection
{
    /**
     * Establishes connection, negotiating version.
     *
     * @return the version provided by the other side of the
     * connection.
     *
     * @param to Taken by reference to allow for various error handling
     * mechanisms.
     *
     * @param from Taken by reference to allow for various error
     * handling mechanisms.
     *
     * @param localVersion Our version which is sent over
     */
    static ServeProto::Version handshake(
        BufferedSink & to,
        Source & from,
        ServeProto::Version localVersion);
};

}
