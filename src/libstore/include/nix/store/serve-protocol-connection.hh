#pragma once
///@file

#include "nix/store/serve-protocol.hh"
#include "nix/store/store-api.hh"

namespace nix {

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
    static ServeProto::Version
    handshake(BufferedSink & to, Source & from, ServeProto::Version localVersion, std::string_view host);

    /**
     * Coercion to `ServeProto::ReadConn`. This makes it easy to use the
     * factored out serve protocol serializers with a
     * `LegacySSHStore::Connection`.
     *
     * The serve protocol connection types are unidirectional, unlike
     * this type.
     */
    operator ServeProto::ReadConn()
    {
        return ServeProto::ReadConn{
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
    operator ServeProto::WriteConn()
    {
        return ServeProto::WriteConn{
            .to = to,
            .version = remoteVersion,
        };
    }

    StorePathSet queryValidPaths(
        const StoreDirConfig & remoteStore, bool lock, const StorePathSet & paths, SubstituteFlag maybeSubstitute);

    std::map<StorePath, UnkeyedValidPathInfo> queryPathInfos(const StoreDirConfig & store, const StorePathSet & paths);
    ;

    void putBuildDerivationRequest(
        const StoreDirConfig & store,
        const StorePath & drvPath,
        const BasicDerivation & drv,
        const ServeProto::BuildOptions & options);

    /**
     * Get the response, must be paired with
     * `putBuildDerivationRequest`.
     */
    BuildResult getBuildDerivationResponse(const StoreDirConfig & store);

    void narFromPath(const StoreDirConfig & store, const StorePath & path, fun<void(Source &)> receiveNar);

    void importPaths(const StoreDirConfig & store, fun<void(Sink &)> sendPaths);
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
    static ServeProto::Version handshake(BufferedSink & to, Source & from, ServeProto::Version localVersion);
};

} // namespace nix
