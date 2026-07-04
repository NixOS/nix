#pragma once
///@file

#include "nix/util/types.hh"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace nix {

/* Mach-O container magics. Thin headers are host-endian (little on
   every supported platform); fat headers are big-endian on disk. */
constexpr uint32_t machMagic32 = 0xfeedface; // MH_MAGIC
constexpr uint32_t machMagic64 = 0xfeedfacf; // MH_MAGIC_64
constexpr uint32_t fatMagic32 = 0xcafebabe;  // FAT_MAGIC (big-endian on disk)
constexpr uint32_t fatMagic64 = 0xcafebabf;  // FAT_MAGIC_64

/* Largest file the parser will read into memory. No darwin build
   output carries a Mach-O binary this big in practice; a larger
   file is reported unparsed rather than inspected. */
constexpr size_t maxMachOFileSize = 512 * 1024 * 1024;

/**
 * Kind of code signature a Mach-O file carries.
 */
enum struct MachOSignatureKind {
    /* No `LC_CODE_SIGNATURE` load command in any slice. */
    None,
    /* A signature without an embedded CMS blob: `ld`'s linker-signed
       ad-hoc signature, or `codesign --sign -`. Deterministically
       regenerable from the file contents alone. */
    AdHoc,
    /* A signature carrying a non-empty CMS (PKCS#7) blob — signed
       with a certificate (Developer ID, App Store). Cannot be
       regenerated without the original signing identity. */
    Cms,
    /* Mach-O magic, but the file could not be inspected (too large).
       Only produced by `scanForMachOSignatureRewrites`, which treats
       such files as potentially signed — fail closed. */
    Unchecked,
};

/**
 * Detect whether `contents` is a Mach-O file (thin, fat32 or fat64)
 * carrying a code signature, and of which kind. For fat containers
 * the strongest kind across slices is returned.
 *
 * Purely content-based — works the same on every platform, so
 * cross-builds of darwin binaries are covered too. Malformed or
 * non-Mach-O contents yield `None`; a present but unparseable
 * signature blob yields `AdHoc` (erring towards detection).
 */
MachOSignatureKind detectMachOSignature(std::string_view contents);

struct MachOSignatureRewriteHit
{
    std::filesystem::path path;
    MachOSignatureKind kind;
};

/**
 * Scan `root` (a regular file, or a directory walked recursively;
 * symlinks are never followed) for regular files that both carry a
 * Mach-O code signature and contain at least one of `hashParts` —
 * i.e. files whose upcoming store path hash rewrite would invalidate
 * their signature.
 */
std::vector<MachOSignatureRewriteHit>
scanForMachOSignatureRewrites(const std::filesystem::path & root, const StringSet & hashParts);

} // namespace nix
