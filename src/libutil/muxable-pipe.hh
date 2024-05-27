#pragma once
///@file

#include "file-descriptor.hh"
#ifdef _WIN32
#  include "windows-async-pipe.hh"
#endif

#ifndef _WIN32
#  include <poll.h>
#else
#  include <ioapiset.h>
#  include "windows-error.hh"
#endif

namespace nix {

/**
 * An "muxable pipe" is a type of pipe supporting endpoints that wait
 * for events on multiple pipes at once.
 *
 * On Unix, this is just a regular anonymous pipe. On Windows, this has
 * to be a named pipe because we need I/O Completion Ports to wait on
 * multiple pipes.
 */
using MuxablePipe =
#ifndef _WIN32
    Pipe
#else
    windows::AsyncPipe
#endif
    ;

/**
 * Use pool() (Unix) / I/O Completion Ports (Windows) to wait for the
 * input side of any logger pipe to become `available'.  Note that
 * `available' (i.e., non-blocking) includes EOF.
 */
struct MuxablePipePollState
{
#ifndef _WIN32
    std::vector<struct pollfd> pollStatus;
    std::map<int, size_t> fdToPollStatus;
#else
    OVERLAPPED_ENTRY oentries[0x20] = {0};
    ULONG removed;
    bool gotEOF = false;

#endif

    /**
     * Check for ready (Unix) / completed (Windows) operations
     */
    void poll(
#ifdef _WIN32
        HANDLE ioport,
#endif
        std::optional<unsigned int> timeout);

    using CommChannel =
#ifndef _WIN32
        Descriptor
#else
        windows::AsyncPipe *
#endif
        ;

    /**
     * Process for ready (Unix) / completed (Windows) operations,
     * calling the callbacks as needed.
     *
     * @param handleRead callback to be passed read data.
     *
     * @param handleEOF callback for when the `MuxablePipe` has closed.
     */
    void iterate(
        std::set<CommChannel> & channels,
        std::function<void(Descriptor fd, std::string_view data)> handleRead,
        std::function<void(Descriptor fd)> handleEOF);
};

}
