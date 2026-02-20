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
    if (!GetFileSizeEx(fd, &li))
        throw WinError("GetFileSizeEx");
    return li.QuadPart;
}

void readFull(HANDLE handle, char * buf, size_t count)
{
    while (count) {
        checkInterrupt();
        DWORD res;
        if (!ReadFile(handle, (char *) buf, count, &res, NULL))
            throw WinError("%s:%d reading from file", __FILE__, __LINE__);
        if (res == 0)
            throw EndOfFile("unexpected end-of-file");
        count -= res;
        buf += res;
    }
}

void writeFull(HANDLE handle, std::string_view s, bool allowInterrupts)
{
    while (!s.empty()) {
        if (allowInterrupts)
            checkInterrupt();
        DWORD res;
        if (!WriteFile(handle, s.data(), s.size(), &res, NULL)) {
            // Do this because `descriptorToPath` will overwrite the last error.
            auto lastError = GetLastError();
            auto path = descriptorToPath(handle);
            throw WinError(lastError, "writing to file %d:%s", handle, PathFmt(path));
        }
        if (res > 0)
            s.remove_prefix(res);
    }
}

std::string readLine(HANDLE handle, bool eofOk, char terminator)
{
    std::string s;
    while (1) {
        checkInterrupt();
        char ch;
        // FIXME: inefficient
        DWORD rd;
        if (!ReadFile(handle, &ch, 1, &rd, NULL)) {
            throw WinError("reading a line");
        } else if (rd == 0) {
            if (eofOk)
                return s;
            else
                throw EndOfFile("unexpected EOF reading a line");
        } else {
            if (ch == terminator)
                return s;
            s += ch;
        }
    }
}

size_t read(Descriptor fd, std::span<std::byte> buffer)
{
    DWORD n;
    if (!ReadFile(fd, buffer.data(), static_cast<DWORD>(buffer.size()), &n, NULL))
        throw WinError("ReadFile of %1% bytes", buffer.size());
    return static_cast<size_t>(n);
}

size_t readOffset(Descriptor fd, off_t offset, std::span<std::byte> buffer)
{
    OVERLAPPED ov = {};
    ov.Offset = static_cast<DWORD>(offset);
    if constexpr (sizeof(offset) > 4) /* We don't build with 32 bit off_t, but let's be safe. */
        ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD n;
    if (!ReadFile(fd, buffer.data(), static_cast<DWORD>(buffer.size()), &n, &ov))
        throw WinError("ReadFile of %1% bytes at offset %2%", buffer.size(), offset);
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
    if (!::FlushFileBuffers(fd))
        throw WinError("FlushFileBuffers file descriptor %1%", fd);
}

} // namespace nix
