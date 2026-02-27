#pragma once
///@file

#include "nix/store/remote-store.hh"
#include "nix/store/worker-protocol.hh"
#include "nix/store/worker-protocol-connection.hh"
#include "nix/util/pool.hh"

namespace nix {

/**
 * Bidirectional connection (send and receive) used by the Remote Store
 * implementation.
 *
 * Contains `Source` and `Sink` for actual communication, along with
 * other information learned when negotiating the connection.
 */
struct RemoteStore::Connection : WorkerProto::BasicClientConnection, WorkerProto::ClientHandshakeInfo
{
    /**
     * Time this connection was established.
     */
    std::chrono::time_point<std::chrono::steady_clock> startTime;
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
