#include "nix/store/s3.hh"
#include "nix/util/tests/gmock-matchers.hh"

#if NIX_WITH_S3_SUPPORT

#  include <gtest/gtest.h>
#  include <gmock/gmock.h>

namespace nix {

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
        }),
    [](const ::testing::TestParamInfo<ParsedS3URLTestCase> & info) { return info.param.description; });

TEST(InvalidParsedS3URLTest, parseS3URLErrors)
{
    auto invalidBucketMatcher = ::testing::ThrowsMessage<BadURL>(
        testing::HasSubstrIgnoreANSIMatcher("error: URI has a missing or invalid bucket name"));

    /* Empty bucket (authority) */
    ASSERT_THAT([]() { ParsedS3URL::parse(parseURL("s3:///key")); }, invalidBucketMatcher);
    /* Invalid bucket name */
    ASSERT_THAT([]() { ParsedS3URL::parse(parseURL("s3://127.0.0.1")); }, invalidBucketMatcher);
}

// Parameterized test for s3ToHttpsUrl conversion
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
        }),
    [](const ::testing::TestParamInfo<S3ToHttpsConversionTestCase> & info) { return info.param.description; });

} // namespace nix

#endif
