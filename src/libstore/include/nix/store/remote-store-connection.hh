#pragma once
///@file

#include "nix/store/remote-store.hh"
#include "nix/store/worker-protocol.hh"
#include "nix/store/worker-protocol-connection.hh"
#include "nix/util/pool.hh"
#include "nix/util/lru-cache.hh"

#include <utility>

namespace nix {

/**
 * A base listed before `BasicClientConnection`, not a member, so that the
 * streams outlive its destructor, which flushes `to`.
 */
struct RemoteStore::ConnectionStreams
{
    std::unique_ptr<BufferedSink> ownedTo;
    std::unique_ptr<BufferedSource> ownedFrom;
};

/**
 * Bidirectional connection (send and receive) used by the Remote Store
 * implementation.
 *
 * Contains `Source` and `Sink` for actual communication, along with
 * other information learned when negotiating the connection.
 */
struct RemoteStore::Connection : RemoteStore::ConnectionStreams,
                                 WorkerProto::BasicClientConnection,
                                 WorkerProto::ClientHandshakeInfo
{
private:
    /* VTable anchor to avoid weak linkage of the vtable - it breaks
       dynamic_cast across shared libraries on Darwin. */
    virtual void anchor();

public:
    Connection(std::unique_ptr<BufferedSink> to, std::unique_ptr<BufferedSource> from)
        : ConnectionStreams{std::move(to), std::move(from)}
        , BasicClientConnection(*ownedTo, *ownedFrom)
    {
    }

    /**
     * `INVALID_DESCRIPTOR` if the stream has no descriptor behind it.
     */
    virtual Descriptor fromFd() const = 0;

    virtual Descriptor toFd() const = 0;

    /**
     * Time this connection was established.
     */
    std::chrono::time_point<std::chrono::steady_clock> startTime;

    /**
     * Paths already pinned as temp roots on this connection.
     * Temp roots persist for the duration of the connection, so retransmission
     * is not required.
     * Skipping the wire call is an optional but worthwhile optimization that
     * reduces communication overhead. LRU eviction would cost a round-trip.
     */
    LRUCache<StorePath, std::monostate> tempRootsPinned{65536};
};

/**
 * A wrapper around Pool<RemoteStore::Connection>::Handle that marks
 * the connection as bad (causing it to be closed) if a non-daemon
 * exception is thrown before the handle is closed. Such an exception
 * causes a deviation from the expected protocol and therefore a
 * desynchronization between the client and daemon.
 */
struct RemoteStore::ConnectionHandle
{
    Pool<RemoteStore::Connection>::Handle handle;
    bool daemonException = false;

    ConnectionHandle(Pool<RemoteStore::Connection>::Handle && handle)
        : handle(std::move(handle))
    {
    }

    ConnectionHandle(ConnectionHandle && h) noexcept
        : handle(std::move(h.handle))
    {
    }

    ~ConnectionHandle();

    RemoteStore::Connection & operator*()
    {
        return *handle;
    }

    RemoteStore::Connection * operator->()
    {
        return &*handle;
    }

    void processStderr(Sink * sink = 0, Source * source = 0, bool flush = true, bool block = true);

    void withFramedSink(fun<void(Sink & sink)> sendData);
};

} // namespace nix
