#ifdef __APPLE__

#  include "darwin-mach-o-rewrite.hh"

#  include "nix/util/error.hh"
#  include "nix/util/file-system.hh"
#  include "nix/util/hash.hh"
#  include "nix/util/logging.hh"

#  include <mach-o/fat.h>
#  include <mach-o/loader.h>

#  include <algorithm>
#  include <cstdint>
#  include <cstring>
#  include <string>
#  include <string_view>

namespace nix {

namespace {

/* CS_* code-signing blob constants. Apple does not ship a userspace
   header for these, so we mirror the names from the xnu source
   (`bsd/sys/codesign.h`). Big-endian on disk regardless of host. */
constexpr uint32_t CSMAGIC_EMBEDDED_SIGNATURE = 0xfade0cc0;
constexpr uint32_t CSMAGIC_CODEDIRECTORY = 0xfade0c02;

constexpr uint8_t CS_HASHTYPE_SHA256 = 2;
constexpr uint8_t CS_HASHSIZE_SHA256 = 32;

/* Cap the size of files we will load wholesale into memory. Anything
   larger than this is implausibly a Mach-O binary in a nixpkgs build
   output and is more likely a builder's runaway artefact (or a
   resource-exhaustion attempt). */
constexpr size_t maxFileSize = 512 * 1024 * 1024;

/* Reasonable upper bound on the CodeDirectory page size exponent. Apple
   ships 12 (4 KiB) for linker-signed binaries and 14 (16 KiB) for
   `codesign(1)` output; 16 (64 KiB) is the practical maximum. */
constexpr uint8_t maxPageSizeLog2 = 16;

uint32_t rdBE32(const uint8_t * p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

/**
 * Process one Mach-O slice starting at `sliceBase` inside `data`.
 * Mutates `data` in place. Returns `true` iff at least one CodeDirectory
 * hash slot was rewritten.
 */
bool fixupSlice(std::string & data, size_t sliceBase, const std::filesystem::path & path)
{
    /* `data` is owned by the caller and is not resized anywhere in this
       function, so this pointer remains valid for the entire body. We
       intentionally cast away `char` for the byte-level reads below;
       writes through this pointer in the slot-update loop are safe for
       the same reason. */
    auto * bytes = reinterpret_cast<uint8_t *>(data.data());

    if (sliceBase + sizeof(mach_header) > data.size())
        return false;

    /* The mach_header / mach_header_64 fields are stored in native
       byte order. We memcpy into a local to avoid undefined-behaviour
       from a `reinterpret_cast` through a non-aliasing pointer. */
    mach_header mhProbe;
    std::memcpy(&mhProbe, bytes + sliceBase, sizeof(mhProbe));

    bool is64;
    if (mhProbe.magic == MH_MAGIC_64)
        is64 = true;
    else if (mhProbe.magic == MH_MAGIC)
        is64 = false;
    else
        /* Either not a Mach-O slice (e.g. random data inside a fat
           container), or a byte-swapped header from an architecture
           Apple has not shipped in 20 years. Either way, leave it. */
        return false;

    size_t headerSize = is64 ? sizeof(mach_header_64) : sizeof(mach_header);
    if (sliceBase + headerSize > data.size())
        return false;

    uint32_t ncmds;
    uint32_t sizeofcmds;
    if (is64) {
        mach_header_64 mh;
        std::memcpy(&mh, bytes + sliceBase, sizeof(mh));
        ncmds = mh.ncmds;
        sizeofcmds = mh.sizeofcmds;
    } else {
        ncmds = mhProbe.ncmds;
        sizeofcmds = mhProbe.sizeofcmds;
    }

    if (sliceBase + headerSize + sizeofcmds > data.size())
        return false;

    size_t lcOff = sliceBase + headerSize;
    size_t lcEnd = lcOff + sizeofcmds;
    bool found = false;
    uint32_t sigOff = 0;
    uint32_t sigSize = 0;
    for (uint32_t i = 0; i < ncmds; i++) {
        if (lcOff + sizeof(load_command) > lcEnd)
            return false;
        load_command lc;
        std::memcpy(&lc, bytes + lcOff, sizeof(lc));
        if (lc.cmdsize < sizeof(load_command) || lcOff + lc.cmdsize > lcEnd)
            return false;
        if (lc.cmd == LC_CODE_SIGNATURE) {
            if (lc.cmdsize < sizeof(linkedit_data_command))
                return false;
            linkedit_data_command sigCmd;
            std::memcpy(&sigCmd, bytes + lcOff, sizeof(sigCmd));
            sigOff = sigCmd.dataoff;
            sigSize = sigCmd.datasize;
            found = true;
            break;
        }
        lcOff += lc.cmdsize;
    }
    if (!found)
        return false;

    /* The signature SuperBlob lives at sliceBase + sigOff. For a thin
       Mach-O sliceBase is zero; for a fat slice the file offset stored
       in `LC_CODE_SIGNATURE` is relative to the slice, not the file. */
    constexpr size_t superBlobHeaderSize = 12; // magic + length + count
    constexpr size_t blobIndexSize = 8;        // type + offset
    size_t sbAbs = sliceBase + sigOff;
    if (sbAbs + superBlobHeaderSize > data.size() || sbAbs + sigSize > data.size())
        return false;

    if (rdBE32(bytes + sbAbs) != CSMAGIC_EMBEDDED_SIGNATURE)
        return false;

    uint32_t sbCount = rdBE32(bytes + sbAbs + 8);

    bool modified = false;

    /* Binaries can carry multiple CodeDirectories (e.g. SHA-1 + SHA-256
       alternates for older macOS); process each, skip non-SHA-256. */
    for (uint32_t bi = 0; bi < sbCount; bi++) {
        size_t entryOff = sbAbs + superBlobHeaderSize + size_t(bi) * blobIndexSize;
        if (entryOff + blobIndexSize > sbAbs + sigSize)
            break;
        uint32_t blobRel = rdBE32(bytes + entryOff + 4);
        size_t blobAbs = sbAbs + blobRel;
        if (blobAbs + 8 > sbAbs + sigSize)
            continue;

        if (rdBE32(bytes + blobAbs) != CSMAGIC_CODEDIRECTORY)
            continue;

        /* CS_CodeDirectory header through `pageSizeLog2` is 44 bytes;
           newer versions append `teamOffset`, `codeLimit64`,
           `execSegBase`, `runtime`, etc., but we never read past
           `pageSizeLog2` so older binaries are fine. */
        constexpr size_t cdHeaderSize = 44;
        if (blobAbs + cdHeaderSize > sbAbs + sigSize)
            continue;

        uint32_t cdLength = rdBE32(bytes + blobAbs + 4);

        /* Defense in depth: validate that the entire CodeDirectory blob
           lies inside the SuperBlob. The slot bounds check below catches
           the worst case (out-of-file write), but a malformed blob could
           still claim a `cdLength` that overruns the SuperBlob structure
           while leaving the slot region in-bounds. Reject early. */
        if (cdLength < cdHeaderSize || blobAbs + cdLength > sbAbs + sigSize) {
            warn("fixupMachoPageHashes: %s: CodeDirectory length out of bounds, skipping", PathFmt(path));
            continue;
        }

        uint32_t hashOffset = rdBE32(bytes + blobAbs + 16);
        uint32_t nSpecialSlots = rdBE32(bytes + blobAbs + 24);
        uint32_t nCodeSlots = rdBE32(bytes + blobAbs + 28);
        uint32_t codeLimit = rdBE32(bytes + blobAbs + 32);
        uint8_t hashSize = bytes[blobAbs + 36];
        uint8_t hashType = bytes[blobAbs + 37];
        uint8_t pageSizeLog2 = bytes[blobAbs + 39];

        if (hashType != CS_HASHTYPE_SHA256) {
            warn(
                "fixupMachoPageHashes: %s: CodeDirectory hashType=%d (not SHA-256), skipping",
                PathFmt(path),
                int(hashType));
            continue;
        }
        if (hashSize != CS_HASHSIZE_SHA256) {
            warn(
                "fixupMachoPageHashes: %s: SHA-256 CodeDirectory has hashSize=%d, skipping",
                PathFmt(path),
                int(hashSize));
            continue;
        }
        if (pageSizeLog2 == 0 || pageSizeLog2 > maxPageSizeLog2) {
            warn("fixupMachoPageHashes: %s: unsupported pageSizeLog2=%d, skipping", PathFmt(path), int(pageSizeLog2));
            continue;
        }

        /* Validate that the special-slot region (negative indices,
           hashing other blobs) lives entirely below the code-slot
           region — otherwise the CodeDirectory is corrupt. */
        if (size_t(nSpecialSlots) * hashSize > hashOffset) {
            warn(
                "fixupMachoPageHashes: %s: nSpecialSlots=%d overflows hashOffset=%d, skipping",
                PathFmt(path),
                nSpecialSlots,
                hashOffset);
            continue;
        }

        size_t pageSize = size_t(1) << pageSizeLog2;

        /* Bounds-check the entire code-slot region against both the
           CodeDirectory blob and the file. */
        size_t slotsAbs = blobAbs + hashOffset;
        size_t slotsEnd = slotsAbs + size_t(nCodeSlots) * hashSize;
        if (slotsEnd > blobAbs + cdLength || slotsEnd > data.size()) {
            warn("fixupMachoPageHashes: %s: CodeDirectory hash slots out of bounds, skipping", PathFmt(path));
            continue;
        }

        debug(
            "fixupMachoPageHashes: %s: nCodeSlots=%d pageSize=%d codeLimit=%d",
            PathFmt(path),
            nCodeSlots,
            pageSize,
            codeLimit);

        for (uint32_t i = 0; i < nCodeSlots; i++) {
            /* Promote `i` to size_t BEFORE the +1 to prevent uint32
               wraparound on hypothetical massive nCodeSlots. */
            size_t pageStart = sliceBase + size_t(i) * pageSize;
            size_t pageEndUnclamped = sliceBase + (size_t(i) + 1) * pageSize;
            size_t pageEndLimit = sliceBase + size_t(codeLimit);
            size_t pageEnd = std::min(pageEndUnclamped, pageEndLimit);
            if (pageEnd > data.size() || pageEnd < pageStart) {
                warn("fixupMachoPageHashes: %s: page %d out of bounds, skipping", PathFmt(path), i);
                continue;
            }
            std::string_view sv(data.data() + pageStart, pageEnd - pageStart);
            Hash h = hashString(HashAlgorithm::SHA256, sv);
            uint8_t * slot = bytes + slotsAbs + size_t(i) * CS_HASHSIZE_SHA256;
            if (std::memcmp(slot, h.hash, CS_HASHSIZE_SHA256) != 0) {
                std::memcpy(slot, h.hash, CS_HASHSIZE_SHA256);
                modified = true;
            }
        }
    }

    return modified;
}

/**
 * Read a regular file, fix up its Mach-O page hashes if needed, and
 * write it back. Returns 1 if the file was modified, 0 otherwise.
 *
 * Throws on read/write failure of a file we have committed to
 * rewriting, or on a Mach-O variant we cannot fix up (e.g. 64-bit
 * fat).
 */
size_t fixupFile(const std::filesystem::path & path)
{
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    if (ec || sz < sizeof(mach_header))
        return 0;
    if (sz > maxFileSize)
        return 0;

    /* `readFile` returns the file as a `std::string`. Random access on
       a flat byte buffer is the right primitive here — `Source` is
       streaming-only and our parser needs to seek into the
       `LC_CODE_SIGNATURE` blob and back. The `reinterpret_cast` to
       `uint8_t *` below is well-defined as of C++17 (`std::string::data`
       returns a pointer to the contiguous storage). */
    std::string data = readFile(path);

    auto * bytes = reinterpret_cast<const uint8_t *>(data.data());
    uint32_t magicLE;
    std::memcpy(&magicLE, bytes, sizeof(magicLE));
    uint32_t magicBE = rdBE32(bytes);

    bool modified = false;

    if (magicLE == MH_MAGIC || magicLE == MH_MAGIC_64) {
        modified = fixupSlice(data, 0, path);
    } else if (magicBE == FAT_MAGIC) {
        /* Apple's `fat_arch` struct is byte-swapped on little-endian
           hosts, so we read the big-endian fields by hand. */
        uint32_t nfat = rdBE32(bytes + 4);
        for (uint32_t i = 0; i < nfat; i++) {
            size_t archOff = sizeof(fat_header) + size_t(i) * sizeof(fat_arch);
            if (archOff + sizeof(fat_arch) > data.size())
                break;
            uint32_t sliceOff = rdBE32(reinterpret_cast<const uint8_t *>(data.data()) + archOff + 8);
            if (fixupSlice(data, sliceOff, path))
                modified = true;
        }
    } else if (magicBE == FAT_MAGIC_64) {
        /* 64-bit fat is rare in practice but real (Apple's own toolchain
           emits it for very large slices). Refusing loudly is better
           than silently leaving an unrunnable binary in the store. */
        throw Error(
            "fixupMachoPageHashes: %s: 64-bit fat Mach-O binaries are not yet supported (nixpkgs#507531 follow-up)",
            PathFmt(path));
    } else {
        return 0;
    }

    if (!modified)
        return 0;

    /* Write back. We don't bother preserving the original mode bits:
       `canonicalisePathMetaData` runs immediately after this in the
       same lambda and resets perms, uid, gid, and mtime to canonical
       values. Use `0600` as a safe transient mode that excludes
       setuid/setgid/sticky. */
    writeFile(path, std::string_view{data}, 0600);
    return 1;
}

} // namespace

size_t fixupMachoPageHashes(const std::filesystem::path & path)
{
    std::error_code ec;
    auto st = std::filesystem::symlink_status(path, ec);
    if (ec)
        return 0;

    if (std::filesystem::is_symlink(st))
        return 0;

    if (std::filesystem::is_regular_file(st))
        return fixupFile(path);

    if (!std::filesystem::is_directory(st))
        return 0;

    /* `recursive_directory_iterator` does not follow symlinks by
       default, but it does still yield file symlinks inside real
       directories — hence the explicit `is_symlink` skip below. */
    size_t count = 0;
    auto it = std::filesystem::recursive_directory_iterator(
        path, std::filesystem::directory_options::skip_permission_denied, ec);
    auto end = std::filesystem::recursive_directory_iterator();
    for (; it != end; it.increment(ec)) {
        if (ec) {
            debug(
                "fixupMachoPageHashes: %s: directory iteration error, skipping entry: %s", PathFmt(path), ec.message());
            ec.clear();
            continue;
        }
        std::error_code sec;
        auto est = it->symlink_status(sec);
        if (sec) {
            debug("fixupMachoPageHashes: %s: cannot stat entry, skipping: %s", PathFmt(it->path()), sec.message());
            continue;
        }
        if (std::filesystem::is_symlink(est))
            continue;
        if (std::filesystem::is_regular_file(est))
            count += fixupFile(it->path());
    }
    return count;
}

} // namespace nix

#endif // __APPLE__
