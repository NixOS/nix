#include "nix/store/s3-url.hh"
#include "nix/util/tests/gmock-matchers.hh"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace nix {

// =============================================================================
// ParsedS3URL Tests
// =============================================================================

struct ParsedS3URLTestCase
{
    std::string url;
    ParsedS3URL expected;
    std::string description;
};

class ParsedS3URLTest : public ::testing::WithParamInterface<ParsedS3URLTestCase>, public ::testing::Test
{};

TEST_P(ParsedS3URLTest, parseS3URLSuccessfully)
{
    const auto & testCase = GetParam();
    auto parsed = ParsedS3URL::parse(parseURL(testCase.url));
    ASSERT_EQ(parsed, testCase.expected);
}

INSTANTIATE_TEST_SUITE_P(
    QueryParams,
    ParsedS3URLTest,
    ::testing::Values(
        ParsedS3URLTestCase{
            "s3://my-bucket/my-key.txt",
            {
                .bucket = "my-bucket",
                .key = {"my-key.txt"},
            },
            "basic_s3_bucket",
        },
        ParsedS3URLTestCase{
            "s3://prod-cache/nix/store/abc123.nar.xz?region=eu-west-1",
            {
                .bucket = "prod-cache",
                .key = {"nix", "store", "abc123.nar.xz"},
                .region = "eu-west-1",
            },
            "with_region",
        },
        ParsedS3URLTestCase{
            "s3://bucket/key?region=us-west-2&profile=prod&endpoint=custom.s3.com&scheme=https&region=us-east-1",
            {
                .bucket = "bucket",
                .key = {"key"},
                .profile = "prod",
                .region = "us-west-2", //< using the first parameter (decodeQuery ignores dupicates)
                .scheme = "https",
                .endpoint = ParsedURL::Authority{.host = "custom.s3.com"},
            },
            "complex",
        },
        ParsedS3URLTestCase{
            "s3://cache/file.txt?profile=production&region=ap-southeast-2",
            {
                .bucket = "cache",
                .key = {"file.txt"},
                .profile = "production",
                .region = "ap-southeast-2",
            },
            "with_profile_and_region",
        },
        ParsedS3URLTestCase{
            "s3://my-bucket/my-key.txt?versionId=abc123xyz",
            {
                .bucket = "my-bucket",
                .key = {"my-key.txt"},
                .versionId = "abc123xyz",
            },
            "with_versionId",
        },
        ParsedS3URLTestCase{
            "s3://bucket/path/to/object?region=eu-west-1&versionId=version456",
            {
                .bucket = "bucket",
                .key = {"path", "to", "object"},
                .region = "eu-west-1",
                .versionId = "version456",
            },
            "with_region_and_versionId",
        },
        ParsedS3URLTestCase{
            "s3://bucket/key?endpoint=https://minio.local&scheme=http",
            {
                .bucket = "bucket",
                .key = {"key"},
                /* TODO: Figure out what AWS SDK is doing when both endpointOverride and scheme are set. */
                .scheme = "http",
                .endpoint =
                    ParsedURL{
                        .scheme = "https",
                        .authority = ParsedURL::Authority{.host = "minio.local"},
                        .path = {""},
                    },
            },
            "with_absolute_endpoint_uri",
        },
        ParsedS3URLTestCase{
            "s3://bucket/key?addressing-style=virtual",
            {
                .bucket = "bucket",
                .key = {"key"},
                .addressingStyle = S3AddressingStyle::Virtual,
            },
            "with_addressing_style_virtual",
        },
        ParsedS3URLTestCase{
            "s3://bucket/key?addressing-style=path",
            {
                .bucket = "bucket",
                .key = {"key"},
                .addressingStyle = S3AddressingStyle::Path,
            },
            "with_addressing_style_path",
        },
        ParsedS3URLTestCase{
            "s3://bucket/key?addressing-style=auto",
            {
                .bucket = "bucket",
                .key = {"key"},
                .addressingStyle = S3AddressingStyle::Auto,
            },
            "with_addressing_style_auto",
        }),
    [](const ::testing::TestParamInfo<ParsedS3URLTestCase> & info) { return info.param.description; });

// Parameterized test for invalid S3 URLs
struct InvalidS3URLTestCase
{
    std::string url;
    std::string expectedErrorSubstring;
    std::string description;
};

class InvalidParsedS3URLTest : public ::testing::WithParamInterface<InvalidS3URLTestCase>, public ::testing::Test
{};

TEST_P(InvalidParsedS3URLTest, parseS3URLErrors)
{
    const auto & testCase = GetParam();

    ASSERT_THAT(
        [&testCase]() { ParsedS3URL::parse(parseURL(testCase.url)); },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher(testCase.expectedErrorSubstring)));
}

INSTANTIATE_TEST_SUITE_P(
    InvalidUrls,
    InvalidParsedS3URLTest,
    ::testing::Values(
        InvalidS3URLTestCase{"s3:///key", "error: URI has a missing or invalid bucket name", "empty_bucket"},
        InvalidS3URLTestCase{"s3://127.0.0.1", "error: URI has a missing or invalid bucket name", "ip_address_bucket"},
        InvalidS3URLTestCase{"s3://bucket with spaces/key", "is not a valid URL", "bucket_with_spaces"},
        InvalidS3URLTestCase{"s3://", "error: URI has a missing or invalid bucket name", "completely_empty"},
        InvalidS3URLTestCase{"s3://bucket", "error: URI has a missing or invalid key", "missing_key"}),
    [](const ::testing::TestParamInfo<InvalidS3URLTestCase> & info) { return info.param.description; });

TEST(ParsedS3URLTest, invalidAddressingStyleThrows)
{
    ASSERT_THROW(ParsedS3URL::parse(parseURL("s3://bucket/key?addressing-style=bogus")), InvalidS3AddressingStyle);
}

TEST(ParsedS3URLTest, virtualStyleWithAuthoritylessEndpointThrows)
{
    ParsedS3URL input{
        .bucket = "bucket",
        .key = {"key"},
        .addressingStyle = S3AddressingStyle::Virtual,
        .endpoint =
            ParsedURL{
                .scheme = "file",
                .path = {"", "some", "path"},
            },
    };
    ASSERT_THROW(input.toHttpsUrl(), nix::Error);
}

// =============================================================================
// S3 URL to HTTPS Conversion Tests
// =============================================================================

struct S3ToHttpsConversionTestCase
{
    ParsedS3URL input;
    ParsedURL expected;
    std::string expectedRendered;
    std::string description;
};

class S3ToHttpsConversionTest : public ::testing::WithParamInterface<S3ToHttpsConversionTestCase>,
                                public ::testing::Test
{};

TEST_P(S3ToHttpsConversionTest, ConvertsCorrectly)
{
    const auto & testCase = GetParam();
    auto result = testCase.input.toHttpsUrl();
    EXPECT_EQ(result, testCase.expected) << "Failed for: " << testCase.description;
    EXPECT_EQ(result.to_string(), testCase.expectedRendered);
}

INSTANTIATE_TEST_SUITE_P(
    S3ToHttpsConversion,
    S3ToHttpsConversionTest,
    ::testing::Values(
        // Default (auto) addressing style: virtual-hosted for standard AWS endpoints
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "my-bucket",
                .key = {"my-key.txt"},
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "my-bucket.s3.us-east-1.amazonaws.com"},
                .path = {"", "my-key.txt"},
            },
            "https://my-bucket.s3.us-east-1.amazonaws.com/my-key.txt",
            "basic_s3_default_region",
        },
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "prod-cache",
                .key = {"nix", "store", "abc123.nar.xz"},
                .region = "eu-west-1",
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "prod-cache.s3.eu-west-1.amazonaws.com"},
                .path = {"", "nix", "store", "abc123.nar.xz"},
            },
            "https://prod-cache.s3.eu-west-1.amazonaws.com/nix/store/abc123.nar.xz",
            "with_eu_west_1_region",
        },
        // Custom endpoint authority: path-style by default
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "bucket",
                .key = {"key"},
                .scheme = "http",
                .endpoint = ParsedURL::Authority{.host = "custom.s3.com"},
            },
            ParsedURL{
                .scheme = "http",
                .authority = ParsedURL::Authority{.host = "custom.s3.com"},
                .path = {"", "bucket", "key"},
            },
            "http://custom.s3.com/bucket/key",
            "custom_endpoint_authority",
        },
        // Custom endpoint URL: path-style by default
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "bucket",
                .key = {"key"},
                .endpoint =
                    ParsedURL{
                        .scheme = "http",
                        .authority = ParsedURL::Authority{.host = "server", .port = 9000},
                        .path = {""},
                    },
            },
            ParsedURL{
                .scheme = "http",
                .authority = ParsedURL::Authority{.host = "server", .port = 9000},
                .path = {"", "bucket", "key"},
            },
            "http://server:9000/bucket/key",
            "custom_endpoint_with_port",
        },
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "bucket",
                .key = {"path", "to", "file.txt"},
                .region = "ap-southeast-2",
                .scheme = "https",
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "bucket.s3.ap-southeast-2.amazonaws.com"},
                .path = {"", "path", "to", "file.txt"},
            },
            "https://bucket.s3.ap-southeast-2.amazonaws.com/path/to/file.txt",
            "complex_path_and_region",
        },
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "my-bucket",
                .key = {"my-key.txt"},
                .versionId = "abc123xyz",
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "my-bucket.s3.us-east-1.amazonaws.com"},
                .path = {"", "my-key.txt"},
                .query = {{"versionId", "abc123xyz"}},
            },
            "https://my-bucket.s3.us-east-1.amazonaws.com/my-key.txt?versionId=abc123xyz",
            "with_versionId",
        },
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "versioned-bucket",
                .key = {"path", "to", "object"},
                .region = "eu-west-1",
                .versionId = "version456",
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "versioned-bucket.s3.eu-west-1.amazonaws.com"},
                .path = {"", "path", "to", "object"},
                .query = {{"versionId", "version456"}},
            },
            "https://versioned-bucket.s3.eu-west-1.amazonaws.com/path/to/object?versionId=version456",
            "with_region_and_versionId",
        },
        // Explicit addressing-style=path forces path-style on standard AWS endpoints
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "my-bucket",
                .key = {"my-key.txt"},
                .region = "us-west-2",
                .addressingStyle = S3AddressingStyle::Path,
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "s3.us-west-2.amazonaws.com"},
                .path = {"", "my-bucket", "my-key.txt"},
            },
            "https://s3.us-west-2.amazonaws.com/my-bucket/my-key.txt",
            "explicit_path_style",
        },
        // Explicit addressing-style=virtual forces virtual-hosted-style on custom endpoints
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "bucket",
                .key = {"key"},
                .scheme = "http",
                .addressingStyle = S3AddressingStyle::Virtual,
                .endpoint = ParsedURL::Authority{.host = "custom.s3.com"},
            },
            ParsedURL{
                .scheme = "http",
                .authority = ParsedURL::Authority{.host = "bucket.custom.s3.com"},
                .path = {"", "key"},
            },
            "http://bucket.custom.s3.com/key",
            "explicit_virtual_style_custom_endpoint",
        },
        // Explicit addressing-style=virtual with full endpoint URL
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "bucket",
                .key = {"key"},
                .addressingStyle = S3AddressingStyle::Virtual,
                .endpoint =
                    ParsedURL{
                        .scheme = "http",
                        .authority = ParsedURL::Authority{.host = "server", .port = 9000},
                        .path = {""},
                    },
            },
            ParsedURL{
                .scheme = "http",
                .authority = ParsedURL::Authority{.host = "bucket.server", .port = 9000},
                .path = {"", "key"},
            },
            "http://bucket.server:9000/key",
            "explicit_virtual_style_full_endpoint_url",
        },
        // Dotted bucket names work normally with explicit path-style
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "my.bucket",
                .key = {"key"},
                .addressingStyle = S3AddressingStyle::Path,
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "s3.us-east-1.amazonaws.com"},
                .path = {"", "my.bucket", "key"},
            },
            "https://s3.us-east-1.amazonaws.com/my.bucket/key",
            "dotted_bucket_with_path_style",
        },
        // Dotted bucket names fall back to path-style with auto on standard AWS endpoints
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "my.bucket.name",
                .key = {"key"},
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "s3.us-east-1.amazonaws.com"},
                .path = {"", "my.bucket.name", "key"},
            },
            "https://s3.us-east-1.amazonaws.com/my.bucket.name/key",
            "dotted_bucket_with_auto_style_on_aws",
        },
        // Dotted bucket names work with auto style on custom endpoints (auto = path-style)
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "my.bucket",
                .key = {"key"},
                .endpoint = ParsedURL::Authority{.host = "minio.local"},
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "minio.local"},
                .path = {"", "my.bucket", "key"},
            },
            "https://minio.local/my.bucket/key",
            "dotted_bucket_with_auto_style_custom_endpoint",
        }),
    [](const ::testing::TestParamInfo<S3ToHttpsConversionTestCase> & info) { return info.param.description; });

// =============================================================================
// S3 URL to HTTPS Conversion Error Tests
// =============================================================================

struct S3ToHttpsConversionErrorTestCase
{
    ParsedS3URL input;
    std::string description;
};

class S3ToHttpsConversionErrorTest : public ::testing::WithParamInterface<S3ToHttpsConversionErrorTestCase>,
                                     public ::testing::Test
{};

TEST_P(S3ToHttpsConversionErrorTest, ThrowsOnConversion)
{
    auto & [input, description] = GetParam();
    ASSERT_THROW(input.toHttpsUrl(), nix::Error);
}

INSTANTIATE_TEST_SUITE_P(
    S3ToHttpsConversionErrors,
    S3ToHttpsConversionErrorTest,
    ::testing::Values(
        S3ToHttpsConversionErrorTestCase{
            ParsedS3URL{
                .bucket = "bucket",
                .key = {"key"},
                .addressingStyle = S3AddressingStyle::Virtual,
                .endpoint = ParsedURL::Authority{.host = ""},
            },
            "virtual_style_with_empty_host_authority",
        },
        S3ToHttpsConversionErrorTestCase{
            ParsedS3URL{
                .bucket = "my.bucket",
                .key = {"key"},
                .addressingStyle = S3AddressingStyle::Virtual,
            },
            "dotted_bucket_with_explicit_virtual_style",
        },
        S3ToHttpsConversionErrorTestCase{
            ParsedS3URL{
                .bucket = "my.bucket.name",
                .key = {"key"},
                .addressingStyle = S3AddressingStyle::Virtual,
            },
            "dotted_bucket_with_explicit_virtual_style_multi_dot",
        },
        S3ToHttpsConversionErrorTestCase{
            ParsedS3URL{
                .bucket = "my.bucket",
                .key = {"key"},
                .addressingStyle = S3AddressingStyle::Virtual,
                .endpoint = ParsedURL::Authority{.host = "minio.local"},
            },
            "dotted_bucket_with_explicit_virtual_style_custom_authority",
        },
        S3ToHttpsConversionErrorTestCase{
            ParsedS3URL{
                .bucket = "my.bucket",
                .key = {"key"},
                .addressingStyle = S3AddressingStyle::Virtual,
                .endpoint =
                    ParsedURL{
                        .scheme = "http",
                        .authority = ParsedURL::Authority{.host = "minio.local", .port = 9000},
                        .path = {""},
                    },
            },
            "dotted_bucket_with_explicit_virtual_style_full_endpoint_url",
        }),
    [](const ::testing::TestParamInfo<S3ToHttpsConversionErrorTestCase> & info) { return info.param.description; });

} // namespace nix
