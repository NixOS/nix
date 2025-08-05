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

// A SHA-512 hash. Hex encoded to be clearer / distinct from the Base64 test case.
const std::string expectedDecoded = base16::decode(
    "ee0f754c1bd8a18428ad14eaa3ead80ff8b96275af5012e7a8384f1f10490da056eec9ae3cc791a7a13a24e16e54df5bccdd109c7d53a14534bbd7360a300b11");

struct Base64TrailingParseCase
{
    std::string sri;
};

class Base64TrailParseTest : public ::testing::TestWithParam<Base64TrailingParseCase>
{};

TEST_P(Base64TrailParseTest, AcceptsVariousSha512Paddings)
{
    auto sri = GetParam().sri;
    auto decoded = base64::decode(sri);

    EXPECT_EQ(decoded, expectedDecoded);
}

/* Nix's Base64 implementation has historically accepted trailing
   garbage. We may want to warn about this in the future, but we cannot
   take it away suddenly.

   Test case taken from Snix:
   https://git.snix.dev/snix/snix/src/commit/2a29b90c7f3f3c52b5bdae50260fb0bd903c6b38/snix/nix-compat/src/nixhash/mod.rs#L431
 */
INSTANTIATE_TEST_SUITE_P(
    Sha512Paddings,
    Base64TrailParseTest,
    ::testing::Values(
        Base64TrailingParseCase{
            "7g91TBvYoYQorRTqo+rYD/i5YnWvUBLnqDhPHxBJDaBW7smuPMeRp6E6JOFuVN9bzN0QnH1ToUU0u9c2CjALEQ"},
        Base64TrailingParseCase{
            "7g91TBvYoYQorRTqo+rYD/i5YnWvUBLnqDhPHxBJDaBW7smuPMeRp6E6JOFuVN9bzN0QnH1ToUU0u9c2CjALEQ="},
        Base64TrailingParseCase{
            "7g91TBvYoYQorRTqo+rYD/i5YnWvUBLnqDhPHxBJDaBW7smuPMeRp6E6JOFuVN9bzN0QnH1ToUU0u9c2CjALEQ=="},
        Base64TrailingParseCase{
            "7g91TBvYoYQorRTqo+rYD/i5YnWvUBLnqDhPHxBJDaBW7smuPMeRp6E6JOFuVN9bzN0QnH1ToUU0u9c2CjALEQ==="},
        Base64TrailingParseCase{
            "7g91TBvYoYQorRTqo+rYD/i5YnWvUBLnqDhPHxBJDaBW7smuPMeRp6E6JOFuVN9bzN0QnH1ToUU0u9c2CjALEQ== cheesecake"}));

} // namespace nix
