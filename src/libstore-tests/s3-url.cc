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
        }
#if NIX_WITH_AWS_AUTH
        ,
        ParsedS3URLTestCase{
            "s3://my-bucket/key?use-transfer-acceleration=true",
            {
                .bucket = "my-bucket",
                .key = {"key"},
                .use_transfer_acceleration = true,
            },
            "with_transfer_acceleration",
        },
        ParsedS3URLTestCase{
            "s3://my-bucket/key?use-transfer-acceleration=false",
            {
                .bucket = "my-bucket",
                .key = {"key"},
                .use_transfer_acceleration = false,
            },
            "with_transfer_acceleration_false",
        },
        ParsedS3URLTestCase{
            "s3://my-bucket/key?use-transfer-acceleration=1",
            {
                .bucket = "my-bucket",
                .key = {"key"},
                .use_transfer_acceleration = true,
            },
            "with_transfer_acceleration_numeric",
        }
#endif
        ),
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

#if NIX_WITH_AWS_AUTH
// =============================================================================
// Transfer Acceleration Bucket Name Validation Tests
// =============================================================================

struct InvalidTransferAccelerationBucketTestCase
{
    std::string url;
    std::string expectedErrorSubstring;
    std::string description;
};

class InvalidTransferAccelerationBucketTest
    : public ::testing::WithParamInterface<InvalidTransferAccelerationBucketTestCase>,
      public ::testing::Test
{};

TEST_P(InvalidTransferAccelerationBucketTest, rejectsInvalidBucketNames)
{
    const auto & testCase = GetParam();
    auto parsed = ParsedS3URL::parse(parseURL(testCase.url));

    ASSERT_THAT(
        [&parsed]() { parsed.toHttpsUrl(); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher(testCase.expectedErrorSubstring)));
}

INSTANTIATE_TEST_SUITE_P(
    InvalidBucketNames,
    InvalidTransferAccelerationBucketTest,
    ::testing::Values(
        InvalidTransferAccelerationBucketTestCase{
            "s3://bucket.with.dots/key?use-transfer-acceleration=true",
            "is not compatible with S3 Transfer Acceleration",
            "bucket_with_dots",
        },
        InvalidTransferAccelerationBucketTestCase{
            "s3://my.bucket.name/key?use-transfer-acceleration=true",
            "is not compatible with S3 Transfer Acceleration",
            "bucket_with_multiple_dots",
        },
        InvalidTransferAccelerationBucketTestCase{
            "s3://bucket.name/key?use-transfer-acceleration=true",
            "is not compatible with S3 Transfer Acceleration",
            "bucket_with_single_dot",
        },
        InvalidTransferAccelerationBucketTestCase{
            "s3://bucket/key?use-transfer-acceleration=true&endpoint=minio.local",
            "cannot be used with custom endpoints",
            "acceleration_with_custom_endpoint",
        }),
    [](const ::testing::TestParamInfo<InvalidTransferAccelerationBucketTestCase> & info) {
        return info.param.description;
    });
#endif

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
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "my-bucket",
                .key = {"my-key.txt"},
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "s3.us-east-1.amazonaws.com"},
                .path = {"", "my-bucket", "my-key.txt"},
            },
            "https://s3.us-east-1.amazonaws.com/my-bucket/my-key.txt",
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
                .authority = ParsedURL::Authority{.host = "s3.eu-west-1.amazonaws.com"},
                .path = {"", "prod-cache", "nix", "store", "abc123.nar.xz"},
            },
            "https://s3.eu-west-1.amazonaws.com/prod-cache/nix/store/abc123.nar.xz",
            "with_eu_west_1_region",
        },
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
                .authority = ParsedURL::Authority{.host = "s3.ap-southeast-2.amazonaws.com"},
                .path = {"", "bucket", "path", "to", "file.txt"},
            },
            "https://s3.ap-southeast-2.amazonaws.com/bucket/path/to/file.txt",
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
                .authority = ParsedURL::Authority{.host = "s3.us-east-1.amazonaws.com"},
                .path = {"", "my-bucket", "my-key.txt"},
                .query = {{"versionId", "abc123xyz"}},
            },
            "https://s3.us-east-1.amazonaws.com/my-bucket/my-key.txt?versionId=abc123xyz",
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
                .authority = ParsedURL::Authority{.host = "s3.eu-west-1.amazonaws.com"},
                .path = {"", "versioned-bucket", "path", "to", "object"},
                .query = {{"versionId", "version456"}},
            },
            "https://s3.eu-west-1.amazonaws.com/versioned-bucket/path/to/object?versionId=version456",
            "with_region_and_versionId",
        },
#if NIX_WITH_AWS_AUTH
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "my-cache",
                .key = {"nix", "store", "abc123.nar.xz"},
                .use_transfer_acceleration = true,
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "my-cache.s3-accelerate.amazonaws.com"},
                .path = {"", "nix", "store", "abc123.nar.xz"},
            },
            "https://my-cache.s3-accelerate.amazonaws.com/nix/store/abc123.nar.xz",
            "transfer_acceleration_enabled",
        },
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "tokyo-cache",
                .key = {"key.txt"},
                .region = "ap-northeast-1",
                .use_transfer_acceleration = true,
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "tokyo-cache.s3-accelerate.amazonaws.com"},
                .path = {"", "key.txt"},
            },
            "https://tokyo-cache.s3-accelerate.amazonaws.com/key.txt",
            "transfer_acceleration_with_region",
        },
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "my-bucket",
                .key = {"file"},
                .use_transfer_acceleration = false,
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "s3.us-east-1.amazonaws.com"},
                .path = {"", "my-bucket", "file"},
            },
            "https://s3.us-east-1.amazonaws.com/my-bucket/file",
            "transfer_acceleration_explicitly_disabled",
        }
#endif
        ),
    [](const ::testing::TestParamInfo<S3ToHttpsConversionTestCase> & info) { return info.param.description; });

} // namespace nix
