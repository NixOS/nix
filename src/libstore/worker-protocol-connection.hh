#pragma once
///@file

#include "worker-protocol.hh"
#include "store-api.hh"

namespace nix {

struct WorkerProto::BasicClientConnection
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
     * Worker protocol version used for the connection.
     *
     * Despite its name, it is actually the maximum version both
     * sides support. (If the maximum doesn't exist, we would fail to
     * establish a connection and produce a value of this type.)
     */
    WorkerProto::Version daemonVersion;

    /**
     * Flush to direction
     */
    virtual ~BasicClientConnection();

    virtual void closeWrite() = 0;

    std::exception_ptr processStderrReturn(Sink * sink = 0, Source * source = 0, bool flush = true);

    void processStderr(bool * daemonException, Sink * sink = 0, Source * source = 0, bool flush = true);

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
    static Version handshake(BufferedSink & to, Source & from, WorkerProto::Version localVersion);

    /**
     * After calling handshake, must call this to exchange some basic
     * information abou the connection.
     */
    ClientHandshakeInfo postHandshake(const StoreDirConfig & store);

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
            .version = daemonVersion,
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
            .version = daemonVersion,
        };
    }

    void addTempRoot(const StoreDirConfig & remoteStore, bool * daemonException, const StorePath & path);

    StorePathSet queryValidPaths(
        const StoreDirConfig & remoteStore,
        bool * daemonException,
        const StorePathSet & paths,
        SubstituteFlag maybeSubstitute);

    UnkeyedValidPathInfo queryPathInfo(const StoreDirConfig & store, bool * daemonException, const StorePath & path);

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

    void importPaths(const StoreDirConfig & store, bool * daemonException, Source & source);
};

struct WorkerProto::BasicServerConnection
{
    /**
     * Send with this.
     */
    FdSink & to;

    /**
     * Receive with this.
     */
    FdSource & from;

    /**
     * Worker protocol version used for the connection.
     *
     * Despite its name, it is actually the maximum version both
     * sides support. (If the maximum doesn't exist, we would fail to
     * establish a connection and produce a value of this type.)
     */
    WorkerProto::Version clientVersion;

    operator WorkerProto::ReadConn()
    {
        return WorkerProto::ReadConn{
            .from = from,
            .version = clientVersion,
        };
    }

    operator WorkerProto::WriteConn()
    {
        return WorkerProto::WriteConn{
            .to = to,
            .version = clientVersion,
        };
    }

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
    static WorkerProto::Version handshake(BufferedSink & to, Source & from, WorkerProto::Version localVersion);

    /**
     * After calling handshake, must call this to exchange some basic
     * information abou the connection.
     */
    void postHandshake(const StoreDirConfig & store, const ClientHandshakeInfo & info);
};

}
