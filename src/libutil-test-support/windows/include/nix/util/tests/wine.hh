#pragma once
///@file

#include <filesystem>

namespace nix::windows {

/**
 * Whether Wine can create symlinks in @p dir.
 *
 * Wine stores reparse points, and hence symlinks, in a `user.WINEREPARSE`
 * extended attribute on the host file system. So creating one fails wherever
 * `user.*` extended attributes are not supported. This may be because a
 * specific file system (implementation) doesn't support them, e.g. tmpfs
 * before Linux 6.6, or because of a blanket prohibition on these operations,
 * such as the one the Nix sandbox currently imposes with seccomp.
 *
 * Within a Windows build we cannot easily "break out of Wine" and try the
 * underlying xattr operations, so we just try making a symlink.
 * Unfortunately Wine reports failing to set the xattr only as a generic
 * failure, so another failure, e.g. from @p dir not being writable, would
 * also lead this function to conclude that symlinks are not supported.
 *
 * Only meaningful under Wine; see `isWine()`.
 */
bool wineSymlinksSupported(const std::filesystem::path & dir);

} // namespace nix::windows
