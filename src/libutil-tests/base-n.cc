#include <gtest/gtest.h>
#include <numeric>

#include "nix/util/base-n.hh"
#include "nix/util/error.hh"

namespace nix {

static const std::span<const std::byte> stringToByteSpan(const std::string_view s)
{
    return {(const std::byte *) s.data(), s.size()};
}

/* ----------------------------------------------------------------------------
 * base64::encode
 * --------------------------------------------------------------------------*/

TEST(base64Encode, emptyString)
{
    ASSERT_EQ(base64::encode(stringToByteSpan("")), "");
}

TEST(base64Encode, encodesAString)
{
    ASSERT_EQ(base64::encode(stringToByteSpan("quod erat demonstrandum")), "cXVvZCBlcmF0IGRlbW9uc3RyYW5kdW0=");
}

TEST(base64Encode, encodeAndDecode)
{
    auto s = "quod erat demonstrandum";
    auto encoded = base64::encode(stringToByteSpan(s));
    auto decoded = base64::decode(encoded);

    ASSERT_EQ(decoded, s);
}

TEST(base64Encode, encodeAndDecodeNonPrintable)
{
    char s[256];
    std::iota(std::rbegin(s), std::rend(s), 0);

    auto encoded = base64::encode(std::as_bytes(std::span<const char>{std::string_view{s}}));
    auto decoded = base64::decode(encoded);

    EXPECT_EQ(decoded.length(), 255u);
    ASSERT_EQ(decoded, s);
}

/* ----------------------------------------------------------------------------
 * base64::decode
 * --------------------------------------------------------------------------*/

TEST(base64Decode, emptyString)
{
    ASSERT_EQ(base64::decode(""), "");
}

TEST(base64Decode, decodeAString)
{
    ASSERT_EQ(base64::decode("cXVvZCBlcmF0IGRlbW9uc3RyYW5kdW0="), "quod erat demonstrandum");
}

TEST(base64Decode, decodeThrowsOnInvalidChar)
{
    ASSERT_THROW(base64::decode("cXVvZCBlcm_0IGRlbW9uc3RyYW5kdW0="), Error);
}

} // namespace nix
