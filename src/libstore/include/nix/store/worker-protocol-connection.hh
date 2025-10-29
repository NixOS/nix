#pragma once
///@file

#include "nix/store/worker-protocol.hh"
#include "nix/store/store-api.hh"

namespace nix {

struct WorkerProto::BasicConnection
{
    /**
     * Send with this.
     */
    FdSink to;

    /**
     * Receive with this.
     */
    FdSource from;

    /**
     * The protocol version agreed by both sides.
     */
    WorkerProto::Version protoVersion;

    /**
     * The set of features that both sides support.
     */
    FeatureSet features;

    /**
     * Coercion to `WorkerProto::ReadConn`. This makes it easy to use the
     * factored out serve protocol serializers with a
     * `LegacySSHStore::Connection`.
     *
     * The serve protocol connection types are unidirectional, unlike
     * this type.
     */
    operator WorkerProto::ReadConn()
    {
        return WorkerProto::ReadConn{
            .from = from,
            .version = protoVersion,
        };
    }

    /**
     * Coercion to `WorkerProto::WriteConn`. This makes it easy to use the
     * factored out serve protocol serializers with a
     * `LegacySSHStore::Connection`.
     *
     * The serve protocol connection types are unidirectional, unlike
     * this type.
     */
    operator WorkerProto::WriteConn()
    {
        return WorkerProto::WriteConn{
            .to = to,
            .version = protoVersion,
        };
    }
};

struct WorkerProto::BasicClientConnection : WorkerProto::BasicConnection
{
    /**
     * Flush to direction
     */
    virtual ~BasicClientConnection();

    virtual void closeWrite() = 0;

    std::exception_ptr processStderrReturn(Sink * sink = 0, Source * source = 0, bool flush = true, bool block = true);

    void
    processStderr(bool * daemonException, Sink * sink = 0, Source * source = 0, bool flush = true, bool block = true);

    /**
     * Establishes connection, negotiating version.
     *
     * @return The minimum version supported by both sides and the set
     * of protocol features supported by both sides.
     *
     * @param to Taken by reference to allow for various error handling
     * mechanisms.
     *
     * @param from Taken by reference to allow for various error
     * handling mechanisms.
     *
     * @param localVersion Our version which is sent over.
     *
     * @param supportedFeatures The protocol features that we support.
     */
    // FIXME: this should probably be a constructor.
    static std::tuple<Version, FeatureSet> handshake(
        BufferedSink & to, Source & from, WorkerProto::Version localVersion, const FeatureSet & supportedFeatures);

    /**
     * After calling handshake, must call this to exchange some basic
     * information about the connection.
     */
    ClientHandshakeInfo postHandshake(const StoreDirConfig & store);

    void addTempRoot(const StoreDirConfig & remoteStore, bool * daemonException, const StorePath & path);

    StorePathSet queryValidPaths(
        const StoreDirConfig & remoteStore,
        bool * daemonException,
        const StorePathSet & paths,
        SubstituteFlag maybeSubstitute);

    std::optional<UnkeyedValidPathInfo>
    queryPathInfo(const StoreDirConfig & store, bool * daemonException, const StorePath & path);

    void putBuildDerivationRequest(
        const StoreDirConfig & store,
        bool * daemonException,
        const StorePath & drvPath,
        const BasicDerivation & drv,
        BuildMode buildMode);

    /**
     * Get the response, must be paired with
     * `putBuildDerivationRequest`.
     */
    BuildResult getBuildDerivationResponse(const StoreDirConfig & store, bool * daemonException);

    void narFromPath(
        const StoreDirConfig & store,
        bool * daemonException,
        const StorePath & path,
        std::function<void(Source &)> fun);
};

struct WorkerProto::BasicServerConnection : WorkerProto::BasicConnection
{
    /**
     * Establishes connection, negotiating version.
     *
     * @return The version provided by the other side of the
     * connection.
     *
     * @param to Taken by reference to allow for various error handling
     * mechanisms.
     *
     * @param from Taken by reference to allow for various error
     * handling mechanisms.
     *
     * @param localVersion Our version which is sent over.
     *
     * @param supportedFeatures The protocol features that we support.
     */
    // FIXME: this should probably be a constructor.
    static std::tuple<Version, FeatureSet> handshake(
        BufferedSink & to, Source & from, WorkerProto::Version localVersion, const FeatureSet & supportedFeatures);

    /**
     * After calling handshake, must call this to exchange some basic
     * information about the connection.
     */
    void postHandshake(const StoreDirConfig & store, const ClientHandshakeInfo & info);
};

} // namespace nix
