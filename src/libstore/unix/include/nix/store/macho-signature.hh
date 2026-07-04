#pragma once
///@file

#include "nix/util/types.hh"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace nix {

/**
 * Classify the wait-status of a `nix __fixup-macho --check` child:
 * exit 0 = every signature is valid, exit 2 = at least one file has
 * stale page hashes or a signature that could not be verified,
 * anything else = the check itself failed. Every caller interprets
 * it through this one definition, keeping them in lockstep with the
 * tool's exit contract.
 */
enum struct MachOCheckOutcome { Valid, Stale, Error };

MachOCheckOutcome classifyMachOCheck(int waitStatus);

/* Mach-O container magics. Thin headers are host-endian (little on
   every supported platform); fat headers are big-endian on disk.
   Defined here so the cheap `hasMachOMagic` peek and the full parser
   in macho-signature.cc share one source of truth. */
constexpr uint32_t machMagic32 = 0xfeedface; // MH_MAGIC
constexpr uint32_t machMagic64 = 0xfeedfacf; // MH_MAGIC_64
constexpr uint32_t fatMagic32 = 0xcafebabe;  // FAT_MAGIC (big-endian on disk)
constexpr uint32_t fatMagic64 = 0xcafebabf;  // FAT_MAGIC_64

/* Largest file the parser will read into memory. No darwin build
   output carries a Mach-O binary this big in practice; a larger
   file is reported unparsed rather than inspected. Shared by the
   daemon-side scan and the `nix __fixup-macho` tool so the two
   agree. */
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
 * Whether the first four bytes at `p` identify a possible Mach-O
 * (thin, either width, or a fat container). Used to peek files
 * cheaply before reading them whole. `p` must point to at least
 * four readable bytes; callers peek a fixed 4-byte buffer.
 */
inline bool hasMachOMagic(const unsigned char * p)
{
    uint32_t le;
    __builtin_memcpy(&le, p, 4);
    uint32_t be = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    return le == machMagic32 || le == machMagic64 || be == fatMagic32 || be == fatMagic64;
}

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

/**
 * Recompute the code-signature page hashes of the Mach-O file
 * `contents` in place — or, with `checkOnly`, just report whether
 * any stored hash disagrees with the page contents. Returns true iff
 * at least one hash slot was (or would be) rewritten; in check mode,
 * a signature that is present but cannot be verified (unsupported
 * hash type, malformed CodeDirectory) also returns true, since the
 * callers that fail closed on this result must not take "could not
 * parse" for "valid". Handles thin, fat32 and fat64 containers; both
 * SHA-256 and SHA-1 CodeDirectories are recomputed when present,
 * since the kernel validates every one at page-in. Only stale hash
 * slots are modified; every other byte is preserved, so the repair
 * is deterministic: the same input bytes always yield the same
 * output bytes.
 *
 * Throws when asked to *repair* (not check) a slice carrying a
 * non-empty CMS signature — the signer's certificate chain commits
 * to the CodeDirectory, so a recompute would produce a
 * differently-broken binary while hiding the reason.
 *
 * This function does no I/O and does not drop privileges; it is the
 * engine of `nix __fixup-macho`, which the daemon execs as the build
 * user wherever untrusted bytes must be repaired. It lives here so
 * that the detection and repair share one parser (and one test
 * suite); code placement does not change the execution surface — the
 * daemon itself only ever calls the read-only detection entry points
 * above.
 *
 * DO NOT call this from daemon code that runs as root: it parses (and
 * for repair, writes) bytes produced by untrusted builders and
 * substituters, and its whole point is to run only in the exec'd,
 * privilege-dropped `nix __fixup-macho` child. The daemon's contract
 * is detect (read-only, in-process) → exec the child to repair.
 *
 * `path` is used in diagnostics only.
 */
bool fixupMachOSignature(std::string & contents, const std::filesystem::path & path, bool checkOnly);

} // namespace nix
