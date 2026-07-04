#include "nix/store/macho-signature.hh"

#include "nix/store/references.hh"
#include "nix/util/base-nix-32.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fmt.hh"
#include "nix/util/logging.hh"

#include <array>
#include <cstdint>
#include <cstring>

namespace nix {

namespace {

/* Mach-O and code-signing constants, mirrored from Apple's
   `<mach-o/loader.h>`, `<mach-o/fat.h>` and the xnu source
   (`bsd/sys/codesign.h`). Vendored rather than included so that the
   detection works identically when cross-building darwin binaries on
   other platforms. (The container magics live in
   the header.) */
constexpr uint32_t lcCodeSignature = 0x1d; // LC_CODE_SIGNATURE

constexpr uint32_t csMagicEmbeddedSignature = 0xfade0cc0; // CSMAGIC_EMBEDDED_SIGNATURE
constexpr uint32_t csMagicCodeDirectory = 0xfade0c02;     // CSMAGIC_CODEDIRECTORY
constexpr uint32_t csMagicBlobWrapper = 0xfade0b01;       // CSMAGIC_BLOBWRAPPER
constexpr uint32_t csSlotSignature = 0x10000;             // CSSLOT_SIGNATURESLOT

constexpr uint8_t csHashTypeSha1 = 1;
constexpr uint8_t csHashTypeSha256 = 2;
constexpr uint8_t csHashSizeSha1 = 20;
constexpr uint8_t csHashSizeSha256 = 32;

/* Upper bound on the CodeDirectory page-size exponent. Apple emits
   12 (4 KiB) for linker-signed binaries and 14 (16 KiB) for
   `codesign(1)`. */
constexpr uint8_t maxPageSizeLog2 = 16;

/* Header sizes. `mach_header` is 28 bytes, `mach_header_64` is 32
   (one trailing `reserved` field). `fat_header` is 8; `fat_arch` is
   20 and `fat_arch_64` 32. */
constexpr size_t machHeaderSize32 = 28;
constexpr size_t machHeaderSize64 = 32;
constexpr size_t fatHeaderSize = 8;
constexpr size_t fatArchSize32 = 20;
constexpr size_t fatArchSize64 = 32;
constexpr size_t loadCommandSize = 8;          // cmd + cmdsize
constexpr size_t linkeditDataCommandSize = 16; // cmd + cmdsize + dataoff + datasize

/* Bound on a fat container's `nfat_arch`, to keep the walk over
   untrusted bytes finite. Real universal binaries carry a handful of
   slices, but xnu accepts more, so the bound is generous. Non-Mach-O
   files sharing the fat magic (Java `.class` files — their version
   field reads as `nfat_arch`) are rejected by the per-slice
   validation below: their "slices" don't carry Mach-O magic. */
constexpr uint32_t maxNFatArch = 128;

/* Bound on a SuperBlob's `count` field, to keep the blob-index walk
   over untrusted bytes finite. */
constexpr uint32_t maxSuperBlobCount = 16;

uint32_t rdLE32(const uint8_t * p)
{
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v; // all supported hosts are little-endian; same as Mach-O headers on disk
}

uint32_t rdBE32(const uint8_t * p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint64_t rdBE64(const uint8_t * p)
{
    return (uint64_t(rdBE32(p)) << 32) | uint64_t(rdBE32(p + 4));
}

/**
 * Classify the signature (if any) of the Mach-O slice starting at
 * `sliceBase`. Returns `None` for a malformed or unsigned slice.
 */
MachOSignatureKind detectSlice(std::string_view contents, size_t sliceBase)
{
    const auto * bytes = reinterpret_cast<const uint8_t *>(contents.data());
    size_t size = contents.size();

    if (sliceBase + machHeaderSize32 > size)
        return MachOSignatureKind::None;

    uint32_t magic = rdLE32(bytes + sliceBase);
    size_t headerSize;
    if (magic == machMagic64)
        headerSize = machHeaderSize64;
    else if (magic == machMagic32)
        headerSize = machHeaderSize32;
    else
        return MachOSignatureKind::None;

    if (sliceBase + headerSize > size)
        return MachOSignatureKind::None;

    /* `ncmds` at offset 16, `sizeofcmds` at 20 — same for 32/64-bit. */
    uint32_t ncmds = rdLE32(bytes + sliceBase + 16);
    uint32_t sizeofcmds = rdLE32(bytes + sliceBase + 20);
    if (sliceBase + headerSize + sizeofcmds > size)
        return MachOSignatureKind::None;

    size_t lcOff = sliceBase + headerSize;
    size_t lcEnd = lcOff + sizeofcmds;
    uint32_t sigOff = 0, sigSize = 0;
    bool found = false;
    for (uint32_t i = 0; i < ncmds && !found; i++) {
        if (lcOff + loadCommandSize > lcEnd)
            return MachOSignatureKind::None;
        uint32_t cmd = rdLE32(bytes + lcOff);
        uint32_t cmdsize = rdLE32(bytes + lcOff + 4);
        if (cmdsize < loadCommandSize || lcOff + cmdsize > lcEnd)
            return MachOSignatureKind::None;
        if (cmd == lcCodeSignature) {
            if (cmdsize < linkeditDataCommandSize)
                return MachOSignatureKind::None;
            sigOff = rdLE32(bytes + lcOff + 8);
            sigSize = rdLE32(bytes + lcOff + 12);
            found = true;
        }
        lcOff += cmdsize;
    }
    if (!found)
        return MachOSignatureKind::None;

    /* The signature is present. Peek into the SuperBlob to tell an
       ad-hoc signature from a CMS-signed one; if the blob doesn't
       parse, report `AdHoc` — the rewrite would still invalidate it. */
    constexpr size_t superBlobHeaderSize = 12; // magic + length + count
    constexpr size_t blobIndexSize = 8;        // type + offset
    size_t sbAbs = sliceBase + sigOff;
    if (sigSize < superBlobHeaderSize || sbAbs + sigSize > size || sbAbs + sigSize < sbAbs)
        return MachOSignatureKind::AdHoc;
    if (rdBE32(bytes + sbAbs) != csMagicEmbeddedSignature)
        return MachOSignatureKind::AdHoc;

    uint32_t sbCount = rdBE32(bytes + sbAbs + 8);
    if (sbCount > maxSuperBlobCount)
        return MachOSignatureKind::AdHoc;

    for (uint32_t bi = 0; bi < sbCount; bi++) {
        size_t entryOff = sbAbs + superBlobHeaderSize + size_t(bi) * blobIndexSize;
        if (entryOff + blobIndexSize > sbAbs + sigSize)
            break;
        if (rdBE32(bytes + entryOff) != csSlotSignature)
            continue;
        uint32_t blobRel = rdBE32(bytes + entryOff + 4);
        size_t blobAbs = sbAbs + blobRel;
        if (blobRel > sigSize || blobAbs + 8 > sbAbs + sigSize)
            continue;
        if (rdBE32(bytes + blobAbs) != csMagicBlobWrapper)
            continue;
        /* An empty 8-byte wrapper is what ad-hoc `codesign(1)` leaves
           in place; anything larger is a PKCS#7 chain. */
        if (rdBE32(bytes + blobAbs + 4) > 8)
            return MachOSignatureKind::Cms;
    }

    return MachOSignatureKind::AdHoc;
}

/**
 * Whether `contents` contains any of `hashParts` as a substring.
 * `hashParts` members are 32-char nix-base-32 strings; candidate
 * positions are found the same way `RefScanSink` does.
 */
bool containsAnyHash(std::string_view contents, const StringSet & hashParts)
{
    constexpr size_t refLength = 32; // StorePath::HashLen
    for (size_t i = 0; i + refLength <= contents.size();) {
        bool candidate = true;
        for (size_t j = refLength; j-- > 0;) {
            if (!BaseNix32::lookupReverse(contents[i + j])) {
                i += j + 1;
                candidate = false;
                break;
            }
        }
        if (!candidate)
            continue;
        if (hashParts.contains(std::string{contents.substr(i, refLength)}))
            return true;
        ++i;
    }
    return false;
}

/* A fat slice is valid if it fits strictly between the fat_arch
   array and EOF, and has non-zero size. Subtraction form so the
   addition can't wrap on fat64's u64 offset/size fields. */
bool validSliceBounds(size_t sliceOff, size_t sliceSize, size_t archArrayEnd, size_t fileSize)
{
    if (sliceSize == 0)
        return false;
    if (sliceOff < archArrayEnd)
        return false;
    if (sliceOff >= fileSize)
        return false;
    if (sliceSize > fileSize - sliceOff)
        return false;
    return true;
}

} // namespace

MachOSignatureKind detectMachOSignature(std::string_view contents)
{
    if (contents.size() < machHeaderSize32)
        return MachOSignatureKind::None;

    const auto * bytes = reinterpret_cast<const uint8_t *>(contents.data());
    uint32_t magicLE = rdLE32(bytes);
    uint32_t magicBE = rdBE32(bytes);

    /* Byte-swapped magics (MH_CIGAM etc.) are deliberately not
       handled: they only occur in PowerPC-era big-endian binaries,
       which no supported macOS can execute. */
    if (magicLE == machMagic32 || magicLE == machMagic64)
        return detectSlice(contents, 0);

    if (magicBE != fatMagic32 && magicBE != fatMagic64)
        return MachOSignatureKind::None;

    const bool is64 = magicBE == fatMagic64;
    const size_t archSize = is64 ? fatArchSize64 : fatArchSize32;

    uint32_t nfat = rdBE32(bytes + 4);
    if (nfat == 0 || nfat > maxNFatArch)
        return MachOSignatureKind::None;

    size_t archArrayEnd = fatHeaderSize + size_t(nfat) * archSize;
    if (archArrayEnd > contents.size())
        return MachOSignatureKind::None;

    auto result = MachOSignatureKind::None;
    for (uint32_t i = 0; i < nfat; i++) {
        /* offset at byte 8 of each entry (u32 in fat_arch, u64 in
           fat_arch_64), size at byte 12 / 16. The bounds rule must be
           the same one the repair walker uses: a slice the detector
           reports but the repair tool would skip could pass a check
           it was never given. */
        size_t archOff = fatHeaderSize + size_t(i) * archSize;
        size_t sliceOff = is64 ? rdBE64(bytes + archOff + 8) : rdBE32(bytes + archOff + 8);
        size_t sliceSize = is64 ? rdBE64(bytes + archOff + 16) : rdBE32(bytes + archOff + 12);
        if (!validSliceBounds(sliceOff, sliceSize, archArrayEnd, contents.size()))
            continue;
        auto kind = detectSlice(contents, sliceOff);
        if (kind > result)
            result = kind;
    }
    return result;
}

/**
 * Walk `root` (regular file, or directory recursively; symlinks
 * skipped), collecting signed Mach-O files. With a non-null
 * `hashParts`, only files containing one of the hashes are reported.
 */
static std::vector<MachOSignatureRewriteHit> scanImpl(const std::filesystem::path & root, const StringSet * hashParts)
{
    std::vector<MachOSignatureRewriteHit> hits;

    auto scanFile = [&](const std::filesystem::path & path) {
        std::error_code ec;
        auto sz = std::filesystem::file_size(path, ec);
        if (ec || sz < machHeaderSize32)
            return;

        /* Peek the magic before loading the file — most files in a
           build output are not Mach-O. `readOffset` uses `pread`,
           which leaves the descriptor's offset at 0 for the full
           `readFile(fd)` below; a plain `read` here would shift
           every subsequent parse by four bytes. */
        AutoCloseFD fd = openFileReadonly(path);
        if (!fd)
            return;
        std::array<std::byte, 4> peek;
        if (readOffset(fd.get(), 0, peek) != peek.size())
            return;
        const auto * peekBytes = reinterpret_cast<const uint8_t *>(peek.data());
        uint32_t magicLE = rdLE32(peekBytes);
        uint32_t magicBE = rdBE32(peekBytes);
        if (magicLE != machMagic32 && magicLE != machMagic64 && magicBE != fatMagic32 && magicBE != fatMagic64)
            return;

        /* A Mach-O file too large to inspect could still carry a
           signature — report it as a hit rather than silently
           letting it through. But when we know which hashes the
           rewrite would substitute, stream-scan for them first, so
           an oversized binary that doesn't contain any of them (and
           is therefore untouched by the rewrite) doesn't cause a
           spurious refusal. */
        if (sz > maxMachOFileSize) {
            if (hashParts) {
                RefScanSink refScan{StringSet{*hashParts}};
                drainFD(fd.get(), refScan);
                if (refScan.getResult().empty())
                    return;
            }
            hits.push_back({path, MachOSignatureKind::Unchecked});
            return;
        }

        auto contents = readFile(fd.get());
        auto kind = detectMachOSignature(contents);
        if (kind == MachOSignatureKind::None)
            return;
        if (hashParts && !containsAnyHash(contents, *hashParts))
            return;
        hits.push_back({path, kind});
    };

    std::error_code ec;
    auto st = std::filesystem::symlink_status(root, ec);
    if (ec || std::filesystem::is_symlink(st))
        return hits;

    if (std::filesystem::is_regular_file(st)) {
        scanFile(root);
        return hits;
    }

    if (!std::filesystem::is_directory(st))
        return hits;

    auto it = std::filesystem::recursive_directory_iterator(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    auto end = std::filesystem::recursive_directory_iterator();
    for (; it != end; it.increment(ec)) {
        if (ec) {
            debug("scanForMachOSignatureRewrites: %s: directory iteration error: %s", PathFmt(root), ec.message());
            ec.clear();
            continue;
        }
        std::error_code sec;
        auto est = it->symlink_status(sec);
        if (sec || std::filesystem::is_symlink(est))
            continue;
        if (std::filesystem::is_regular_file(est))
            scanFile(it->path());
    }
    return hits;
}

std::vector<MachOSignatureRewriteHit>
scanForMachOSignatureRewrites(const std::filesystem::path & root, const StringSet & hashParts)
{
    if (hashParts.empty())
        return {};
    return scanImpl(root, &hashParts);
}

} // namespace nix
