#pragma once

///@file

#include "nix/util/async.hh"
#include "nix/util/serialise.hh"
#include "nix/util/file-descriptor.hh"

#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/spawn.hpp>

namespace nix {

using NativeAsyncStreamDescriptor =
#ifndef _WIN32
    asio::posix::stream_descriptor
#else
    asio::windows::stream_handle
#endif
    ;

/**
 * @brief Source that reads from an owning Descriptor asynchronously.
 *
 * When the read would block, the caller is suspended. It's important to
 * ensure that this object is not migrated between coroutines.
 */
class AsyncFdSource : public BufferedSource
{
    NativeAsyncStreamDescriptor fd;
    asio::yield_context yield;

public:
    AsyncFdSource(asio::io_context & ctx, asio::yield_context yield, AutoCloseFD fd)
        : fd(ctx, fd.release())
        , yield(std::move(yield))
    {
    }

    size_t readUnbuffered(char * data, size_t len) override
    {
        boost::system::error_code ec;
        auto res = fd.async_read_some(asio::mutable_buffer(data, len), yield[ec]);
        if (ec == boost::asio::error::eof)
            return 0;
        else if (ec)
            throw SystemError(ec, "read of %1% bytes", len);
        return res;
    }
};

/**
 * @brief Sink that writes to an owning Descriptor asynchronously.
 *
 * When the write would block, the caller is suspended. It's important to
 * ensure that this object is not migrated between coroutines.
 */
class AsyncFdSink : public BufferedSink
{
    NativeAsyncStreamDescriptor fd;
    asio::yield_context yield;

public:
    AsyncFdSink(asio::io_context & ctx, asio::yield_context yield, AutoCloseFD fd)
        : fd(ctx, fd.release())
        , yield(std::move(yield))
    {
    }

    /* Note that unlike ~FdSink we are intentionally not flushing on
       destruction, because potentially switching contexts during stack
       unwinding is very sketchy. */

    void writeUnbuffered(std::string_view data) override
    {
        while (!data.empty()) {
            boost::system::error_code ec;
            auto consumed = fd.async_write_some(asio::buffer(data.data(), data.size()), yield[ec]);
            if (ec)
                throw SystemError(ec, "write of %1% bytes", data.size());
            data.remove_prefix(consumed);
        }
    }
};

} // namespace nix
