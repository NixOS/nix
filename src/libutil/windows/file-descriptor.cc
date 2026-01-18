#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/util/finally.hh"
#include "nix/util/serialise.hh"
#include "nix/util/file-path.hh"

#include <fileapi.h>
#include <error.h>
#include <namedpipeapi.h>
#include <namedpipeapi.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace nix {

using namespace nix::windows;

std::string readFile(HANDLE handle)
{
    LARGE_INTEGER li;
    if (!GetFileSizeEx(handle, &li))
        throw WinError("%s:%d statting file", __FILE__, __LINE__);

    return drainFD(handle, true, li.QuadPart);
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
#if _WIN32_WINNT >= 0x0600
        auto path = handleToPath(handle); // debug; do it before because handleToPath changes lasterror
        if (!WriteFile(handle, s.data(), s.size(), &res, NULL)) {
            throw WinError("writing to file %1%:%2%", handle, path);
        }
#else
        if (!WriteFile(handle, s.data(), s.size(), &res, NULL)) {
            throw WinError("writing to file %1%", handle);
        }
#endif
        if (res > 0)
            s.remove_prefix(res);
    }
}

std::string readLine(HANDLE handle, bool eofOk)
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
            if (ch == '\n')
                return s;
            s += ch;
        }
    }
}

void drainFD(HANDLE handle, Sink & sink /*, bool block*/)
{
    std::vector<unsigned char> buf(64 * 1024);
    while (1) {
        checkInterrupt();
        DWORD rd;
        if (!ReadFile(handle, buf.data(), buf.size(), &rd, NULL)) {
            WinError winError("%s:%d reading from handle %p", __FILE__, __LINE__, handle);
            if (winError.lastError == ERROR_BROKEN_PIPE)
                break;
            throw winError;
        } else if (rd == 0)
            break;
        sink({(char *) buf.data(), (size_t) rd});
    }
}

void copyFdRange(Descriptor fd, off_t offset, size_t nbytes, Sink & sink)
{
    auto left = nbytes;
    std::array<char, 64 * 1024> buf;

    while (left) {
        checkInterrupt();
        auto limit = std::min<decltype(buf)::size_type>(left, buf.size());
        OVERLAPPED ov = {};
        ov.Offset = static_cast<DWORD>(offset);
        if constexpr (sizeof(offset) > 4) /* We don't build with 32 bit off_t, but let's be safe. */
            ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD n;
        if (!ReadFile(fd, buf.data(), static_cast<DWORD>(limit), &n, &ov))
            throw nix::windows::WinError("ReadFile of %1% bytes at offset %2%", left, offset);
        if (n == 0)
            throw EndOfFile("unexpected end-of-file");
        assert(static_cast<size_t>(n) <= left);
        sink(std::string_view(buf.data(), n));
        offset += n;
        left -= n;
    }
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

#if _WIN32_WINNT >= 0x0600

std::wstring windows::handleToFileName(HANDLE handle)
{
    std::vector<wchar_t> buf(0x100);
    DWORD dw = GetFinalPathNameByHandleW(handle, buf.data(), buf.size(), FILE_NAME_OPENED);
    if (dw == 0) {
        if (handle == GetStdHandle(STD_INPUT_HANDLE))
            return L"<stdin>";
        if (handle == GetStdHandle(STD_OUTPUT_HANDLE))
            return L"<stdout>";
        if (handle == GetStdHandle(STD_ERROR_HANDLE))
            return L"<stderr>";
        return (boost::wformat(L"<unnnamed handle %X>") % handle).str();
    }
    if (dw > buf.size()) {
        buf.resize(dw);
        if (GetFinalPathNameByHandleW(handle, buf.data(), buf.size(), FILE_NAME_OPENED) != dw - 1)
            throw WinError("GetFinalPathNameByHandleW");
        dw -= 1;
    }
    return std::wstring(buf.data(), dw);
}

Path windows::handleToPath(HANDLE handle)
{
    return os_string_to_string(handleToFileName(handle));
}

#endif

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

} // namespace nix
