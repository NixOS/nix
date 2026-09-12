#include "nix/util/windows-async-pipe.hh"

namespace nix::windows {

void AsyncPipe::createAsyncPipe()
{
    std::string pipeName = fmt("\\\\.\\pipe\\nix-%d-%p", GetCurrentProcessId(), (void *) this);

    readSide = CreateNamedPipeA(
        pipeName.c_str(),
        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE,
        PIPE_UNLIMITED_INSTANCES,
        0,
        0,
        INFINITE,
        NULL);
    if (!readSide)
        throw WinError("CreateNamedPipeA(%s)", pipeName);

    /* Connecting is asynchronous on an overlapped pipe; it completes once
       the write side below has been opened, so wait for it right after. */
    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(overlapped));
    if (!ConnectNamedPipe(readSide.get(), &overlapped) && GetLastError() != ERROR_IO_PENDING)
        throw WinError("ConnectNamedPipe(%s)", pipeName);

    SECURITY_ATTRIBUTES psa2 = {0};
    psa2.nLength = sizeof(SECURITY_ATTRIBUTES);
    psa2.bInheritHandle = TRUE;

    writeSide = CreateFileA(pipeName.c_str(), GENERIC_WRITE, 0, &psa2, OPEN_EXISTING, 0, NULL);
    if (!writeSide)
        throw WinError("CreateFileA(%s)", pipeName);

    DWORD ignored;
    if (!GetOverlappedResult(readSide.get(), &overlapped, &ignored, TRUE) && GetLastError() != ERROR_PIPE_CONNECTED)
        throw WinError("GetOverlappedResult(ConnectNamedPipe(%s))", pipeName);
}

void AsyncPipe::close()
{
    readSide.close();
    writeSide.close();
}

} // namespace nix::windows
