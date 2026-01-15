#pragma once

#ifdef __APPLE__

#include <filesystem>
#include <string>

namespace nix {

/**
 * Check if a file is a Mach-O binary (executable or dylib).
 * Returns true for both 32-bit and 64-bit Mach-O files, as well as fat binaries.
 */
bool isMachOBinary(const std::filesystem::path & path);

/**
 * Remove the code signature from a Mach-O binary using codesign --remove-signature.
 * This is used before computing content-addressed hashes to ensure
 * deterministic hashing (code signatures contain timestamps and other
 * non-deterministic data).
 *
 * Unlike zeroing the signature bytes in-place (which corrupts the signature
 * structure and makes re-signing impossible), this properly removes the
 * LC_CODE_SIGNATURE load command and truncates the signature blob.
 *
 * Does nothing if the file is not a Mach-O binary or has no code signature.
 */
void removeMachOCodeSignature(const std::filesystem::path & path);

/**
 * Re-sign a Mach-O binary with an ad-hoc signature.
 * This should be called after moving a binary to its final location
 * and after any hash rewriting has been performed.
 *
 * Uses codesign with ad-hoc signing (-s -), which doesn't require
 * any certificates.
 *
 * Does nothing if the file is not a Mach-O binary.
 */
void signMachOBinary(const std::filesystem::path & path);

/**
 * Recursively walk a directory and remove code signatures from all Mach-O binaries.
 * Used before computing CA hashes.
 */
void removeMachOCodeSignaturesRecursively(const std::filesystem::path & path);

/**
 * Recursively walk a directory and re-sign all Mach-O binaries.
 * Used after moving outputs to their final CA location.
 */
void signMachOBinariesRecursively(const std::filesystem::path & path);

} // namespace nix

#endif // __APPLE__
