#include "nix/util/file-system-at.hh"
#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/util/file-path.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/util.hh"

#include <boost/outcome.hpp>
#include <span>

#include <fileapi.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <winternl.h>

namespace nix {

using namespace nix::windows;

namespace windows {

namespace {

namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;

/**
 * Open a file/directory relative to a directory handle using NtCreateFile.
 *
 * @param dirFd Directory handle to open relative to
 * @param pathComponent Single path component (not a full path)
 * @param desiredAccess Access rights requested
 * @param createOptions NT create options flags
 * @param createDisposition FILE_OPEN, FILE_CREATE, etc.
 * @return Handle to the opened file/directory, or NTSTATUS on error
 */
outcome::unchecked<AutoCloseFD, NTSTATUS> maybeNtOpenAt(
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
        return outcome::failure(status);

    return AutoCloseFD{h};
}

AutoCloseFD ntOpenAt(
    Descriptor dirFd,
    std::wstring_view pathComponent,
    ACCESS_MASK desiredAccess,
    ULONG createOptions,
    ULONG createDisposition = FILE_OPEN)
{
    auto result = maybeNtOpenAt(dirFd, pathComponent, desiredAccess, createOptions, createDisposition);
    if (!result)
        throw WinError(
            RtlNtStatusToDosError(result.error()), "opening %s relative to directory handle", PathFmt(pathComponent));
    return std::move(result.value());
}

/**
 * Open a symlink relative to a directory handle without following it.
 *
 * @param dirFd Directory handle to open relative to
 * @param path Relative path to the symlink
 * @return Handle to the symlink
 */
AutoCloseFD openSymlinkAt(Descriptor dirFd, const std::filesystem::path & path)
{
    assert(path.is_relative());
    assert(!path.empty());

    auto wpath = path.lexically_normal().make_preferred();
    return ntOpenAt(dirFd, wpath.c_str(), FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN_REPARSE_POINT);
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
 * Write symlink target to an open file handle using reparse point.
 *
 * @param handle Open file handle (must have GENERIC_WRITE access)
 * @param target The symlink target (what it points to)
 */
void writeSymlinkTarget(HANDLE handle, const std::filesystem::path & target)
{
    /* Build the reparse data buffer for a symbolic link.
       Layout: SubstituteName and PrintName stored consecutively in PathBuffer.
       We use the same string for both. */
    size_t targetBytes = target.native().size() * sizeof(wchar_t);
    size_t bufSize = offsetof(ReparseDataBuffer, SymbolicLinkReparseBuffer.PathBuffer) + targetBytes * 2;
    std::vector<uint8_t> buf(bufSize, 0);

    auto * reparse = reinterpret_cast<ReparseDataBuffer *>(buf.data());
    reparse->ReparseTag = IO_REPARSE_TAG_SYMLINK;
    reparse->ReparseDataLength =
        static_cast<unsigned short>(bufSize - offsetof(ReparseDataBuffer, SymbolicLinkReparseBuffer));
    reparse->Reserved = 0;

    auto & symlink = reparse->SymbolicLinkReparseBuffer;
    /* SubstituteName comes first */
    symlink.SubstituteNameOffset = 0;
    symlink.SubstituteNameLength = static_cast<unsigned short>(targetBytes);
    /* PrintName follows SubstituteName */
    symlink.PrintNameOffset = static_cast<unsigned short>(targetBytes);
    symlink.PrintNameLength = static_cast<unsigned short>(targetBytes);
    /* SYMLINK_FLAG_RELATIVE = 1 for relative symlinks, 0 for absolute */
    symlink.Flags = target.is_relative() ? 1 : 0;

    /* Copy target into PathBuffer twice (SubstituteName and PrintName) */
    memcpy(symlink.PathBuffer, target.c_str(), targetBytes);
    memcpy(reinterpret_cast<char *>(symlink.PathBuffer) + targetBytes, target.c_str(), targetBytes);

    DWORD bytesReturned;
    if (!DeviceIoControl(
            handle,
            FSCTL_SET_REPARSE_POINT,
            buf.data(),
            static_cast<DWORD>(bufSize),
            nullptr,
            0,
            &bytesReturned,
            nullptr))
        throw WinError("setting reparse point for symlink");
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

OsString readLinkAt(Descriptor dirFd, const std::filesystem::path & path)
{
    auto linkHandle = openSymlinkAt(dirFd, path);
    return readSymlinkTarget(linkHandle.get());
}

void createFileSymlinkAt(Descriptor dirFd, const std::filesystem::path & path, const OsString & target)
{
    assert(path.is_relative());
    assert(!path.empty());

    auto wpath = path.lexically_normal().make_preferred();

    /* Create the file that will become the symlink */
    auto handle = ntOpenAt(
        dirFd, wpath.c_str(), GENERIC_WRITE | DELETE, FILE_NON_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT, FILE_CREATE);

    writeSymlinkTarget(handle.get(), target);
}

void createDirectorySymlinkAt(Descriptor dirFd, const std::filesystem::path & path, const OsString & target)
{
    assert(path.is_relative());
    assert(!path.empty());

    auto wpath = path.lexically_normal().make_preferred();

    /* Create the directory that will become the symlink */
    auto handle = ntOpenAt(
        dirFd, wpath.c_str(), GENERIC_WRITE | DELETE, FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT, FILE_CREATE);

    writeSymlinkTarget(handle.get(), target);
}

void createUnknownSymlinkAt(Descriptor dirFd, const std::filesystem::path & path, const OsString & target)
{
    assert(path.is_relative());
    assert(!path.empty());

    std::filesystem::path targetPath(target);
    bool isDirectory = false;

    if (targetPath.is_absolute()) {
        /* For absolute targets, use std::filesystem::status directly */
        std::error_code ec;
        auto status = std::filesystem::status(targetPath, ec);
        isDirectory = !ec && std::filesystem::is_directory(status);
    } else {
        /* For relative targets, the target is relative to the symlink's parent directory.
           Open the parent directory first, then try to open the target relative to it. */
        Descriptor parentFd = dirFd;
        AutoCloseFD parentFdOwned;

        auto parentPath = path.parent_path();
        if (!parentPath.empty()) {
            /* Open the parent directory of the symlink */
            auto wparent = parentPath.lexically_normal().make_preferred();
            parentFdOwned = ntOpenAt(dirFd, wparent.c_str(), FILE_TRAVERSE | SYNCHRONIZE, FILE_DIRECTORY_FILE);
            parentFd = parentFdOwned.get();
        }

        /* Try to open the target as a directory and verify with fstat. */
        auto result = maybeNtOpenAt(
            parentFd, targetPath.make_preferred().wstring(), FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_DIRECTORY_FILE);
        if (result) {
            auto st = fstat(result.value().get());
            isDirectory = S_ISDIR(st.st_mode);
        }
    }

    if (isDirectory)
        createDirectorySymlinkAt(dirFd, path, target);
    else
        createFileSymlinkAt(dirFd, path, target);
}

outcome::unchecked<AutoCloseFD, std::error_code>
openDirectoryAt(Descriptor dirFd, const std::filesystem::path & path, bool create)
{
    assert(path.is_relative());
    assert(!path.empty());

    auto wpath = path.lexically_normal().make_preferred();

    auto result = maybeNtOpenAt(
        dirFd, wpath.c_str(), FILE_TRAVERSE | SYNCHRONIZE, FILE_DIRECTORY_FILE, create ? FILE_CREATE : FILE_OPEN);
    if (result)
        return std::move(result.value());

    return outcome::failure(std::error_code(RtlNtStatusToDosError(result.error()), std::system_category()));
}

PosixStat fstat(Descriptor fd)
{
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(fd, &info))
        throw WinError("getting file information for %s", PathFmt(descriptorToPath(fd)));

    PosixStat st;
    windows::statFromFileInfo(
        st,
        info.dwFileAttributes,
        info.ftCreationTime,
        info.ftLastAccessTime,
        info.ftLastWriteTime,
        info.nFileSizeHigh,
        info.nFileSizeLow,
        info.nNumberOfLinks);

    return st;
}

PosixStat fstatat(Descriptor dirFd, const std::filesystem::path & path)
{
    assert(path.is_relative());
    assert(!path.empty());

    auto wpath = path.lexically_normal().make_preferred();

    /* Open the file without following symlinks */
    auto handle = ntOpenAt(dirFd, wpath.c_str(), FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN_REPARSE_POINT);

    return fstat(handle.get());
}

std::optional<PosixStat> maybeFstatat(Descriptor dirFd, const std::filesystem::path & path)
{
    assert(path.is_relative());
    assert(!path.empty());

    auto wpath = path.lexically_normal().make_preferred();

    auto result = maybeNtOpenAt(dirFd, wpath.c_str(), FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN_REPARSE_POINT);
    if (result)
        return fstat(result.value().get());

    auto lastError = RtlNtStatusToDosError(result.error());
    if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND)
        return std::nullopt;
    throw WinError(lastError, "getting status of %s", PathFmt(descriptorToPath(dirFd) / path));
}

AutoCloseFD openFileEnsureBeneathNoSymlinks(
    Descriptor dirFd,
    const std::filesystem::path & path,
    ACCESS_MASK desiredAccess,
    ULONG createOptions,
    ULONG createDisposition)
{
    assert(path.is_relative());
    assert(!path.empty());

    AutoCloseFD parentFd;
    auto components = std::vector<std::filesystem::path>(path.begin(), path.end());
    assert(!components.empty());
    auto getParentFd = [&]() { return parentFd ? parentFd.get() : dirFd; };

    /* Helper to construct path from components up to (and including) the given index */
    auto pathUpTo = [&](size_t idx) {
        std::filesystem::path result;
        for (size_t i = 0; i <= idx; ++i)
            result /= components[i];
        return result;
    };

    /* Helper to check if a component is a symlink and throw SymlinkNotAllowed if so */
    auto throwIfSymlink = [&](std::wstring_view component, const std::filesystem::path & pathForError) {
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
    for (size_t i = 0; i + 1 < components.size(); ++i) {
        std::wstring wcomponent = components[i].wstring();

        /* Open directory without following symlinks */
        auto result = maybeNtOpenAt(
            getParentFd(),
            wcomponent,
            FILE_TRAVERSE | SYNCHRONIZE,                  // Just need traversal rights
            FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT // Open directory, don't follow symlinks
        );
        if (!result) {
            auto lastError = RtlNtStatusToDosError(result.error());
            /* Check if this is because it's a symlink */
            if (lastError == ERROR_CANT_ACCESS_FILE || lastError == ERROR_ACCESS_DENIED) {
                throwIfSymlink(wcomponent, pathUpTo(i));
            }
            throw WinError(lastError, "opening directory component '%s'", PathFmt(pathUpTo(i)));
        }

        /* Check if what we opened is actually a symlink */
        if (isReparsePoint(result.value().get())) {
            throw SymlinkNotAllowed(pathUpTo(i));
        }

        parentFd = std::move(result.value());
    }

    /* Now open the final component with requested flags */
    std::wstring finalComponent = components.back().wstring();

    auto finalResult = maybeNtOpenAt(
        getParentFd(),
        finalComponent,
        desiredAccess,
        createOptions | FILE_OPEN_REPARSE_POINT, // Don't follow symlinks on final component either
        createDisposition);
    if (!finalResult) {
        auto lastError = RtlNtStatusToDosError(finalResult.error());
        /* Check if final component is a symlink when we requested to not follow it */
        if (lastError == ERROR_CANT_ACCESS_FILE) {
            throwIfSymlink(finalComponent, path);
        }
        /* Return invalid handle for ENOENT/EEXIST style errors that caller may want to handle */
        if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_FILE_EXISTS)
            return AutoCloseFD{};
        throw WinError(lastError, "opening file '%s'", PathFmt(path));
    }

    /* Final check: did we accidentally open a symlink? */
    if (isReparsePoint(finalResult.value().get()))
        throw SymlinkNotAllowed(path);

    return std::move(finalResult.value());
}

} // namespace nix
