#pragma once

///@file

#include "nix/util/async.hh"
#include "nix/util/serialise.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/logging.hh"

#ifdef _WIN32
#  include <boost/asio/windows/stream_handle.hpp>
#else
#  include <boost/asio/posix/stream_descriptor.hpp>
#endif
#include <boost/asio/spawn.hpp>

#include <cassert>
#include <thread>

#ifndef _WIN32
#  include <cerrno>
#  include <poll.h>
#endif

namespace nix {

/** Hold across a suspension to restore the per-thread current activity on resumption. */
struct SuspendedActivity
{
    const ActivityId saved = getCurActivity();

    ~SuspendedActivity()
    {
        setCurActivity(saved);
    }
};

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
 *
 * End of file throws `EndOfFile`, since returning 0 would make `Source::operator()` spin.
 */
class AsyncFdSource : public BufferedSource
{
    NativeAsyncStreamDescriptor fd;
    asio::yield_context yield;
    std::thread::id owner = std::this_thread::get_id();
    bool _good = true;

public:
    BackedStringView endOfFileError{"unexpected end-of-file"};

    AsyncFdSource(asio::io_context & ctx, asio::yield_context yield, AutoCloseFD fd)
        : fd(ctx, fd.release())
        , yield(std::move(yield))
    {
    }

    ~AsyncFdSource();

    size_t readUnbuffered(char * data, size_t len) override
    {
        assert(std::this_thread::get_id() == owner);
        boost::system::error_code ec;
        std::size_t res;
        try {
            SuspendedActivity suspended;
            res = fd.async_read_some(asio::mutable_buffer(data, len), yield[ec]);
        } catch (boost::system::system_error & e) {
            /* After a cancellation, `throw_if_cancelled` throws here instead of setting `ec`. */
            _good = false;
            throw SystemError(e.code(), "read of %1% bytes", len);
        }
        if (ec == boost::asio::error::eof || (!ec && res == 0)) {
            _good = false;
            throw EndOfFile(std::string(*endOfFileError));
        } else if (ec) {
            _good = false;
            throw SystemError(ec, "read of %1% bytes", len);
        }
        return res;
    }

    bool good() override
    {
        return _good;
    }

    bool hasData() override
    {
        if (BufferedSource::hasData())
            return true;
#ifdef _WIN32
        /* An overlapped handle cannot be probed without issuing a read. */
        return false;
#else
        while (true) {
            struct pollfd pfd;
            pfd.fd = fd.native_handle();
            pfd.events = POLLIN;
            pfd.revents = 0;

            auto n = ::poll(&pfd, 1, 0);
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                throw SysError("polling file descriptor");
            }
            return n > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
        }
#endif
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
    std::thread::id owner = std::this_thread::get_id();
    bool _good = true;

public:
    AsyncFdSink(asio::io_context & ctx, asio::yield_context yield, AutoCloseFD fd)
        : fd(ctx, fd.release())
        , yield(std::move(yield))
    {
    }

    /* Note that unlike ~FdSink we are intentionally not flushing on
       destruction, because potentially switching contexts during stack
       unwinding is very sketchy. */
    ~AsyncFdSink();

    /** Drops unflushed data instead of writing it, so it is safe in a destructor. */
    void close()
    {
        bufPos = 0;
        boost::system::error_code ec;
        if (fd.is_open())
            fd.close(ec);
    }

    void writeUnbuffered(std::string_view data) override
    {
        assert(std::this_thread::get_id() == owner);
        while (!data.empty()) {
            boost::system::error_code ec;
            std::size_t consumed;
            try {
                SuspendedActivity suspended;
                consumed = fd.async_write_some(asio::buffer(data.data(), data.size()), yield[ec]);
            } catch (boost::system::system_error & e) {
                _good = false;
                throw SystemError(e.code(), "write of %1% bytes", data.size());
            }
            if (ec) {
                _good = false;
                throw SystemError(ec, "write of %1% bytes", data.size());
            }
            data.remove_prefix(consumed);
        }
    }

    bool good() override
    {
        return _good;
    }
};

} // namespace nix
