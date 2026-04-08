#pragma once
///@file

#include <cstddef>
#include <filesystem>

namespace nix {

/**
 * Recompute Mach-O `LC_CODE_SIGNATURE` page hashes for any pages whose
 * stored SHA-256 disagrees with the actual on-disk page contents,
 * preserving every other byte of the file (including the
 * `linker-signed` flag and the original page size).
 *
 * This is the bit-reproducible repair for outputs whose `__TEXT,__cstring`
 * pages were mutated by `RewritingSink` after the linker had already
 * ad-hoc-signed them. The same input bytes always yield the same output
 * bytes, unlike re-signing via `codesign(1)` (which switches to a 16 KiB
 * page size and clears `linker-signed`).
 *
 * Walks recursively if `path` is a directory; processes a single file
 * otherwise. Symlinks are never followed. Files that are not Mach-O,
 * are unsigned, use unsupported hash types, or already have hashes that
 * match their pages are no-ops.
 *
 * @param path Output path that has just been moved into place. The caller
 *   must own this path exclusively (no concurrent mutators); within
 *   `DerivationBuilderImpl::registerOutputs` this is guaranteed because
 *   the builder process has exited and the daemon has not yet handed
 *   the path off.
 * @return Number of regular files that were rewritten.
 *
 * Throws `SysError` if a file we have committed to rewriting cannot be
 * read or written, or `Error` if a Mach-O variant we cannot fix up
 * (e.g. 64-bit fat) is encountered.
 *
 * See nixpkgs#507531 / NixOS/nix#6065.
 */
size_t fixupMachoPageHashes(const std::filesystem::path & path);

} // namespace nix
