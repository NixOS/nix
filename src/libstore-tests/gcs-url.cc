#include "nix/store/gcs-url.hh"
#include "nix/util/tests/gmock-matchers.hh"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace nix {

// =============================================================================
// ParsedGcsURL Tests
// =============================================================================

struct ParsedGcsURLTestCase
{
    std::string url;
    ParsedGcsURL expected;
    std::string description;
};

class ParsedGcsURLTest : public ::testing::WithParamInterface<ParsedGcsURLTestCase>, public ::testing::Test
{};

TEST_P(ParsedGcsURLTest, parseGcsURLSuccessfully)
{
    const auto & testCase = GetParam();
    auto parsed = ParsedGcsURL::parse(parseURL(testCase.url));
    ASSERT_EQ(parsed, testCase.expected);
}

INSTANTIATE_TEST_SUITE_P(
    ValidUrls,
    ParsedGcsURLTest,
    ::testing::Values(
        ParsedGcsURLTestCase{
            "gs://my-bucket/my-key.txt",
            {
                .bucket = "my-bucket",
                .key = {"my-key.txt"},
                .writable = false,
            },
            "basic_gcs_bucket",
        },
        ParsedGcsURLTestCase{
            "gs://nix-cache/nix/store/abc123.nar.xz",
            {
                .bucket = "nix-cache",
                .key = {"nix", "store", "abc123.nar.xz"},
                .writable = false,
            },
            "nested_path",
        },
        ParsedGcsURLTestCase{
            "gs://my-bucket/path/to/deep/file.txt",
            {
                .bucket = "my-bucket",
                .key = {"path", "to", "deep", "file.txt"},
                .writable = false,
            },
            "deeply_nested_path",
        },
        ParsedGcsURLTestCase{
            "gs://bucket-with-dashes/key",
            {
                .bucket = "bucket-with-dashes",
                .key = {"key"},
                .writable = false,
            },
            "bucket_with_dashes",
        },
        ParsedGcsURLTestCase{
            "gs://bucket123/file-with-special_chars.tar.gz",
            {
                .bucket = "bucket123",
                .key = {"file-with-special_chars.tar.gz"},
                .writable = false,
            },
            "key_with_special_chars",
        },
        ParsedGcsURLTestCase{
            "gs://my-bucket/key?write=true",
            {
                .bucket = "my-bucket",
                .key = {"key"},
                .writable = true,
            },
            "with_write_true",
        },
        ParsedGcsURLTestCase{
            "gs://my-bucket/key?write=false",
            {
                .bucket = "my-bucket",
                .key = {"key"},
                .writable = false,
            },
            "with_write_false",
        },
        ParsedGcsURLTestCase{
            "gs://cache/path/to/nar.xz?write=true",
            {
                .bucket = "cache",
                .key = {"path", "to", "nar.xz"},
                .writable = true,
            },
            "nested_path_with_write",
        }),
    [](const ::testing::TestParamInfo<ParsedGcsURLTestCase> & info) { return info.param.description; });

// Parameterized test for invalid GCS URLs
struct InvalidGcsURLTestCase
{
    std::string url;
    std::string expectedErrorSubstring;
    std::string description;
};

class InvalidParsedGcsURLTest : public ::testing::WithParamInterface<InvalidGcsURLTestCase>, public ::testing::Test
{};

TEST_P(InvalidParsedGcsURLTest, parseGcsURLErrors)
{
    const auto & testCase = GetParam();

    ASSERT_THAT(
        [&testCase]() { ParsedGcsURL::parse(parseURL(testCase.url)); },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher(testCase.expectedErrorSubstring)));
}

INSTANTIATE_TEST_SUITE_P(
    InvalidUrls,
    InvalidParsedGcsURLTest,
    ::testing::Values(
        InvalidGcsURLTestCase{"gs:///key", "error: URI has a missing or invalid bucket name", "empty_bucket"},
        InvalidGcsURLTestCase{"gs://127.0.0.1/key", "error: URI has a missing or invalid bucket name", "ip_address_bucket"},
        InvalidGcsURLTestCase{"gs://", "error: URI has a missing or invalid bucket name", "completely_empty"},
        InvalidGcsURLTestCase{"gs://bucket", "error: URI has a missing or invalid key", "missing_key"}),
    [](const ::testing::TestParamInfo<InvalidGcsURLTestCase> & info) { return info.param.description; });

// =============================================================================
// GCS URL to HTTPS Conversion Tests
// =============================================================================

struct GcsToHttpsConversionTestCase
{
    ParsedGcsURL input;
    ParsedURL expected;
    std::string expectedRendered;
    std::string description;
};

class GcsToHttpsConversionTest : public ::testing::WithParamInterface<GcsToHttpsConversionTestCase>,
                                 public ::testing::Test
{};

TEST_P(GcsToHttpsConversionTest, ConvertsCorrectly)
{
    const auto & testCase = GetParam();
    auto result = testCase.input.toHttpsUrl();
    EXPECT_EQ(result, testCase.expected) << "Failed for: " << testCase.description;
    EXPECT_EQ(result.to_string(), testCase.expectedRendered);
}

INSTANTIATE_TEST_SUITE_P(
    GcsToHttpsConversion,
    GcsToHttpsConversionTest,
    ::testing::Values(
        GcsToHttpsConversionTestCase{
            ParsedGcsURL{
                .bucket = "my-bucket",
                .key = {"my-key.txt"},
                .writable = false,
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "storage.googleapis.com"},
                .path = {"", "my-bucket", "my-key.txt"},
            },
            "https://storage.googleapis.com/my-bucket/my-key.txt",
            "basic_conversion",
        },
        GcsToHttpsConversionTestCase{
            ParsedGcsURL{
                .bucket = "nix-cache",
                .key = {"nix", "store", "abc123.nar.xz"},
                .writable = false,
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "storage.googleapis.com"},
                .path = {"", "nix-cache", "nix", "store", "abc123.nar.xz"},
            },
            "https://storage.googleapis.com/nix-cache/nix/store/abc123.nar.xz",
            "nested_path_conversion",
        },
        GcsToHttpsConversionTestCase{
            ParsedGcsURL{
                .bucket = "bucket",
                .key = {"path", "to", "deep", "object.txt"},
                .writable = true,  // writable doesn't affect HTTPS URL conversion
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "storage.googleapis.com"},
                .path = {"", "bucket", "path", "to", "deep", "object.txt"},
            },
            "https://storage.googleapis.com/bucket/path/to/deep/object.txt",
            "deeply_nested_path_conversion",
        }),
    [](const ::testing::TestParamInfo<GcsToHttpsConversionTestCase> & info) { return info.param.description; });

} // namespace nix
