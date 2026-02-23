#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/util/finally.hh"
#include "nix/util/serialise.hh"

#include <span>

#include <fileapi.h>
#include <error.h>
#include <namedpipeapi.h>
#include <namedpipeapi.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace nix {

using namespace nix::windows;

std::make_unsigned_t<off_t> getFileSize(Descriptor fd)
{
    LARGE_INTEGER li;
    if (!GetFileSizeEx(fd, &li)) {
        auto lastError = GetLastError();
        throw WinError(lastError, "getting size of file %s", PathFmt(descriptorToPath(fd)));
    }
    return li.QuadPart;
}

size_t read(Descriptor fd, std::span<std::byte> buffer)
{
    checkInterrupt(); // For consistency with unix, and its EINTR loop
    DWORD n;
    if (!ReadFile(fd, buffer.data(), static_cast<DWORD>(buffer.size()), &n, NULL)) {
        auto lastError = GetLastError();
        if (lastError == ERROR_BROKEN_PIPE)
            n = 0; // Treat as EOF
        else
            throw WinError(lastError, "reading %1% bytes from %2%", buffer.size(), PathFmt(descriptorToPath(fd)));
    }
    return static_cast<size_t>(n);
}

size_t readOffset(Descriptor fd, off_t offset, std::span<std::byte> buffer)
{
    checkInterrupt(); // For consistency with unix, and its EINTR loop
    OVERLAPPED ov = {};
    ov.Offset = static_cast<DWORD>(offset);
    if constexpr (sizeof(offset) > 4) /* We don't build with 32 bit off_t, but let's be safe. */
        ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD n;
    if (!ReadFile(fd, buffer.data(), static_cast<DWORD>(buffer.size()), &n, &ov)) {
        auto lastError = GetLastError();
        throw WinError(
            lastError,
            "reading %1% bytes at offset %2% from %3%",
            buffer.size(),
            offset,
            PathFmt(descriptorToPath(fd)));
    }
    return static_cast<size_t>(n);
}

size_t write(Descriptor fd, std::span<const std::byte> buffer, bool allowInterrupts)
{
    if (allowInterrupts)
        checkInterrupt(); // For consistency with unix
    DWORD n;
    if (!WriteFile(fd, buffer.data(), static_cast<DWORD>(buffer.size()), &n, NULL)) {
        auto lastError = GetLastError();
        throw WinError(lastError, "writing %1% bytes to %2%", buffer.size(), PathFmt(descriptorToPath(fd)));
    }
    return static_cast<size_t>(n);
}

//////////////////////////////////////////////////////////////////////

void Pipe::create()
{
    SECURITY_ATTRIBUTES saAttr = {0};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.lpSecurityDescriptor = NULL;
    saAttr.bInheritHandle = TRUE;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &saAttr, 0))
        throw WinError("CreatePipe");

    readSide = hReadPipe;
    writeSide = hWritePipe;
}

//////////////////////////////////////////////////////////////////////

off_t lseek(HANDLE h, off_t offset, int whence)
{
    DWORD method;
    switch (whence) {
    case SEEK_SET:
        method = FILE_BEGIN;
        break;
    case SEEK_CUR:
        method = FILE_CURRENT;
        break;
    case SEEK_END:
        method = FILE_END;
        break;
    default:
        throw Error("lseek: invalid whence %d", whence);
    }

    LARGE_INTEGER li;
    li.QuadPart = offset;
    LARGE_INTEGER newPos;

    if (!SetFilePointerEx(h, li, &newPos, method)) {
        /* Convert to a POSIX error, since caller code works with this as if it were
           a POSIX lseek. */
        errno = std::error_code(GetLastError(), std::system_category()).default_error_condition().value();
        return -1;
    }

    return newPos.QuadPart;
}

void syncDescriptor(Descriptor fd)
{
    if (!::FlushFileBuffers(fd)) {
        auto lastError = GetLastError();
        throw WinError(lastError, "flushing file %s", PathFmt(descriptorToPath(fd)));
    }
}

} // namespace nix
