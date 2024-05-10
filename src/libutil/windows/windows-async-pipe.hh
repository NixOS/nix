#pragma once
///@file

#include "file-descriptor.hh"

namespace nix::windows {

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

}
