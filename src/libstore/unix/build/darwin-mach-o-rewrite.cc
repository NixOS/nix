#ifdef __APPLE__

#  include "darwin-mach-o-rewrite.hh"

#  include "nix/util/error.hh"
#  include "nix/util/file-descriptor.hh"
#  include "nix/util/file-system.hh"
#  include "nix/util/hash.hh"
#  include "nix/util/logging.hh"

#  include <mach-o/fat.h>
#  include <mach-o/loader.h>

#  include <algorithm>
#  include <array>
#  include <cstdint>
#  include <cstring>
#  include <string>
#  include <string_view>
#  include <utility>

namespace nix {

namespace {

/* CS_* code-signing blob constants. Apple does not ship a userspace
   header for these, so we mirror the names from the xnu source
   (`bsd/sys/codesign.h`). Big-endian on disk regardless of host. */
constexpr uint32_t CSMAGIC_EMBEDDED_SIGNATURE = 0xfade0cc0;
constexpr uint32_t CSMAGIC_CODEDIRECTORY = 0xfade0c02;
constexpr uint32_t CSMAGIC_BLOBWRAPPER = 0xfade0b01;

constexpr uint8_t CS_HASHTYPE_SHA1 = 1;
constexpr uint8_t CS_HASHTYPE_SHA256 = 2;
constexpr uint8_t CS_HASHSIZE_SHA1 = 20;
constexpr uint8_t CS_HASHSIZE_SHA256 = 32;

/* SuperBlob slot type for an embedded CMS signature. An empty 8-byte
   `CSMAGIC_BLOBWRAPPER` placeholder is left in place by Apple's
   `codesign(1)` on ad-hoc signing and is safe to process. A non-empty
   payload is a PKCS#7 chain (Developer ID / App Store) that commits
   to the CodeDirectory's hash: recomputing any page-hash slot would
   invalidate the signer's cdhash commitment, and the re-signer's
   identity is not recoverable from inside the daemon. */
constexpr uint32_t CSSLOT_SIGNATURESLOT = 0x10000;

/* Bound on the in-memory size of files we process. No darwin build
   output carries a Mach-O binary this big in practice. */
constexpr size_t maxFileSize = 512 * 1024 * 1024;

/* Upper bound on the CodeDirectory page size exponent. Apple emits 12
   (4 KiB) for linker-signed binaries and 14 (16 KiB) for `codesign(1)`;
   16 is the practical ceiling. */
constexpr uint8_t maxPageSizeLog2 = 16;

/* Bound on a fat container's `nfat_arch`. Java `.class` files share
   the `0xcafebabe` magic and their major.minor version reads as a
   large `nfat`, so without this check a class file reaches the per-
   slice loop with 65+ garbage entries. */
constexpr uint32_t maxNFatArch = 16;

/* Bound on a SuperBlob's `count` field. An unbounded `count` with
   matching `CSMAGIC_CODEDIRECTORY` entries inside a 512 MiB file
   could drive the per-CodeDirectory loop to recompute the same
   slot region many times over. */
constexpr uint32_t maxSuperBlobCount = 16;

/* On-disk width of fat_arch (20 bytes) and fat_arch_64 (32 bytes).
   We read fields by offset rather than via Apple's host-ordered
   structs; the static_asserts catch any future SDK padding change
   at compile time. */
constexpr size_t fatArchSize32 = 20;
constexpr size_t fatArchSize64 = 32;
static_assert(sizeof(fat_arch) == fatArchSize32);
static_assert(sizeof(fat_arch_64) == fatArchSize64);

uint32_t rdBE32(const uint8_t * p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint64_t rdBE64(const uint8_t * p)
{
    return (uint64_t(rdBE32(p)) << 32) | uint64_t(rdBE32(p + 4));
}

/* A fat slice is valid if it fits strictly between the fat_arch
   array and EOF, and has non-zero size. */
bool validSliceBounds(size_t sliceOff, size_t sliceSize, size_t archArrayEnd, size_t fileSize)
{
    if (sliceSize == 0)
        return false;
    if (sliceOff < archArrayEnd)
        return false;
    if (sliceOff >= fileSize)
        return false;
    /* Subtraction form so the addition can't wrap on fat64's u64
       offset/size fields. */
    if (sliceSize > fileSize - sliceOff)
        return false;
    return true;
}

/**
 * Process one Mach-O slice starting at `sliceBase` inside `data`.
 * Mutates `data` in place. Returns `true` iff at least one CodeDirectory
 * hash slot was rewritten.
 */
bool fixupSlice(std::string & data, size_t sliceBase, const std::filesystem::path & path)
{
    /* `data` is never resized past this point, so `bytes` stays valid. */
    auto * bytes = reinterpret_cast<uint8_t *>(data.data());

    if (sliceBase + sizeof(mach_header) > data.size())
        return false;

    /* Mach-O header fields are host-ordered. memcpy-into-local avoids
       UB from a reinterpret_cast through a non-aliasing pointer. */
    mach_header mhProbe;
    std::memcpy(&mhProbe, bytes + sliceBase, sizeof(mhProbe));

    bool is64;
    if (mhProbe.magic == MH_MAGIC_64)
        is64 = true;
    else if (mhProbe.magic == MH_MAGIC)
        is64 = false;
    else
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

    /* `LC_CODE_SIGNATURE.dataoff` is slice-relative (zero for thin). */
    constexpr size_t superBlobHeaderSize = 12; // magic + length + count
    constexpr size_t blobIndexSize = 8;        // type + offset
    size_t sbAbs = sliceBase + sigOff;
    if (sbAbs + superBlobHeaderSize > data.size() || sbAbs + sigSize > data.size())
        return false;

    if (rdBE32(bytes + sbAbs) != CSMAGIC_EMBEDDED_SIGNATURE)
        return false;

    uint32_t sbCount = rdBE32(bytes + sbAbs + 8);
    if (sbCount > maxSuperBlobCount) {
        warn("fixupMachoPageHashes: %s: implausible SuperBlob count %d, skipping", PathFmt(path), int(sbCount));
        return false;
    }

    /* Pre-scan for a non-empty CMS signature blob and skip the slice if
       present — see the comment on `CSSLOT_SIGNATURESLOT` above. */
    for (uint32_t bi = 0; bi < sbCount; bi++) {
        size_t entryOff = sbAbs + superBlobHeaderSize + size_t(bi) * blobIndexSize;
        if (entryOff + blobIndexSize > sbAbs + sigSize)
            break;
        if (rdBE32(bytes + entryOff) != CSSLOT_SIGNATURESLOT)
            continue;
        uint32_t blobRel = rdBE32(bytes + entryOff + 4);
        size_t blobAbs = sbAbs + blobRel;
        if (blobAbs + 8 > sbAbs + sigSize)
            continue;
        if (rdBE32(bytes + blobAbs) != CSMAGIC_BLOBWRAPPER)
            continue;
        uint32_t blobLen = rdBE32(bytes + blobAbs + 4);
        if (blobLen > 8) {
            warn(
                "fixupMachoPageHashes: %s: SuperBlob carries a non-empty CMS signature (%d bytes), skipping",
                PathFmt(path),
                int(blobLen));
            return false;
        }
    }

    bool modified = false;

    /* Older binaries carry SHA-1 + SHA-256 alternate CodeDirectories. */
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

        /* Bound the CodeDirectory against the SuperBlob — the later
           slot-region check would miss a malformed `cdLength` that
           overruns sibling blob metadata while keeping slots in file. */
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

        /* Pre-2016 binaries carry SHA-1 + SHA-256 alternate CDs in one
           SuperBlob. The kernel validates every CD at page-in, so
           fixing just one leaves the other stale and the binary
           SIGKILLs at load. */
        HashAlgorithm hashAlgo;
        uint8_t expectedHashSize;
        if (hashType == CS_HASHTYPE_SHA256) {
            hashAlgo = HashAlgorithm::SHA256;
            expectedHashSize = CS_HASHSIZE_SHA256;
        } else if (hashType == CS_HASHTYPE_SHA1) {
            hashAlgo = HashAlgorithm::SHA1;
            expectedHashSize = CS_HASHSIZE_SHA1;
        } else {
            warn(
                "fixupMachoPageHashes: %s: CodeDirectory hashType=%d not supported, skipping",
                PathFmt(path),
                int(hashType));
            continue;
        }
        if (hashSize != expectedHashSize) {
            warn(
                "fixupMachoPageHashes: %s: CodeDirectory hashType=%d has hashSize=%d, skipping",
                PathFmt(path),
                int(hashType),
                int(hashSize));
            continue;
        }
        if (pageSizeLog2 == 0 || pageSizeLog2 > maxPageSizeLog2) {
            warn("fixupMachoPageHashes: %s: unsupported pageSizeLog2=%d, skipping", PathFmt(path), int(pageSizeLog2));
            continue;
        }

        /* Special slots hash other blobs at negative indices from
           `hashOffset`, so they must fit below the code-slot region. */
        if (size_t(nSpecialSlots) * hashSize > hashOffset) {
            warn(
                "fixupMachoPageHashes: %s: nSpecialSlots=%d overflows hashOffset=%d, skipping",
                PathFmt(path),
                nSpecialSlots,
                hashOffset);
            continue;
        }

        size_t pageSize = size_t(1) << pageSizeLog2;

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
            /* size_t-promoted before the `+1` to avoid uint32 wraparound. */
            size_t pageStart = sliceBase + size_t(i) * pageSize;
            size_t pageEndUnclamped = sliceBase + (size_t(i) + 1) * pageSize;
            size_t pageEndLimit = sliceBase + size_t(codeLimit);
            size_t pageEnd = std::min(pageEndUnclamped, pageEndLimit);
            if (pageEnd > data.size() || pageEnd < pageStart) {
                warn("fixupMachoPageHashes: %s: page %d out of bounds, skipping", PathFmt(path), int(i));
                continue;
            }
            std::string_view sv(data.data() + pageStart, pageEnd - pageStart);
            Hash h = hashString(hashAlgo, sv);
            uint8_t * slot = bytes + slotsAbs + size_t(i) * hashSize;
            if (std::memcmp(slot, h.hash, hashSize) != 0) {
                std::memcpy(slot, h.hash, hashSize);
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
 * rewriting.
 */
size_t fixupFile(const std::filesystem::path & path)
{
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    if (ec || sz < sizeof(mach_header))
        return 0;
    if (sz > maxFileSize)
        return 0;

    /* Peek the 4-byte magic before loading the whole file. The helper
       runs on every regular file in every darwin build output closure,
       most of which are not Mach-O. `readOffset` uses `pread`, which
       leaves the fd offset at 0 for the full `readFile(fd)` below. */
    AutoCloseFD fd = openFileReadonly(path);
    if (!fd)
        return 0;
    std::array<std::byte, 4> peek;
    if (readOffset(fd.get(), 0, peek) != peek.size())
        return 0;
    const auto * peekBytes = reinterpret_cast<const uint8_t *>(peek.data());
    uint32_t magicLE;
    std::memcpy(&magicLE, peekBytes, sizeof(magicLE));
    uint32_t magicBE = rdBE32(peekBytes);
    if (magicLE != MH_MAGIC && magicLE != MH_MAGIC_64 && magicBE != FAT_MAGIC && magicBE != FAT_MAGIC_64)
        return 0;

    std::string data = readFile(fd.get());

    auto * bytes = reinterpret_cast<const uint8_t *>(data.data());

    bool modified = false;

    if (magicLE == MH_MAGIC || magicLE == MH_MAGIC_64) {
        modified = fixupSlice(data, 0, path);
    } else if (magicBE == FAT_MAGIC || magicBE == FAT_MAGIC_64) {
        /* fat32 and fat64 share the same fat_header (magic + nfat_arch,
           u32 BE). The per-arch table differs: fat32 has u32 offset and
           size; fat64 has u64. */
        const bool is64 = (magicBE == FAT_MAGIC_64);
        const size_t archSize = is64 ? fatArchSize64 : fatArchSize32;

        const uint32_t nfat = rdBE32(bytes + 4);
        if (nfat == 0 || nfat > maxNFatArch)
            return 0;

        const size_t archArrayEnd = sizeof(fat_header) + size_t(nfat) * archSize;
        if (archArrayEnd > data.size())
            return 0;

        /* fat_arch entry fields (BE on disk):
             fat_arch:    cputype@0 cpusubtype@4 offset@8  (u32) size@12 (u32) align@16
             fat_arch_64: cputype@0 cpusubtype@4 offset@8  (u64) size@16 (u64) align@24 reserved@28 */
        for (uint32_t i = 0; i < nfat; i++) {
            const size_t archOff = sizeof(fat_header) + size_t(i) * archSize;
            const auto [sliceOff, sliceSize] = [&]() -> std::pair<size_t, size_t> {
                if (is64)
                    return {rdBE64(bytes + archOff + 8), rdBE64(bytes + archOff + 16)};
                return {rdBE32(bytes + archOff + 8), rdBE32(bytes + archOff + 12)};
            }();

            if (!validSliceBounds(sliceOff, sliceSize, archArrayEnd, data.size())) {
                warn(
                    "fixupMachoPageHashes: %s: fat_arch[%d] slice bounds invalid "
                    "(offset=%d size=%d file=%d), skipping",
                    PathFmt(path),
                    int(i),
                    int(sliceOff),
                    int(sliceSize),
                    int(data.size()));
                continue;
            }

            if (fixupSlice(data, sliceOff, path))
                modified = true;
        }
    } else {
        return 0;
    }

    if (!modified)
        return 0;

    /* `canonicalisePathMetaData` runs next and resets perms, so 0600
       here is a safe transient. */
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

    /* The iterator doesn't recurse into directory symlinks but still
       yields them (and file symlinks) as entries, so we skip
       explicitly. */
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
