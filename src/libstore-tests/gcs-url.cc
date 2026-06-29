#include "nix/store/gcs-url.hh"
#include "nix/util/tests/gmock-matchers.hh"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace nix {

struct ParsedGCSURLTestCase
{
    std::string url;
    ParsedGCSURL expected;
    std::string description;
};

class ParsedGCSURLTest : public ::testing::WithParamInterface<ParsedGCSURLTestCase>, public ::testing::Test
{};

TEST_P(ParsedGCSURLTest, parseGCSURLSuccessfully)
{
    const auto & testCase = GetParam();
    auto parsed = ParsedGCSURL::parse(parseURL(testCase.url));
    ASSERT_EQ(parsed, testCase.expected);
}

INSTANTIATE_TEST_SUITE_P(
    QueryParams,
    ParsedGCSURLTest,
    ::testing::Values(
        ParsedGCSURLTestCase{
            "gs://my-bucket/my-key.txt",
            {
                .bucket = "my-bucket",
                .key = {"my-key.txt"},
            },
            "basic_gs_bucket",
        },
        ParsedGCSURLTestCase{
            "gs://prod-cache/nix/store/abc123.nar.xz",
            {
                .bucket = "prod-cache",
                .key = {"nix", "store", "abc123.nar.xz"},
            },
            "nested_key",
        },
        ParsedGCSURLTestCase{
            "gs://bucket/key?user-project=proj&generation=42",
            {
                .bucket = "bucket",
                .key = {"key"},
                .userProject = "proj",
                .generation = "42",
            },
            "all_params",
        }),
    [](const ::testing::TestParamInfo<ParsedGCSURLTestCase> & info) { return info.param.description; });

struct InvalidGCSURLTestCase
{
    std::string url;
    std::string expectedErrorSubstring;
    std::string description;
};

class InvalidParsedGCSURLTest : public ::testing::WithParamInterface<InvalidGCSURLTestCase>, public ::testing::Test
{};

TEST_P(InvalidParsedGCSURLTest, parseGCSURLErrors)
{
    const auto & testCase = GetParam();

    ASSERT_THAT(
        [&testCase]() { ParsedGCSURL::parse(parseURL(testCase.url)); },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher(testCase.expectedErrorSubstring)));
}

INSTANTIATE_TEST_SUITE_P(
    InvalidUrls,
    InvalidParsedGCSURLTest,
    ::testing::Values(
        InvalidGCSURLTestCase{"gs:///key", "error: URI has a missing or invalid bucket name", "empty_bucket"},
        InvalidGCSURLTestCase{"gs://127.0.0.1", "error: URI has a missing or invalid bucket name", "ip_address_bucket"},
        InvalidGCSURLTestCase{"gs://", "error: URI has a missing or invalid bucket name", "completely_empty"},
        InvalidGCSURLTestCase{"gs://bucket", "error: URI has a missing or invalid key", "missing_key"},
        InvalidGCSURLTestCase{"s3://bucket/key", "error: URI scheme 's3' is not 'gs'", "wrong_scheme"},
        /* Bearer tokens are host-independent. Refusing here is the security boundary that keeps fetchurl from
           exfiltrating one. */
        InvalidGCSURLTestCase{
            "gs://bucket/key?endpoint=attacker.example", "not accepted in a gs:// URL", "endpoint_in_url_rejected"},
        InvalidGCSURLTestCase{"gs://bucket/key?scheme=http", "not accepted in a gs:// URL", "scheme_in_url_rejected"},
        InvalidGCSURLTestCase{
            "gs://bucket/key?user-project=p%0d%0aX-Evil:1", "invalid 'user-project' value", "user_project_crlf"},
        InvalidGCSURLTestCase{
            "gs://bucket/../other-bucket/key", "URI key has an invalid path segment", "dotdot_escapes_bucket"}),
    [](const ::testing::TestParamInfo<InvalidGCSURLTestCase> & info) { return info.param.description; });

TEST(ParsedGCSURL, toHttpsUrl)
{
    auto p = ParsedGCSURL::parse(parseURL("gs://prod-cache/nix/store/abc.nar.xz?generation=42"));
    EXPECT_EQ(
        p.toHttpsUrl().to_string(), "https://storage.googleapis.com/prod-cache/nix/store/abc.nar.xz?generation=42");
}

TEST(ParsedGCSURL, toHttpsUrlCustomEndpoint)
{
    ParsedGCSURL p{.bucket = "b", .key = {"k"}};
    EXPECT_EQ(p.toHttpsUrl(parseURL("http://emu.internal:4443")).to_string(), "http://emu.internal:4443/b/k");
    EXPECT_EQ(p.toHttpsUrl(parseURL("http://server:9000/prefix")).to_string(), "http://server:9000/prefix/b/k");
}

} // namespace nix
