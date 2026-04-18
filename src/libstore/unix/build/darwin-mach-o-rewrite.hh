#pragma once
///@file

#include <cstddef>
#include <filesystem>

namespace nix {

/**
 * Recompute Mach-O `LC_CODE_SIGNATURE` page hashes for any pages whose
 * stored hash disagrees with the on-disk page contents. Handles both
 * SHA-256 and SHA-1 CodeDirectories (pre-2016 binaries ship both as
 * alternates, and the kernel validates every CD at page-in). Every
 * other byte — including the `linker-signed` flag and the original
 * page size — is preserved, so the same input bytes always yield the
 * same output bytes.
 *
 * Walks recursively if `path` is a directory; processes a single file
 * otherwise. Symlinks are never followed. Handles thin Mach-O and both
 * fat32 and fat64 containers; other files are no-ops.
 *
 * @param path Output path just moved into place. The caller must hold
 *   it exclusively — `registerOutputs` does, after the builder exits.
 * @return Number of regular files that were rewritten.
 *
 * Throws `SysError` on read/write failure of a file we've begun
 * rewriting.
 *
 * See nixpkgs#507531 / NixOS/nix#6065.
 */
size_t fixupMachoPageHashes(const std::filesystem::path & path);

} // namespace nix
