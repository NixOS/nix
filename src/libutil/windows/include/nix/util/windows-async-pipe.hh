#pragma once
///@file

#include "nix/util/file-descriptor.hh"

namespace nix::windows {

/***
 * An "async pipe" is a pipe whose read side is opened for overlapped
 * I/O, so that it can be read asynchronously (e.g. through an I/O
 * completion port, as Boost.Asio does).
 *
 * Unfortunately, only named pipes support that on windows, so we use
 * those with randomized temp file names.
 */
class AsyncPipe
{
public:
    AutoCloseFD writeSide, readSide;

    void createAsyncPipe();
    void close();
};

} // namespace nix::windows
