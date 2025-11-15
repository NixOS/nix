#pragma once
/// @file

#include "nix/util/source-accessor.hh"
#include "nix/util/ref.hh"

#include <filesystem>

namespace nix {

/**
 * Create a directory accessor rooted at @param root.
 *
 * On unix accesses are performed with openat. Linux uses openat2 if supported
 * by the kernel (>= 5.6).
 *
 * On Windows returns a PosixSourceAccessor.
 *
 * @param fd Descriptor of the directory.
 * @param root Filesystem path corresponding to fd. Must correspond to a directory.
 */
ref<SourceAccessor>
makeDirectorySourceAccessor(AutoCloseFD fd, std::filesystem::path root, bool trackLastModified = false);

} // namespace nix
