#pragma once
///@file

#include "nix/util/file-descriptor.hh"
#ifdef _WIN32

namespace nix::windows {

/***
 * An "async pipe" is a pipe that supports I/O Completion Ports so
 * multiple pipes can be listened too.
 *
 * Unfortunately, only named pipes support that on windows, so we use
 * those with randomized temp file names.
 */
class AsyncPipe
{
public:
    AutoCloseFD writeSide, readSide;
    OVERLAPPED overlapped;
    DWORD got;
    std::vector<unsigned char> buffer;

    void createAsyncPipe(HANDLE iocp);
    void close();
};

} // namespace nix::windows
#endif
