#include <gtest/gtest.h>

#include "nix/util/wtf8.hh"

namespace nix {

/* ----------------------------------------------------------------------------
 * Round trips
 * --------------------------------------------------------------------------*/

TEST(wtf8, asciiRoundTrips)
{
    std::u16string in = u"C:\\Users\\test\\file.txt";
    EXPECT_EQ(wtf8FromUtf16(in), "C:\\Users\\test\\file.txt");
    EXPECT_EQ(utf16FromWtf8(wtf8FromUtf16(in)), in);
}

TEST(wtf8, emptyRoundTrips)
{
    EXPECT_EQ(wtf8FromUtf16(u""), "");
    EXPECT_EQ(utf16FromWtf8(""), u"");
}

TEST(wtf8, twoAndThreeByteCodePointsRoundTrip)
{
    /* U+00E9 (2 bytes) and U+65E5 (3 bytes). */
    std::u16string in = u"caf\u00e9 \u65e5";
    auto encoded = wtf8FromUtf16(in);
    EXPECT_EQ(encoded, "caf\xc3\xa9 \xe6\x97\xa5");
    EXPECT_EQ(utf16FromWtf8(encoded), in);
}

TEST(wtf8, supplementaryCodePointUsesFourBytesNotTwoSurrogates)
{
    /* U+1F600 arrives as a surrogate pair in UTF-16. A valid pair must be
       combined into the four-byte form, otherwise WTF-8 would not be a
       superset of UTF-8 for well-formed input. */
    std::u16string in = {0xD83D, 0xDE00};
    auto encoded = wtf8FromUtf16(in);
    EXPECT_EQ(encoded, "\xf0\x9f\x98\x80");
    EXPECT_EQ(encoded.size(), 4u);
    EXPECT_EQ(utf16FromWtf8(encoded), in);
}

/* ----------------------------------------------------------------------------
 * Unpaired surrogates: the entire reason this exists
 * --------------------------------------------------------------------------*/

TEST(wtf8, unpairedHighSurrogateRoundTrips)
{
    /* A file named with a lone high surrogate is creatable on NTFS. The
       previous `codecvt_utf8_utf16` conversion threw `std::range_error`
       here, so such a name could not be handled at all. */
    std::u16string in = {u'a', 0xD800, u'b'};
    auto encoded = wtf8FromUtf16(in);
    EXPECT_EQ(utf16FromWtf8(encoded), in);
}

TEST(wtf8, unpairedLowSurrogateRoundTrips)
{
    std::u16string in = {u'a', 0xDC00, u'b'};
    EXPECT_EQ(utf16FromWtf8(wtf8FromUtf16(in)), in);
}

TEST(wtf8, unpairedSurrogateAtEndRoundTrips)
{
    /* A high surrogate with nothing after it exercises the bounds check in
       the pairing test rather than the pairing itself. */
    std::u16string in = {u'x', 0xD800};
    EXPECT_EQ(utf16FromWtf8(wtf8FromUtf16(in)), in);
}

TEST(wtf8, highSurrogateFollowedByNonLowSurrogateRoundTrips)
{
    /* Two high surrogates in a row are both unpaired. Combining them would
       silently invent a code point. */
    std::u16string in = {0xD800, 0xD801};
    auto encoded = wtf8FromUtf16(in);
    EXPECT_EQ(encoded.size(), 6u);
    EXPECT_EQ(utf16FromWtf8(encoded), in);
}

TEST(wtf8, surrogateEncodesAsItsOwnThreeByteSequence)
{
    std::u16string in = {0xD800};
    EXPECT_EQ(wtf8FromUtf16(in), "\xed\xa0\x80");
}

TEST(wtf8, everySurrogateValueRoundTrips)
{
    /* Exhaustive over the surrogate range, so no single value can be
       mishandled unnoticed. The counter is `unsigned` rather than
       `char16_t` both to avoid wrapping at the top of the range and
       because `operator<<(std::ostream &, char16_t)` is deleted, so a
       `char16_t` cannot be streamed into a failure message. */
    for (unsigned u = 0xD800; u <= 0xDFFF; ++u) {
        std::u16string in = {static_cast<char16_t>(u)};
        EXPECT_EQ(utf16FromWtf8(wtf8FromUtf16(in)), in) << "failed at U+" << std::hex << u;
    }
}

/* ----------------------------------------------------------------------------
 * Malformed input is replaced, never accepted as the value it spells
 * --------------------------------------------------------------------------*/

/* Counts below follow the maximal-subpart convention: a malformed sequence
   costs one replacement character for the byte that could not begin or
   continue a valid sequence, and the bytes after it are then examined on
   their own terms rather than being swallowed. All were measured. */

TEST(wtf8, overlongTwoByteEncodingIsRejected)
{
    /* C0 80 spells U+0000 in two bytes. Accepting it would let a caller
       smuggle a NUL past a check that inspected the bytes. C0 cannot begin
       any valid sequence, so it and the orphaned 80 each yield U+FFFD. */
    auto decoded = utf16FromWtf8("\xc0\x80");
    EXPECT_NE(decoded, std::u16string(1, u'\0'));
    EXPECT_EQ(decoded, std::u16string({0xFFFD, 0xFFFD}));
}

TEST(wtf8, overlongThreeByteEncodingIsRejected)
{
    /* E0 80 AF spells '/' in three bytes. Same hazard, separator edition:
       a path check that ran on the bytes would not see a separator here. */
    auto decoded = utf16FromWtf8("\xe0\x80\xaf");
    EXPECT_EQ(decoded, std::u16string({0xFFFD, 0xFFFD, 0xFFFD}));
    EXPECT_EQ(decoded.find(u'/'), std::u16string::npos);
}

TEST(wtf8, truncatedSequenceIsReplaced)
{
    EXPECT_EQ(utf16FromWtf8("\xe6\x97"), std::u16string({0xFFFD, 0xFFFD}));
}

TEST(wtf8, strayContinuationByteIsReplaced)
{
    EXPECT_EQ(utf16FromWtf8("\x80"), std::u16string({0xFFFD}));
}

TEST(wtf8, invalidLeadByteIsReplaced)
{
    /* F7 cannot begin a valid sequence at all; in strict UTF-8 it would
       spell U+1FFFFF, past U+10FFFF. */
    EXPECT_EQ(utf16FromWtf8("\xf7\xbf\xbf\xbf"), std::u16string({0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD}));
}

TEST(wtf8, fourByteSequenceAboveMaxCodePointIsRejected)
{
    /* F4 90 80 80 spells U+110000. F4 is a legal lead, so this is caught by
       the restriction on its second byte rather than by the lead itself. */
    EXPECT_EQ(utf16FromWtf8("\xf4\x90\x80\x80"), std::u16string({0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD}));
}

TEST(wtf8, surrogateThreeByteSequenceIsAcceptedNotReplaced)
{
    /* ED A0 80 is rejected by a strict UTF-8 decoder and must be accepted
       here. This is the decoder's one intentional divergence from UTF-8,
       and the reason the whole file exists. */
    EXPECT_EQ(utf16FromWtf8("\xed\xa0\x80"), std::u16string({0xD800}));
}

TEST(wtf8, decodingAlwaysTerminates)
{
    /* Every byte value as a one-byte input. The guarantee under test is
       that the decoder consumes at least one byte per iteration; a decoder
       that did not would hang rather than fail. */
    for (int b = 0; b < 256; ++b) {
        std::string in(1, static_cast<char>(b));
        auto out = utf16FromWtf8(in);
        EXPECT_FALSE(out.empty()) << "byte " << b << " produced nothing";
    }
}

} // namespace nix
