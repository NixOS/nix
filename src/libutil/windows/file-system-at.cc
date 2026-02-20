#include "nix/util/file-system-at.hh"
#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/util/file-path.hh"
#include "nix/util/source-accessor.hh"

#include <fileapi.h>
#include <error.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <winternl.h>

namespace nix {

using namespace nix::windows;

namespace windows {

namespace {

/**
 * Open a file/directory relative to a directory handle using NtCreateFile.
 *
 * @param dirFd Directory handle to open relative to
 * @param pathComponent Single path component (not a full path)
 * @param desiredAccess Access rights requested
 * @param createOptions NT create options flags
 * @param createDisposition FILE_OPEN, FILE_CREATE, etc.
 * @return Handle to the opened file/directory (caller must close)
 */
AutoCloseFD ntOpenAt(
    Descriptor dirFd,
    std::wstring_view pathComponent,
    ACCESS_MASK desiredAccess,
    ULONG createOptions,
    ULONG createDisposition = FILE_OPEN)
{
    /* Set up UNICODE_STRING for the relative path */
    UNICODE_STRING pathStr;
    pathStr.Buffer = const_cast<PWSTR>(pathComponent.data());
    pathStr.Length = static_cast<USHORT>(pathComponent.size() * sizeof(wchar_t));
    pathStr.MaximumLength = pathStr.Length;

    /* Set up OBJECT_ATTRIBUTES to open relative to dirFd */
    OBJECT_ATTRIBUTES objAttrs;
    InitializeObjectAttributes(
        &objAttrs,
        &pathStr,
        0,      // No special flags
        dirFd,  // RootDirectory
        nullptr // No security descriptor
    );

    /* Open using NT API */
    IO_STATUS_BLOCK ioStatus;
    HANDLE h;
    NTSTATUS status = NtCreateFile(
        &h,
        desiredAccess,
        &objAttrs,
        &ioStatus,
        nullptr, // No allocation size
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        createDisposition,
        createOptions | FILE_SYNCHRONOUS_IO_NONALERT,
        nullptr, // No EA buffer
        0        // No EA length
    );

    if (status != 0)
        throw WinError(
            RtlNtStatusToDosError(status), "opening %s relative to directory handle", PathFmt(pathComponent));

    return AutoCloseFD{h};
}

/**
 * Open a symlink relative to a directory handle without following it.
 *
 * @param dirFd Directory handle to open relative to
 * @param path Relative path to the symlink
 * @return Handle to the symlink
 */
AutoCloseFD openSymlinkAt(Descriptor dirFd, const CanonPath & path)
{
    assert(!path.isRoot());
    assert(!path.rel().starts_with('/')); /* Just in case the invariant is somehow broken. */

    std::wstring wpath = string_to_os_string(path.rel());
    return ntOpenAt(dirFd, wpath, FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN_REPARSE_POINT);
}

/**
 * This struct isn't defined in the normal Windows SDK, but only in the Windows Driver Kit.
 *
 * I (@Ericson2314) would not normally do something like this, but LLVM
 * has decided that this is in fact stable, per
 * https://github.com/llvm/llvm-project/blob/main/libcxx/src/filesystem/posix_compat.h,
 * so I guess that is good enough for us. GCC doesn't support symlinks
 * at all on windows so we have to put it here, not grab it from private
 * c++ standard library headers anyways.
 */
struct ReparseDataBuffer
{
    unsigned long ReparseTag;
    unsigned short ReparseDataLength;
    unsigned short Reserved;

    union
    {
        struct
        {
            unsigned short SubstituteNameOffset;
            unsigned short SubstituteNameLength;
            unsigned short PrintNameOffset;
            unsigned short PrintNameLength;
            unsigned long Flags;
            wchar_t PathBuffer[1];
        } SymbolicLinkReparseBuffer;

        struct
        {
            unsigned short SubstituteNameOffset;
            unsigned short SubstituteNameLength;
            unsigned short PrintNameOffset;
            unsigned short PrintNameLength;
            wchar_t PathBuffer[1];
        } MountPointReparseBuffer;

        struct
        {
            unsigned char DataBuffer[1];
        } GenericReparseBuffer;
    };
};

/**
 * Read the target of a symlink from an open handle.
 *
 * @param linkHandle Handle to a symlink (must have been opened with FILE_OPEN_REPARSE_POINT)
 * @return The symlink target as a wide string
 */
OsString readSymlinkTarget(HANDLE linkHandle)
{
    uint8_t buf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    DWORD out;

    checkInterrupt();

    if (!DeviceIoControl(linkHandle, FSCTL_GET_REPARSE_POINT, nullptr, 0, buf, sizeof(buf), &out, nullptr))
        throw WinError("reading reparse point for handle %d", linkHandle);

    const auto * reparse = reinterpret_cast<const ReparseDataBuffer *>(buf);
    size_t path_buf_offset = offsetof(ReparseDataBuffer, SymbolicLinkReparseBuffer.PathBuffer[0]);

    if (out < path_buf_offset) {
        auto fullPath = descriptorToPath(linkHandle);
        throw WinError(
            DWORD{ERROR_REPARSE_TAG_INVALID}, "invalid reparse data for %d:%s", linkHandle, PathFmt(fullPath));
    }

    if (reparse->ReparseTag != IO_REPARSE_TAG_SYMLINK) {
        auto fullPath = descriptorToPath(linkHandle);
        throw WinError(DWORD{ERROR_REPARSE_TAG_INVALID}, "not a symlink: %d:%s", linkHandle, PathFmt(fullPath));
    }

    const auto & symlink = reparse->SymbolicLinkReparseBuffer;
    unsigned short name_offset, name_length;

    /* Prefer PrintName over SubstituteName if available */
    if (symlink.PrintNameLength == 0) {
        name_offset = symlink.SubstituteNameOffset;
        name_length = symlink.SubstituteNameLength;
    } else {
        name_offset = symlink.PrintNameOffset;
        name_length = symlink.PrintNameLength;
    }

    if (path_buf_offset + name_offset + name_length > out) {
        auto fullPath = descriptorToPath(linkHandle);
        throw WinError(
            DWORD{ERROR_REPARSE_TAG_INVALID}, "invalid symlink data for %d:%s", linkHandle, PathFmt(fullPath));
    }

    /* Extract the target path */
    const wchar_t * target_start = &symlink.PathBuffer[name_offset / sizeof(wchar_t)];
    size_t target_len = name_length / sizeof(wchar_t);

    return {target_start, target_len};
}

/**
 * Check if a handle refers to a reparse point (e.g., symlink).
 *
 * @param handle Open file/directory handle
 * @return true if the handle refers to a reparse point
 */
bool isReparsePoint(HANDLE handle)
{
    FILE_BASIC_INFO basicInfo;
    if (!GetFileInformationByHandleEx(handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)))
        throw WinError("GetFileInformationByHandleEx");

    return (basicInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

} // anonymous namespace

} // namespace windows

AutoCloseFD openFileEnsureBeneathNoSymlinks(
    Descriptor dirFd, const CanonPath & path, ACCESS_MASK desiredAccess, ULONG createOptions, ULONG createDisposition)
{
    assert(!path.isRoot());
    assert(!path.rel().starts_with('/')); /* Just in case the invariant is somehow broken. */

    AutoCloseFD parentFd;
    auto nrComponents = std::ranges::distance(path);
    assert(nrComponents >= 1);
    auto components = std::views::take(path, nrComponents - 1); /* Everything but last component */
    auto getParentFd = [&]() { return parentFd ? parentFd.get() : dirFd; };

    /* Helper to construct CanonPath from components up to (and including) the given iterator */
    auto pathUpTo = [&](auto it) {
        return std::ranges::fold_left(components.begin(), it, CanonPath::root, [](auto lhs, auto rhs) {
            lhs.push(rhs);
            return lhs;
        });
    };

    /* Helper to check if a component is a symlink and throw SymlinkNotAllowed if so */
    auto throwIfSymlink = [&](std::wstring_view component, const CanonPath & pathForError) {
        try {
            auto testHandle =
                ntOpenAt(getParentFd(), component, FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN_REPARSE_POINT);
            if (isReparsePoint(testHandle.get()))
                throw SymlinkNotAllowed(pathForError);
        } catch (SymlinkNotAllowed &) {
            throw;
        } catch (...) {
            /* If we can't determine, ignore and let caller handle original error */
        }
    };

    /* Iterate through each path component to ensure no symlinks in intermediate directories.
     * This prevents TOCTOU issues by opening each component relative to the parent. */
    for (auto it = components.begin(); it != components.end(); ++it) {
        std::wstring wcomponent = string_to_os_string(std::string(*it));

        /* Open directory without following symlinks */
        AutoCloseFD parentFd2;
        try {
            parentFd2 = ntOpenAt(
                getParentFd(),
                wcomponent,
                FILE_TRAVERSE | SYNCHRONIZE,                  // Just need traversal rights
                FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT // Open directory, don't follow symlinks
            );
        } catch (WinError & e) {
            /* Check if this is because it's a symlink */
            if (e.lastError == ERROR_CANT_ACCESS_FILE || e.lastError == ERROR_ACCESS_DENIED) {
                throwIfSymlink(wcomponent, pathUpTo(std::next(it)));
            }
            throw;
        }

        /* Check if what we opened is actually a symlink */
        if (isReparsePoint(parentFd2.get())) {
            throw SymlinkNotAllowed(pathUpTo(std::next(it)));
        }

        parentFd = std::move(parentFd2);
    }

    /* Now open the final component with requested flags */
    std::wstring finalComponent = string_to_os_string(std::string(path.baseName().value()));

    AutoCloseFD finalHandle;
    try {
        finalHandle = ntOpenAt(
            getParentFd(),
            finalComponent,
            desiredAccess,
            createOptions | FILE_OPEN_REPARSE_POINT, // Don't follow symlinks on final component either
            createDisposition);
    } catch (WinError & e) {
        /* Check if final component is a symlink when we requested to not follow it */
        if (e.lastError == ERROR_CANT_ACCESS_FILE) {
            throwIfSymlink(finalComponent, path);
        }
        throw;
    }

    /* Final check: did we accidentally open a symlink? */
    if (isReparsePoint(finalHandle.get()))
        throw SymlinkNotAllowed(path);

    return finalHandle;
}

OsString readLinkAt(Descriptor dirFd, const CanonPath & path)
{
    AutoCloseFD linkHandle(windows::openSymlinkAt(dirFd, path));
    return windows::readSymlinkTarget(linkHandle.get());
}

} // namespace nix
