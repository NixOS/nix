#include "windows-async-pipe.hh"
#include "windows-error.hh"

namespace nix::windows {

void AsyncPipe::createAsyncPipe(HANDLE iocp)
{
    // std::cerr << (format("-----AsyncPipe::createAsyncPipe(%x)") % iocp) << std::endl;

    buffer.resize(0x1000);
    memset(&overlapped, 0, sizeof(overlapped));

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

    HANDLE hIocp = CreateIoCompletionPort(readSide.get(), iocp, (ULONG_PTR) (readSide.get()) ^ 0x5555, 0);
    if (hIocp != iocp)
        throw WinError("CreateIoCompletionPort(%x[%s], %x, ...) returned %x", readSide.get(), pipeName, iocp, hIocp);

    if (!ConnectNamedPipe(readSide.get(), &overlapped) && GetLastError() != ERROR_IO_PENDING)
        throw WinError("ConnectNamedPipe(%s)", pipeName);

    SECURITY_ATTRIBUTES psa2 = {0};
    psa2.nLength = sizeof(SECURITY_ATTRIBUTES);
    psa2.bInheritHandle = TRUE;

    writeSide = CreateFileA(pipeName.c_str(), GENERIC_WRITE, 0, &psa2, OPEN_EXISTING, 0, NULL);
    if (!readSide)
        throw WinError("CreateFileA(%s)", pipeName);
}

void AsyncPipe::close()
{
    readSide.close();
    writeSide.close();
}

}
