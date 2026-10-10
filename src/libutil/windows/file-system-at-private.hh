#pragma once
///@file
///
/// Handle-relative opening, shared between the Windows `*at` implementations
/// and the Windows recursive deletion in `file-system.cc`. Not installed:
/// nothing outside `libutil`'s Windows sources should need these.

#include "nix/util/file-descriptor.hh"

#include <optional>
#include <string_view>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace nix::windows {

/**
 * Open a file or directory relative to a directory handle.
 *
 * Win32 has no `openat`, so this goes through `NtCreateFile`, whose
 * `OBJECT_ATTRIBUTES::RootDirectory` is the handle-relative primitive. That is
 * what makes it possible to walk a tree without ever re-resolving a path, and
 * so without a window in which a component could be swapped.
 *
 * @param dirFd Directory handle to open relative to.
 * @param pathComponent A single path component, not a full path.
 * @param desiredAccess Access rights requested.
 * @param createOptions NT create options. Pass `FILE_OPEN_REPARSE_POINT` to
 *   open a symlink or junction itself rather than following it.
 * @param createDisposition `FILE_OPEN`, `FILE_CREATE`, and so on.
 *
 * @return Handle to the opened object; the caller owns it.
 *
 * @throws WinError on any failure, including "not found".
 */
AutoCloseFD ntOpenAt(
    Descriptor dirFd,
    std::wstring_view pathComponent,
    ACCESS_MASK desiredAccess,
    ULONG createOptions,
    ULONG createDisposition = FILE_OPEN);

/**
 * As `ntOpenAt`, but a missing object is not an error.
 *
 * @return `std::nullopt` if the object does not exist, or if a component of
 *   the path is not a directory. Mirrors the `ENOENT` and `ENOTDIR` cases that
 *   the Unix `maybeFstatat` treats as absence rather than failure.
 *
 * @throws WinError on any other failure.
 */
std::optional<AutoCloseFD> tryNtOpenAt(
    Descriptor dirFd,
    std::wstring_view pathComponent,
    ACCESS_MASK desiredAccess,
    ULONG createOptions,
    ULONG createDisposition = FILE_OPEN);

} // namespace nix::windows
