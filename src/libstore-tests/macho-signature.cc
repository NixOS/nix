#include "nix/store/macho-signature.hh"

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

} // namespace nix
