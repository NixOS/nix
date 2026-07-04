#include "nix/store/macho-signature.hh"
#include "nix/util/error.hh"
#include "nix/util/hash.hh"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace nix {

/* The detector is content-based and endian-explicit, so these tests
   run on every platform — that is the point: Linux cross-builds of
   darwin binaries must be covered too. The byte vectors are built by
   hand from the on-disk layouts in `<mach-o/loader.h>`,
   `<mach-o/fat.h>` and xnu's `bsd/sys/codesign.h`. */

namespace {

void putLE32(std::string & s, size_t off, uint32_t v)
{
    s[off + 0] = char(v & 0xff);
    s[off + 1] = char((v >> 8) & 0xff);
    s[off + 2] = char((v >> 16) & 0xff);
    s[off + 3] = char((v >> 24) & 0xff);
}

void putBE32(std::string & s, size_t off, uint32_t v)
{
    s[off + 0] = char((v >> 24) & 0xff);
    s[off + 1] = char((v >> 16) & 0xff);
    s[off + 2] = char((v >> 8) & 0xff);
    s[off + 3] = char(v & 0xff);
}

void putBE64(std::string & s, size_t off, uint64_t v)
{
    putBE32(s, off, uint32_t(v >> 32));
    putBE32(s, off + 4, uint32_t(v & 0xffffffff));
}

constexpr uint32_t MH_MAGIC_64_ = 0xfeedfacf;
constexpr uint32_t MH_MAGIC_ = 0xfeedface;
constexpr uint32_t FAT_MAGIC_ = 0xcafebabe;
constexpr uint32_t FAT_MAGIC_64_ = 0xcafebabf;
constexpr uint32_t LC_CODE_SIGNATURE_ = 0x1d;
constexpr uint32_t CSMAGIC_EMBEDDED_SIGNATURE_ = 0xfade0cc0;
constexpr uint32_t CSMAGIC_CODEDIRECTORY_ = 0xfade0c02;
constexpr uint32_t CSMAGIC_BLOBWRAPPER_ = 0xfade0b01;
constexpr uint32_t CSSLOT_CODEDIRECTORY_ = 0;
constexpr uint32_t CSSLOT_SIGNATURESLOT_ = 0x10000;

/**
 * A minimal signed 64-bit slice: mach_header_64, one
 * LC_CODE_SIGNATURE load command, and a SuperBlob containing a
 * CodeDirectory plus (optionally) a CMS blob wrapper of `cmsLen`
 * payload bytes (0 = no signature slot, 8 = the empty ad-hoc
 * wrapper, >8 = Developer-ID-style).
 */
std::string makeSignedSlice64(size_t cmsLen)
{
    constexpr size_t headerSize = 32;
    constexpr size_t lcSize = 16; // linkedit_data_command
    bool haveCms = cmsLen > 0;
    uint32_t nBlobs = haveCms ? 2 : 1;
    size_t sbHeader = 12 + nBlobs * 8;
    size_t cdLen = 44; // CodeDirectory header only, no slots
    size_t cmsBlob = haveCms ? cmsLen : 0;
    size_t sigSize = sbHeader + cdLen + cmsBlob;
    size_t sigOff = headerSize + lcSize;

    std::string s(sigOff + sigSize, '\0');

    /* mach_header_64 */
    putLE32(s, 0, MH_MAGIC_64_);
    putLE32(s, 16, 1);      // ncmds
    putLE32(s, 20, lcSize); // sizeofcmds

    /* LC_CODE_SIGNATURE */
    putLE32(s, headerSize + 0, LC_CODE_SIGNATURE_);
    putLE32(s, headerSize + 4, lcSize);
    putLE32(s, headerSize + 8, uint32_t(sigOff));
    putLE32(s, headerSize + 12, uint32_t(sigSize));

    /* SuperBlob */
    putBE32(s, sigOff + 0, CSMAGIC_EMBEDDED_SIGNATURE_);
    putBE32(s, sigOff + 4, uint32_t(sigSize));
    putBE32(s, sigOff + 8, nBlobs);
    putBE32(s, sigOff + 12, CSSLOT_CODEDIRECTORY_);
    putBE32(s, sigOff + 16, uint32_t(sbHeader));
    if (haveCms) {
        putBE32(s, sigOff + 20, CSSLOT_SIGNATURESLOT_);
        putBE32(s, sigOff + 24, uint32_t(sbHeader + cdLen));
    }

    /* CodeDirectory (header only) */
    size_t cdOff = sigOff + sbHeader;
    putBE32(s, cdOff + 0, CSMAGIC_CODEDIRECTORY_);
    putBE32(s, cdOff + 4, uint32_t(cdLen));

    /* CMS blob wrapper */
    if (haveCms) {
        size_t cmsOff = cdOff + cdLen;
        putBE32(s, cmsOff + 0, CSMAGIC_BLOBWRAPPER_);
        putBE32(s, cmsOff + 4, uint32_t(cmsLen));
    }

    return s;
}

/** An unsigned 64-bit slice with a single non-signature load command. */
std::string makeUnsignedSlice64()
{
    constexpr size_t headerSize = 32;
    constexpr size_t lcSize = 16;
    std::string s(headerSize + lcSize, '\0');
    putLE32(s, 0, MH_MAGIC_64_);
    putLE32(s, 16, 1);
    putLE32(s, 20, lcSize);
    putLE32(s, headerSize + 0, 0x32); // LC_SOURCE_VERSION
    putLE32(s, headerSize + 4, lcSize);
    return s;
}

/** Wrap slices into a fat container (32- or 64-bit fat headers). */
std::string makeFat(const std::vector<std::string> & slices, bool fat64)
{
    size_t archSize = fat64 ? 32 : 20;
    size_t tableEnd = 8 + slices.size() * archSize;
    /* Align each slice to 16 bytes for tidiness (not required by the
       detector). */
    std::vector<size_t> offsets;
    size_t cur = (tableEnd + 15) & ~size_t(15);
    for (auto & sl : slices) {
        offsets.push_back(cur);
        cur += (sl.size() + 15) & ~size_t(15);
    }
    std::string s(cur, '\0');
    putBE32(s, 0, fat64 ? FAT_MAGIC_64_ : FAT_MAGIC_);
    putBE32(s, 4, uint32_t(slices.size()));
    for (size_t i = 0; i < slices.size(); i++) {
        size_t archOff = 8 + i * archSize;
        if (fat64) {
            putBE64(s, archOff + 8, offsets[i]);
            putBE64(s, archOff + 16, slices[i].size());
        } else {
            putBE32(s, archOff + 8, uint32_t(offsets[i]));
            putBE32(s, archOff + 12, uint32_t(slices[i].size()));
        }
        s.replace(offsets[i], slices[i].size(), slices[i]);
    }
    return s;
}

} // namespace

TEST(detectMachOSignature, emptyAndTiny)
{
    EXPECT_EQ(detectMachOSignature(""), MachOSignatureKind::None);
    EXPECT_EQ(detectMachOSignature("not a mach-o file at all"), MachOSignatureKind::None);
}

TEST(detectMachOSignature, unsignedThin)
{
    EXPECT_EQ(detectMachOSignature(makeUnsignedSlice64()), MachOSignatureKind::None);
}

TEST(detectMachOSignature, adHocThin)
{
    /* No signature slot at all — the linker-signed shape. */
    EXPECT_EQ(detectMachOSignature(makeSignedSlice64(0)), MachOSignatureKind::AdHoc);
    /* Empty 8-byte blob wrapper — the `codesign -s -` shape. */
    EXPECT_EQ(detectMachOSignature(makeSignedSlice64(8)), MachOSignatureKind::AdHoc);
}

TEST(detectMachOSignature, cmsThin)
{
    EXPECT_EQ(detectMachOSignature(makeSignedSlice64(100)), MachOSignatureKind::Cms);
}

TEST(detectMachOSignature, thin32BitHeader)
{
    /* 32-bit mach_header (28 bytes) followed by LC_CODE_SIGNATURE
       whose SuperBlob is out of bounds — still detected as signed
       (fail towards detection). */
    std::string s(28 + 16, '\0');
    putLE32(s, 0, MH_MAGIC_);
    putLE32(s, 16, 1);
    putLE32(s, 20, 16);
    putLE32(s, 28 + 0, LC_CODE_SIGNATURE_);
    putLE32(s, 28 + 4, 16);
    putLE32(s, 28 + 8, 0xffff0000); // dataoff far beyond EOF
    putLE32(s, 28 + 12, 64);
    EXPECT_EQ(detectMachOSignature(s), MachOSignatureKind::AdHoc);
}

TEST(detectMachOSignature, fat32Container)
{
    auto fat = makeFat({makeUnsignedSlice64(), makeSignedSlice64(0)}, false);
    EXPECT_EQ(detectMachOSignature(fat), MachOSignatureKind::AdHoc);
}

TEST(detectMachOSignature, fat64Container)
{
    auto fat = makeFat({makeSignedSlice64(100), makeUnsignedSlice64()}, true);
    EXPECT_EQ(detectMachOSignature(fat), MachOSignatureKind::Cms);
}

TEST(detectMachOSignature, fatStrongestKindWins)
{
    auto fat = makeFat({makeSignedSlice64(0), makeSignedSlice64(100)}, false);
    EXPECT_EQ(detectMachOSignature(fat), MachOSignatureKind::Cms);
}

TEST(detectMachOSignature, javaClassFile)
{
    /* Java class files share the fat magic; the version field reads
       as nfat_arch. A small one fails the arch-array bounds check. */
    std::string s(32, '\0');
    putBE32(s, 0, 0xcafebabe);
    putBE32(s, 4, 65); // "nfat_arch" = major version 65 (Java 21)
    EXPECT_EQ(detectMachOSignature(s), MachOSignatureKind::None);

    /* A large one has room for the phantom arch table, but its
       garbage "slices" carry no Mach-O magic. */
    std::string big(8192, '\x5a');
    putBE32(big, 0, 0xcafebabe);
    putBE32(big, 4, 65);
    EXPECT_EQ(detectMachOSignature(big), MachOSignatureKind::None);
}

TEST(detectMachOSignature, absurdNFatArch)
{
    /* nfat_arch beyond any real universal binary is rejected by the
       bound. */
    std::string s(1 << 20, '\0');
    putBE32(s, 0, FAT_MAGIC_);
    putBE32(s, 4, 100000);
    EXPECT_EQ(detectMachOSignature(s), MachOSignatureKind::None);
}

TEST(detectMachOSignature, fatWithZeroArches)
{
    std::string s(32, '\0');
    putBE32(s, 0, FAT_MAGIC_);
    putBE32(s, 4, 0);
    EXPECT_EQ(detectMachOSignature(s), MachOSignatureKind::None);
}

TEST(detectMachOSignature, truncatedLoadCommands)
{
    /* sizeofcmds runs past EOF. */
    std::string s(32, '\0');
    putLE32(s, 0, MH_MAGIC_64_);
    putLE32(s, 16, 4);
    putLE32(s, 20, 0xffffff);
    EXPECT_EQ(detectMachOSignature(s), MachOSignatureKind::None);
}

TEST(detectMachOSignature, zeroSizeLoadCommand)
{
    /* cmdsize = 0 must not loop forever. */
    auto s = makeUnsignedSlice64();
    putLE32(s, 32 + 4, 0);
    EXPECT_EQ(detectMachOSignature(s), MachOSignatureKind::None);
}

TEST(detectMachOSignature, fatSliceOffsetsOutOfBounds)
{
    /* Slice offsets pointing outside the file are skipped. */
    std::string s(8 + 20, '\0');
    putBE32(s, 0, FAT_MAGIC_);
    putBE32(s, 4, 1);
    putBE32(s, 8 + 8, 0x7fffffff); // offset beyond EOF
    putBE32(s, 8 + 12, 64);
    EXPECT_EQ(detectMachOSignature(s), MachOSignatureKind::None);
}

TEST(detectMachOSignature, fat64HugeOffsetNoOverflow)
{
    /* A fat64 slice offset near UINT64_MAX must not wrap into
       bounds. */
    std::string s(8 + 32, '\0');
    putBE32(s, 0, FAT_MAGIC_64_);
    putBE32(s, 4, 1);
    putBE64(s, 8 + 8, ~uint64_t(0) - 8);
    putBE64(s, 8 + 16, 1024);
    EXPECT_EQ(detectMachOSignature(s), MachOSignatureKind::None);
}

/* -------------------------------------------------------------------
   Repair engine (`fixupMachOSignature`) — the same fixtures, but with
   real hash slots whose recompute we can verify against hashString.
   ------------------------------------------------------------------- */

namespace {

constexpr uint8_t CS_HASHTYPE_SHA1_ = 1;
constexpr uint8_t CS_HASHTYPE_SHA256_ = 2;

struct CdSpec
{
    uint8_t hashType;
    uint8_t hashSize;
};

/**
 * A signed slice whose CodeDirectories carry real page-hash slots.
 * Layout: mach_header_64, LC_CODE_SIGNATURE, `pages` pages of
 * repeated 'A' content padding up to the signature, then a SuperBlob
 * with one CodeDirectory per `cds` entry (plus optionally a CMS
 * wrapper of `cmsLen` bytes). Page size 4096 (pageSizeLog2 = 12);
 * codeLimit = sigOff (everything before the signature is hashed,
 * like real binaries). Slots are initialized to ZERO — i.e. stale —
 * so a repair must rewrite every one.
 */
std::string makeRepairableSlice(const std::vector<CdSpec> & cds, size_t pages, size_t cmsLen = 0)
{
    constexpr size_t headerSize = 32;
    constexpr size_t lcSize = 16;
    constexpr size_t pageSize = 4096;

    size_t sigOff = pages * pageSize; // codeLimit; header+lc live inside page 0
    uint32_t nCodeSlots = uint32_t(pages);

    bool haveCms = cmsLen > 0;
    uint32_t nBlobs = uint32_t(cds.size()) + (haveCms ? 1 : 0);
    size_t sbHeader = 12 + nBlobs * 8;

    std::vector<size_t> cdLens;
    size_t cdsTotal = 0;
    for (auto & cd : cds) {
        size_t len = 44 + size_t(nCodeSlots) * cd.hashSize;
        cdLens.push_back(len);
        cdsTotal += len;
    }
    size_t sigSize = sbHeader + cdsTotal + (haveCms ? cmsLen : 0);

    std::string s(sigOff + sigSize, 'A');

    /* mach_header_64 */
    putLE32(s, 0, MH_MAGIC_64_);
    putLE32(s, 16, 1);
    putLE32(s, 20, lcSize);

    /* LC_CODE_SIGNATURE */
    putLE32(s, headerSize + 0, LC_CODE_SIGNATURE_);
    putLE32(s, headerSize + 4, lcSize);
    putLE32(s, headerSize + 8, uint32_t(sigOff));
    putLE32(s, headerSize + 12, uint32_t(sigSize));

    /* SuperBlob */
    putBE32(s, sigOff + 0, CSMAGIC_EMBEDDED_SIGNATURE_);
    putBE32(s, sigOff + 4, uint32_t(sigSize));
    putBE32(s, sigOff + 8, nBlobs);
    size_t cursor = sbHeader;
    for (size_t i = 0; i < cds.size(); i++) {
        putBE32(s, sigOff + 12 + i * 8, CSSLOT_CODEDIRECTORY_ + uint32_t(i ? 0x1000 + i : 0)); // alternate slots
        putBE32(s, sigOff + 16 + i * 8, uint32_t(cursor));
        cursor += cdLens[i];
    }
    if (haveCms) {
        putBE32(s, sigOff + 12 + cds.size() * 8, CSSLOT_SIGNATURESLOT_);
        putBE32(s, sigOff + 16 + cds.size() * 8, uint32_t(cursor));
    }

    /* CodeDirectories: zeroed (stale) hash slots */
    cursor = sbHeader;
    for (size_t i = 0; i < cds.size(); i++) {
        size_t cdOff = sigOff + cursor;
        putBE32(s, cdOff + 0, CSMAGIC_CODEDIRECTORY_);
        putBE32(s, cdOff + 4, uint32_t(cdLens[i]));
        putBE32(s, cdOff + 16, 44);               // hashOffset
        putBE32(s, cdOff + 24, 0);                // nSpecialSlots
        putBE32(s, cdOff + 28, nCodeSlots);       // nCodeSlots
        putBE32(s, cdOff + 32, uint32_t(sigOff)); // codeLimit
        s[cdOff + 36] = char(cds[i].hashSize);    // hashSize
        s[cdOff + 37] = char(cds[i].hashType);    // hashType
        s[cdOff + 39] = 12;                       // pageSizeLog2
        for (size_t j = 0; j < size_t(nCodeSlots) * cds[i].hashSize; j++)
            s[cdOff + 44 + j] = '\0';
        cursor += cdLens[i];
    }

    if (haveCms) {
        size_t cmsOff = sigOff + cursor;
        putBE32(s, cmsOff + 0, CSMAGIC_BLOBWRAPPER_);
        putBE32(s, cmsOff + 4, uint32_t(cmsLen));
    }

    return s;
}

/** Verify every hash slot of every CD in `s` against a recompute. */
void expectAllSlotsValid(const std::string & s, const std::vector<CdSpec> & cds, size_t pages)
{
    constexpr size_t pageSize = 4096;
    size_t sigOff = pages * pageSize;
    size_t sbHeader = 12 + (cds.size() + 0) * 8;
    /* Recompute sbHeader accounting for a possible CMS entry: read
       the actual blob count from the SuperBlob instead. */
    uint32_t nBlobs = (uint32_t(uint8_t(s[sigOff + 8])) << 24) | (uint32_t(uint8_t(s[sigOff + 9])) << 16)
                      | (uint32_t(uint8_t(s[sigOff + 10])) << 8) | uint32_t(uint8_t(s[sigOff + 11]));
    sbHeader = 12 + nBlobs * 8;

    size_t cursor = sbHeader;
    for (auto & cd : cds) {
        size_t cdOff = sigOff + cursor;
        for (size_t i = 0; i < pages; i++) {
            std::string_view page(s.data() + i * pageSize, pageSize);
            Hash h = hashString(cd.hashType == CS_HASHTYPE_SHA256_ ? HashAlgorithm::SHA256 : HashAlgorithm::SHA1, page);
            EXPECT_EQ(std::memcmp(s.data() + cdOff + 44 + i * cd.hashSize, h.hash, cd.hashSize), 0)
                << "stale slot: cd hashType=" << int(cd.hashType) << " page " << i;
        }
        cursor += 44 + pages * cd.hashSize;
    }
}

} // namespace

TEST(fixupMachOSignature, repairsStaleSha256Slots)
{
    std::vector<CdSpec> cds{{CS_HASHTYPE_SHA256_, 32}};
    auto s = makeRepairableSlice(cds, 3);
    EXPECT_TRUE(fixupMachOSignature(s, "test", false));
    expectAllSlotsValid(s, cds, 3);
    /* Idempotent: a second run finds nothing stale. */
    EXPECT_FALSE(fixupMachOSignature(s, "test", false));
    /* And check mode agrees. */
    EXPECT_FALSE(fixupMachOSignature(s, "test", true));
}

TEST(fixupMachOSignature, repairsDualSha1Sha256CodeDirectories)
{
    /* Pre-2016 binaries carry SHA-1 + SHA-256 alternates; the kernel
       validates every one, so both must be recomputed. */
    std::vector<CdSpec> cds{{CS_HASHTYPE_SHA256_, 32}, {CS_HASHTYPE_SHA1_, 20}};
    auto s = makeRepairableSlice(cds, 2);
    EXPECT_TRUE(fixupMachOSignature(s, "test", false));
    expectAllSlotsValid(s, cds, 2);
    EXPECT_FALSE(fixupMachOSignature(s, "test", true));
}

TEST(fixupMachOSignature, checkModeReportsWithoutModifying)
{
    std::vector<CdSpec> cds{{CS_HASHTYPE_SHA256_, 32}};
    auto s = makeRepairableSlice(cds, 2);
    auto before = s;
    EXPECT_TRUE(fixupMachOSignature(s, "test", true));
    EXPECT_EQ(s, before);
}

TEST(fixupMachOSignature, repairPreservesNonSlotBytes)
{
    std::vector<CdSpec> cds{{CS_HASHTYPE_SHA256_, 32}};
    auto s = makeRepairableSlice(cds, 2);
    auto before = s;
    EXPECT_TRUE(fixupMachOSignature(s, "test", false));
    /* Only the hash-slot region may differ. */
    constexpr size_t pageSize = 4096;
    size_t sigOff = 2 * pageSize;
    size_t sbHeader = 12 + 1 * 8;
    size_t slotsBegin = sigOff + sbHeader + 44;
    size_t slotsEnd = slotsBegin + 2 * 32;
    EXPECT_EQ(s.substr(0, slotsBegin), before.substr(0, slotsBegin));
    EXPECT_EQ(s.substr(slotsEnd), before.substr(slotsEnd));
}

TEST(fixupMachOSignature, throwsOnCmsRepair)
{
    std::vector<CdSpec> cds{{CS_HASHTYPE_SHA256_, 32}};
    auto s = makeRepairableSlice(cds, 2, /*cmsLen=*/100);
    EXPECT_THROW(fixupMachOSignature(s, "test", false), Error);
    /* ...but check mode verifies it like any other slice. */
    EXPECT_TRUE(fixupMachOSignature(s, "test", true));
}

TEST(fixupMachOSignature, emptyCmsWrapperIsRepairable)
{
    /* The empty 8-byte wrapper ad-hoc `codesign` leaves in place is
       not a real CMS signature. */
    std::vector<CdSpec> cds{{CS_HASHTYPE_SHA256_, 32}};
    auto s = makeRepairableSlice(cds, 2, /*cmsLen=*/8);
    EXPECT_TRUE(fixupMachOSignature(s, "test", false));
    expectAllSlotsValid(s, cds, 2);
}

TEST(fixupMachOSignature, lastPageClampedToCodeLimit)
{
    /* codeLimit truncated into the middle of the last page: the last
       slot hashes only up to codeLimit, matching xnu. */
    std::vector<CdSpec> cds{{CS_HASHTYPE_SHA256_, 32}};
    auto s = makeRepairableSlice(cds, 2);
    constexpr size_t pageSize = 4096;
    size_t sigOff = 2 * pageSize;
    size_t sbHeader = 12 + 1 * 8;
    size_t cdOff = sigOff + sbHeader;
    uint32_t truncatedLimit = uint32_t(sigOff - 100);
    putBE32(s, cdOff + 32, truncatedLimit);
    EXPECT_TRUE(fixupMachOSignature(s, "test", false));

    Hash h0 = hashString(HashAlgorithm::SHA256, std::string_view(s.data(), pageSize));
    EXPECT_EQ(std::memcmp(s.data() + cdOff + 44, h0.hash, 32), 0);
    Hash h1 = hashString(HashAlgorithm::SHA256, std::string_view(s.data() + pageSize, truncatedLimit - pageSize));
    EXPECT_EQ(std::memcmp(s.data() + cdOff + 44 + 32, h1.hash, 32), 0);
}

TEST(fixupMachOSignature, repairsFatContainer)
{
    /* The fat-dispatch path repairs each signed slice independently. */
    std::vector<CdSpec> cds{{CS_HASHTYPE_SHA256_, 32}};
    auto slice = makeRepairableSlice(cds, 2);
    auto fat = makeFat({slice, slice}, false);
    EXPECT_TRUE(fixupMachOSignature(fat, "test", false));
    EXPECT_FALSE(fixupMachOSignature(fat, "test", true));
    auto fat64 = makeFat({slice}, true);
    EXPECT_TRUE(fixupMachOSignature(fat64, "test", false));
    EXPECT_FALSE(fixupMachOSignature(fat64, "test", true));
}

TEST(fixupMachOSignature, nSpecialSlotsOverflowGuard)
{
    /* When nSpecialSlots * hashSize exceeds hashOffset — the special
       (negative-index) slots would overrun the code-slot region — the
       CodeDirectory is skipped rather than misparsed. The slice's
       stale slots are then left untouched. */
    std::vector<CdSpec> cds{{CS_HASHTYPE_SHA256_, 32}};
    auto s = makeRepairableSlice(cds, 2);
    constexpr size_t pageSize = 4096;
    size_t cdOff = 2 * pageSize + (12 + 8);
    /* hashOffset is 44; 2 * 32 = 64 > 44 -> guard fires, CD skipped. */
    putBE32(s, cdOff + 24, 2); // nSpecialSlots
    auto before = s;
    EXPECT_FALSE(fixupMachOSignature(s, "test", false)); // skipped, nothing written
    EXPECT_EQ(s, before);
}

TEST(fixupMachOSignature, unsupportedHashTypeSkipped)
{
    /* A CodeDirectory with an unknown hashType is left alone (warned),
       not crashed or misrepaired. */
    std::vector<CdSpec> cds{{CS_HASHTYPE_SHA256_, 32}};
    auto s = makeRepairableSlice(cds, 2);
    constexpr size_t pageSize = 4096;
    size_t cdOff = 2 * pageSize + (12 + 8);
    s[cdOff + 37] = char(99); // hashType = bogus
    EXPECT_FALSE(fixupMachOSignature(s, "test", false));
}

TEST(fixupMachOSignature, checkModeFailsOnUnverifiableSignature)
{
    /* A signature the engine cannot verify must not pass the check:
       the callers that fail closed on the check's word (the build
       door's post-repair re-check) would otherwise accept a binary
       whose signature was never actually verified. */
    std::vector<CdSpec> cds{{CS_HASHTYPE_SHA256_, 32}};
    constexpr size_t pageSize = 4096;
    size_t cdOff = 2 * pageSize + (12 + 8);

    {
        /* Unsupported hash type (SHA-384-shaped). */
        auto s = makeRepairableSlice(cds, 2);
        s[cdOff + 37] = char(3);
        EXPECT_TRUE(fixupMachOSignature(s, "test", true));
    }
    {
        /* Unsupported page-size exponent. */
        auto s = makeRepairableSlice(cds, 2);
        s[cdOff + 39] = char(20);
        EXPECT_TRUE(fixupMachOSignature(s, "test", true));
    }
    {
        /* CodeDirectory length out of bounds. */
        auto s = makeRepairableSlice(cds, 2);
        putBE32(s, cdOff + 4, 0xffffffff);
        EXPECT_TRUE(fixupMachOSignature(s, "test", true));
    }
    {
        /* A signature declared by the load command whose SuperBlob
           doesn't parse at all. */
        auto s = makeRepairableSlice(cds, 2);
        putBE32(s, 2 * pageSize, 0xdeadbeef);
        EXPECT_TRUE(fixupMachOSignature(s, "test", true));
    }
    {
        /* Blob-index offset pointing outside the signature region. */
        auto s = makeRepairableSlice(cds, 2);
        putBE32(s, 2 * pageSize + 16, 0xffffff00);
        EXPECT_TRUE(fixupMachOSignature(s, "test", true));
    }
    {
        /* hashSize inconsistent with the declared hash type. */
        auto s = makeRepairableSlice(cds, 2);
        s[cdOff + 36] = char(48);
        EXPECT_TRUE(fixupMachOSignature(s, "test", true));
    }
    {
        /* A SuperBlob that parses but contains no CodeDirectory:
           a signature nothing can verify. The detector reports such
           a file as a hit, so the check must not call it valid. */
        auto s = makeRepairableSlice(cds, 2);
        putBE32(s, cdOff, 0xfade0b01); // CD blob magic -> blob wrapper
        EXPECT_TRUE(fixupMachOSignature(s, "test", true));
    }
}

TEST(fixupMachOSignature, fatSliceBoundsMatchDetection)
{
    /* The detector and the repair walker must agree on which fat
       slices exist: a slice one walks and the other skips could pass
       a check it was never given. A zero-size arch entry is invalid
       to both. */
    std::vector<CdSpec> cds{{CS_HASHTYPE_SHA256_, 32}};
    auto slice = makeRepairableSlice(cds, 2);
    auto fat = makeFat({slice}, false);
    putBE32(fat, 8 + 12, 0); // fat_arch.size = 0
    EXPECT_EQ(detectMachOSignature(fat), MachOSignatureKind::None);
    EXPECT_FALSE(fixupMachOSignature(fat, "test", true));
}

TEST(fixupMachOSignature, repairSkippingUnsupportedCdStillFailsCheck)
{
    /* The OPEN-1 shape end to end at the engine level: one supported
       CodeDirectory and one with an unsupported hash type. The repair
       fixes the supported one and returns true (it modified slots),
       but the follow-up check must still fail — the unsupported CD
       was skipped, not verified, and its slots may be stale. */
    std::vector<CdSpec> dual{{CS_HASHTYPE_SHA256_, 32}, {CS_HASHTYPE_SHA1_, 20}};
    auto s = makeRepairableSlice(dual, 2);
    constexpr size_t pageSize = 4096;
    size_t sha1CdOff = 2 * pageSize + (12 + 2 * 8) + (44 + 2 * 32);
    s[sha1CdOff + 37] = char(3); // hashType = SHA-384 (unsupported)

    EXPECT_TRUE(fixupMachOSignature(s, "test", false));
    EXPECT_TRUE(fixupMachOSignature(s, "test", true));
}

TEST(fixupMachOSignature, nonMachOUntouched)
{
    std::string s(64, 'x');
    auto before = s;
    EXPECT_FALSE(fixupMachOSignature(s, "test", false));
    EXPECT_EQ(s, before);
}

/* Both entry points parse bytes produced by untrusted builders and
   substituters, so memory safety on malformed input is a correctness
   property. Feed seeded mutations and truncations of valid signed,
   fat, and bare-header fixtures through detection and repair; the
   invariant is that they never read out of bounds, which the
   sanitizer build turns into a failure. A fixed LCG keeps the corpus
   reproducible. */
TEST(machOSignatureFuzz, mutationsNeverCrash)
{
    std::vector<CdSpec> cds{{CS_HASHTYPE_SHA256_, 32}};
    const std::string base = makeRepairableSlice(cds, 4, /*cmsLen=*/16);
    /* Also fuzz a fat container and a bare header. */
    std::vector<std::string> seeds{
        base,
        makeFat({base, makeUnsignedSlice64()}, false),
        makeFat({base}, true),
        makeSignedSlice64(100),
        std::string(40, '\0'),
    };

    uint64_t rng = 0x9e3779b97f4a7c15ull;
    auto next = [&] {
        rng = rng * 6364136223846793005ull + 1442695040888963407ull;
        return uint32_t(rng >> 33);
    };

    for (const auto & seed : seeds) {
        for (int iter = 0; iter < 4000; iter++) {
            std::string s = seed;
            if (!s.empty()) {
                /* 1–4 byte pokes. */
                int pokes = 1 + int(next() % 4);
                for (int k = 0; k < pokes; k++)
                    s[next() % s.size()] = char(next() & 0xff);
                /* Occasionally truncate. */
                if (next() % 4 == 0)
                    s.resize(next() % (s.size() + 1));
            }

            /* Detection must never throw or read OOB. */
            (void) detectMachOSignature(s);

            /* Check mode is read-only and must never throw. */
            std::string check = s;
            (void) fixupMachOSignature(check, "fuzz", true);
            EXPECT_EQ(check, s); // check mode never mutates

            /* Repair may legitimately throw on a CMS wrapper but must
               not crash. No idempotence assertion here: a poke can
               corrupt the CodeDirectory header (hashOffset,
               nCodeSlots) so a second pass hashes a different,
               overlapping slot region — a malformed CD need not be
               idempotent. Idempotence on well-formed input is covered
               above. */
            std::string repair = s;
            try {
                (void) fixupMachOSignature(repair, "fuzz", false);
                (void) fixupMachOSignature(repair, "fuzz", true);
            } catch (const Error &) {
                /* CMS or other unrepairable — fine, just no crash. */
            }
        }
    }
}

} // namespace nix
