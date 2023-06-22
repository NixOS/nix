#include "remote-store.hh"
#include "worker-protocol.hh"

namespace nix {

/**
 * Bidirectional connection (send and receive) used by the Remote Store
 * implementation.
 *
 * Contains `Source` and `Sink` for actual communication, along with
 * other information learned when negotiating the connection.
 */
struct RemoteStore::Connection
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
     * Despite its name, I think it is actually the maximum version both
     * sides support. (If the maximum doesn't exist, we would fail to
     * establish a connection and produce a value of this type.)
     */
    unsigned int daemonVersion;

    /**
     * Whether the remote side trusts us or not.
     *
     * 3 values: "yes", "no", or `std::nullopt` for "unknown".
     *
     * Note that the "remote side" might not be just the end daemon, but
     * also an intermediary forwarder that can make its own trusting
     * decisions. This would be the intersection of all their trust
     * decisions, since it takes only one link in the chain to start
     * denying operations.
     */
    std::optional<TrustedFlag> remoteTrustsUs;

    /**
     * The version of the Nix daemon that is processing our requests.
     *
     * Do note, it may or may not communicating with another daemon,
     * rather than being an "end" `LocalStore` or similar.
     */
    std::optional<std::string> daemonNixVersion;

    /**
     * Time this connection was established.
     */
    std::chrono::time_point<std::chrono::steady_clock> startTime;

    /**
     * Coercion to `WorkerProto::ReadConn`. This makes it easy to use the
     * factored out worker protocol searlizers with a
     * `RemoteStore::Connection`.
     *
     * The worker protocol connection types are unidirectional, unlike
     * this type.
     */
    operator WorkerProto::ReadConn ()
    {
        return WorkerProto::ReadConn {
            .from = from,
        };
    }

    /**
     * Coercion to `WorkerProto::WriteConn`. This makes it easy to use the
     * factored out worker protocol searlizers with a
     * `RemoteStore::Connection`.
     *
     * The worker protocol connection types are unidirectional, unlike
     * this type.
     */
    operator WorkerProto::WriteConn ()
    {
        return WorkerProto::WriteConn {
            .to = to,
        };
    }

    virtual ~Connection();

    virtual void closeWrite() = 0;

    std::exception_ptr processStderr(Sink * sink = 0, Source * source = 0, bool flush = true);
};

}
